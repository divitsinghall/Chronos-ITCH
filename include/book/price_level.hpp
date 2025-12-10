#pragma once

/**
 * @file price_level.hpp
 * @brief Price level aggregation for order book.
 *
 * DESIGN PRINCIPLES:
 * 1. Aggregate orders at same price for cache-friendly iteration.
 * 2. Use IntrusiveList for O(1) order insertion/removal.
 * 3. Cache total volume to avoid O(n) iteration for market data.
 * 4. Minimal overhead - no virtual functions.
 */

#include "intrusive_list.hpp"
#include "types.hpp"
#include <cstdint>

namespace book {

// ============================================================================
// PriceLevel - Aggregation of orders at a single price
// ============================================================================

/**
 * @brief Represents a single price level in the order book.
 *
 * Contains all orders resting at a specific price, maintained in FIFO order
 * (time priority). The total_volume is cached for efficient market data
 * dissemination.
 *
 * Key properties:
 * - Orders are stored in time priority (oldest first via IntrusiveList)
 * - O(1) order insertion (push_back) and removal
 * - Cached total_volume for fast aggregate queries
 *
 * @note PriceLevel is move-only because IntrusiveList is move-only.
 */
struct PriceLevel {
  uint64_t price = 0;          ///< Price in ticks (fixed-point, e.g., * 10000)
  IntrusiveList<Order> orders; ///< FIFO queue of orders at this price
  uint64_t total_volume = 0;   ///< Cached aggregate quantity

  // ========================================================================
  // Constructors
  // ========================================================================

  /**
   * @brief Default constructor for empty price level.
   */
  PriceLevel() = default;

  /**
   * @brief Construct price level with specific price.
   */
  explicit PriceLevel(uint64_t level_price) noexcept
      : price(level_price), orders(), total_volume(0) {}

  // Non-copyable (IntrusiveList is non-copyable)
  PriceLevel(const PriceLevel &) = delete;
  PriceLevel &operator=(const PriceLevel &) = delete;

  // Movable
  PriceLevel(PriceLevel &&other) noexcept
      : price(other.price), orders(std::move(other.orders)),
        total_volume(other.total_volume) {
    other.price = 0;
    other.total_volume = 0;
  }

  PriceLevel &operator=(PriceLevel &&other) noexcept {
    if (this != &other) {
      price = other.price;
      orders = std::move(other.orders);
      total_volume = other.total_volume;
      other.price = 0;
      other.total_volume = 0;
    }
    return *this;
  }

  // ========================================================================
  // Accessors
  // ========================================================================

  /**
   * @brief Check if price level has no orders.
   */
  [[nodiscard]] bool empty() const noexcept { return orders.empty(); }

  /**
   * @brief Get number of orders at this level (O(n)).
   */
  [[nodiscard]] std::size_t order_count() const noexcept {
    return orders.size();
  }

  /**
   * @brief Convert fixed-point price to double.
   */
  [[nodiscard]] double price_as_double() const noexcept {
    return static_cast<double>(price) / 10000.0;
  }

  // ========================================================================
  // Modifiers
  // ========================================================================

  /**
   * @brief Add order to back of queue (time priority).
   *
   * @param order Pointer to order to add
   */
  void add_order(Order *order) noexcept {
    orders.push_back(order);
    total_volume += order->qty;
  }

  /**
   * @brief Remove order from this level.
   *
   * @param order Pointer to order to remove
   */
  void remove_order(Order *order) noexcept {
    if (order->qty <= total_volume) {
      total_volume -= order->qty;
    } else {
      total_volume = 0;
    }
    orders.remove(order);
  }

  /**
   * @brief Update cached volume after partial fill.
   *
   * @param filled_qty Quantity that was filled
   */
  void reduce_volume(uint32_t filled_qty) noexcept {
    if (filled_qty <= total_volume) {
      total_volume -= filled_qty;
    } else {
      total_volume = 0;
    }
  }
};

} // namespace book
