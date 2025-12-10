#pragma once

/**
 * @file order_book.hpp
 * @brief High-performance limit order book with Price-Time Priority matching.
 *
 * DESIGN PRINCIPLES:
 * 1. Vector-based price levels for cache-friendly linear iteration.
 * 2. Hash map for O(1) order cancellation by ID.
 * 3. Price-Time Priority: best price first, FIFO within price level.
 * 4. Zero allocation during trading (uses external MemPool).
 *
 * MATCHING RULES:
 * - Buy orders match against asks if buy_price >= best_ask
 * - Sell orders match against bids if sell_price <= best_bid
 * - Execution price is always the resting order's price
 * - Partial fills reduce quantity, full fills remove from book
 */

#include "memory_pool.hpp"
#include "price_level.hpp"
#include "types.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace book {

// ============================================================================
// Execution Report (Trade notification)
// ============================================================================

/**
 * @brief Represents a single execution (trade).
 *
 * Generated when an incoming order matches against a resting order.
 */
struct Execution {
  uint64_t maker_id; ///< Resting order ID
  uint64_t taker_id; ///< Incoming order ID
  uint64_t price;    ///< Execution price (maker's price)
  uint32_t qty;      ///< Executed quantity
  Side maker_side;   ///< Maker's side (opposite of taker)
};

// ============================================================================
// OrderBook - Limit Order Book with Matching Engine
// ============================================================================

/**
 * @brief High-performance limit order book with matching engine.
 *
 * Maintains bid and ask sides as sorted vectors of price levels.
 * Provides O(1) order cancellation via hash map lookup.
 *
 * @tparam Capacity Maximum number of orders the pool can hold
 *
 * Key properties:
 * - Price-Time Priority matching (FIFO at each price level)
 * - Cache-friendly linear iteration through price levels
 * - O(1) cancel via order_map_ lookup
 * - Zero allocation during trading (pre-allocated pool)
 *
 * @example
 *   MemPool<Order, 1000000> pool;
 *   OrderBook<1000000> book(pool);
 *
 *   book.add_order(1, 10000, 100, Side::Buy);   // Buy 100 @ 1.0000
 *   book.add_order(2, 10100, 50, Side::Sell);   // Sell 50 @ 1.0100
 *
 *   auto spread = book.spread();  // 100 ticks = 0.0100
 */
template <std::size_t Capacity> class OrderBook {
public:
  // ========================================================================
  // Types
  // ========================================================================

  using PoolType = MemPool<Order, Capacity>;
  using ExecutionCallback = void (*)(const Execution &);

  // ========================================================================
  // Construction
  // ========================================================================

  /**
   * @brief Construct order book with reference to memory pool.
   *
   * @param pool Reference to pre-allocated memory pool for orders
   */
  explicit OrderBook(PoolType &pool) noexcept : pool_(pool) {}

  // Non-copyable
  OrderBook(const OrderBook &) = delete;
  OrderBook &operator=(const OrderBook &) = delete;

  // Non-movable (contains references)
  OrderBook(OrderBook &&) = delete;
  OrderBook &operator=(OrderBook &&) = delete;

  ~OrderBook() = default;

  // ========================================================================
  // Order Entry
  // ========================================================================

  /**
   * @brief Add a new limit order to the book.
   *
   * If the order crosses the spread, it will be matched against resting
   * orders using Price-Time Priority. Any remaining quantity rests in book.
   *
   * @param id Unique order identifier
   * @param price Price in ticks (fixed-point)
   * @param qty Quantity (shares)
   * @param side Order side (Buy or Sell)
   * @param on_execution Optional callback for trade notifications
   * @return true if order was added/matched, false if pool is full
   *
   * Complexity: O(k) where k is number of price levels crossed
   */
  bool add_order(uint64_t id, uint64_t price, uint32_t qty, Side side,
                 ExecutionCallback on_execution = nullptr) noexcept {
    // Check for duplicate order ID
    if (order_map_.find(id) != order_map_.end()) {
      return false;
    }

    uint32_t remaining_qty = qty;

    // Try to match against opposite side
    if (side == Side::Buy) {
      remaining_qty = match_buy(id, price, qty, on_execution);
    } else {
      remaining_qty = match_sell(id, price, qty, on_execution);
    }

    // If fully filled, no need to add to book
    if (remaining_qty == 0) {
      return true;
    }

    // Allocate order from pool
    Order *order = pool_.allocate();
    if (order == nullptr) {
      return false; // Pool exhausted
    }

    // Initialize order
    order->id = id;
    order->price = price;
    order->qty = remaining_qty;
    order->side = static_cast<char>(side);

    // Add to book
    if (side == Side::Buy) {
      add_to_bids(order);
    } else {
      add_to_asks(order);
    }

    // Register in order map for O(1) cancel
    order_map_[id] = order;

    return true;
  }

  // ========================================================================
  // Order Cancellation
  // ========================================================================

  /**
   * @brief Cancel an existing order.
   *
   * @param id Order ID to cancel
   * @return true if order was found and cancelled, false otherwise
   *
   * Complexity: O(1) for lookup + O(1) for removal from list
   *             Price level cleanup is O(n) in worst case
   */
  bool cancel_order(uint64_t id) noexcept {
    auto it = order_map_.find(id);
    if (it == order_map_.end()) {
      return false;
    }

    Order *order = it->second;
    order_map_.erase(it);

    // Remove from price level
    if (order->is_buy()) {
      remove_from_bids(order);
    } else {
      remove_from_asks(order);
    }

    // Return to pool
    pool_.deallocate(order);

    return true;
  }

  // ========================================================================
  // Market Data Accessors
  // ========================================================================

  /**
   * @brief Get best bid price.
   *
   * @return Best bid price, or nullopt if no bids
   */
  [[nodiscard]] std::optional<uint64_t> best_bid() const noexcept {
    if (bids_.empty() || bids_.front().empty()) {
      return std::nullopt;
    }
    return bids_.front().price;
  }

  /**
   * @brief Get best ask price.
   *
   * @return Best ask price, or nullopt if no asks
   */
  [[nodiscard]] std::optional<uint64_t> best_ask() const noexcept {
    if (asks_.empty() || asks_.front().empty()) {
      return std::nullopt;
    }
    return asks_.front().price;
  }

  /**
   * @brief Get bid-ask spread in ticks.
   *
   * @return Spread, or nullopt if either side is empty
   */
  [[nodiscard]] std::optional<uint64_t> spread() const noexcept {
    auto bid = best_bid();
    auto ask = best_ask();
    if (!bid || !ask) {
      return std::nullopt;
    }
    return *ask - *bid;
  }

  /**
   * @brief Get total volume at best bid.
   */
  [[nodiscard]] uint64_t best_bid_volume() const noexcept {
    if (bids_.empty()) {
      return 0;
    }
    return bids_.front().total_volume;
  }

  /**
   * @brief Get total volume at best ask.
   */
  [[nodiscard]] uint64_t best_ask_volume() const noexcept {
    if (asks_.empty()) {
      return 0;
    }
    return asks_.front().total_volume;
  }

  /**
   * @brief Check if order book is empty (no resting orders).
   */
  [[nodiscard]] bool empty() const noexcept {
    return bids_.empty() && asks_.empty();
  }

  /**
   * @brief Get number of resting orders.
   */
  [[nodiscard]] std::size_t order_count() const noexcept {
    return order_map_.size();
  }

  /**
   * @brief Get number of bid price levels.
   */
  [[nodiscard]] std::size_t bid_level_count() const noexcept {
    return bids_.size();
  }

  /**
   * @brief Get number of ask price levels.
   */
  [[nodiscard]] std::size_t ask_level_count() const noexcept {
    return asks_.size();
  }

  // ========================================================================
  // Direct access for testing
  // ========================================================================

  [[nodiscard]] const std::vector<PriceLevel> &bids() const noexcept {
    return bids_;
  }

  [[nodiscard]] const std::vector<PriceLevel> &asks() const noexcept {
    return asks_;
  }

private:
  // ========================================================================
  // Data Members
  // ========================================================================

  std::vector<PriceLevel> bids_; ///< Sorted descending (best bid first)
  std::vector<PriceLevel> asks_; ///< Sorted ascending (best ask first)
  std::unordered_map<uint64_t, Order *> order_map_; ///< ID -> Order*
  PoolType &pool_; ///< Reference to memory pool

  // ========================================================================
  // Matching Logic
  // ========================================================================

  /**
   * @brief Match a buy order against resting asks.
   *
   * @return Remaining quantity after matching
   */
  uint32_t match_buy(uint64_t taker_id, uint64_t price, uint32_t qty,
                     ExecutionCallback on_execution) noexcept {
    uint32_t remaining = qty;

    // Iterate through ask levels (lowest price first)
    while (remaining > 0 && !asks_.empty()) {
      PriceLevel &level = asks_.front();

      // Check if we can match (buy price >= ask price)
      if (price < level.price) {
        break; // No more matches possible
      }

      // Match against orders at this level
      remaining =
          match_at_level(level, taker_id, remaining, Side::Sell, on_execution);

      // Remove empty level
      if (level.empty()) {
        asks_.erase(asks_.begin());
      }
    }

    return remaining;
  }

  /**
   * @brief Match a sell order against resting bids.
   *
   * @return Remaining quantity after matching
   */
  uint32_t match_sell(uint64_t taker_id, uint64_t price, uint32_t qty,
                      ExecutionCallback on_execution) noexcept {
    uint32_t remaining = qty;

    // Iterate through bid levels (highest price first)
    while (remaining > 0 && !bids_.empty()) {
      PriceLevel &level = bids_.front();

      // Check if we can match (sell price <= bid price)
      if (price > level.price) {
        break; // No more matches possible
      }

      // Match against orders at this level
      remaining =
          match_at_level(level, taker_id, remaining, Side::Buy, on_execution);

      // Remove empty level
      if (level.empty()) {
        bids_.erase(bids_.begin());
      }
    }

    return remaining;
  }

  /**
   * @brief Match against orders at a single price level.
   *
   * @param level Price level to match against
   * @param taker_id Incoming order ID
   * @param qty Quantity to fill
   * @param maker_side Side of resting orders
   * @param on_execution Callback for executions
   * @return Remaining quantity
   */
  uint32_t match_at_level(PriceLevel &level, uint64_t taker_id, uint32_t qty,
                          Side maker_side,
                          ExecutionCallback on_execution) noexcept {
    uint32_t remaining = qty;

    // Match FIFO (front of list is oldest)
    while (remaining > 0 && !level.empty()) {
      Order &maker = level.orders.front();

      // Calculate fill quantity
      uint32_t fill_qty = std::min(remaining, maker.qty);

      // Generate execution report
      if (on_execution) {
        Execution exec{.maker_id = maker.id,
                       .taker_id = taker_id,
                       .price = level.price,
                       .qty = fill_qty,
                       .maker_side = maker_side};
        on_execution(exec);
      }

      // Update quantities
      remaining -= fill_qty;
      level.reduce_volume(fill_qty);
      maker.reduce_qty(fill_qty);

      // Remove filled order
      if (maker.is_filled()) {
        Order *filled_order = &maker;
        level.orders.pop_front();
        order_map_.erase(filled_order->id);
        pool_.deallocate(filled_order);
      }
    }

    return remaining;
  }

  // ========================================================================
  // Price Level Management
  // ========================================================================

  /**
   * @brief Add order to bid side (sorted descending).
   */
  void add_to_bids(Order *order) noexcept {
    // Find insertion point (descending order)
    auto it = std::lower_bound(bids_.begin(), bids_.end(), order->price,
                               [](const PriceLevel &level, uint64_t price) {
                                 return level.price > price; // Descending
                               });

    // Check if level exists at this price
    if (it != bids_.end() && it->price == order->price) {
      it->add_order(order);
    } else {
      // Insert new level
      PriceLevel new_level(order->price);
      new_level.add_order(order);
      bids_.insert(it, std::move(new_level));
    }
  }

  /**
   * @brief Add order to ask side (sorted ascending).
   */
  void add_to_asks(Order *order) noexcept {
    // Find insertion point (ascending order)
    auto it = std::lower_bound(asks_.begin(), asks_.end(), order->price,
                               [](const PriceLevel &level, uint64_t price) {
                                 return level.price < price; // Ascending
                               });

    // Check if level exists at this price
    if (it != asks_.end() && it->price == order->price) {
      it->add_order(order);
    } else {
      // Insert new level
      PriceLevel new_level(order->price);
      new_level.add_order(order);
      asks_.insert(it, std::move(new_level));
    }
  }

  /**
   * @brief Remove order from bid side.
   */
  void remove_from_bids(Order *order) noexcept {
    // Find price level
    for (auto it = bids_.begin(); it != bids_.end(); ++it) {
      if (it->price == order->price) {
        it->remove_order(order);
        if (it->empty()) {
          bids_.erase(it);
        }
        return;
      }
    }
  }

  /**
   * @brief Remove order from ask side.
   */
  void remove_from_asks(Order *order) noexcept {
    // Find price level
    for (auto it = asks_.begin(); it != asks_.end(); ++it) {
      if (it->price == order->price) {
        it->remove_order(order);
        if (it->empty()) {
          asks_.erase(it);
        }
        return;
      }
    }
  }
};

} // namespace book
