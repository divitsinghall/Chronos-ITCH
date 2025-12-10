#pragma once

/**
 * @file types.hpp
 * @brief Core order book types with minimal memory footprint.
 *
 * DESIGN PRINCIPLES:
 * 1. Packed structures to minimize cache misses.
 * 2. Intrusive node inheritance for O(1) list operations.
 * 3. Fixed-point pricing to avoid floating-point latency.
 * 4. POD-like for trivial construction/destruction.
 */

#include "intrusive_list.hpp"
#include <cstdint>

namespace book {

// ============================================================================
// Side Enum
// ============================================================================

/**
 * @brief Order side indicator.
 */
enum class Side : char { Buy = 'B', Sell = 'S' };

// ============================================================================
// Order - Core order structure
// ============================================================================

/**
 * @brief Limit order in the order book.
 *
 * This struct inherits from IntrusiveNode to enable O(1) removal from
 * price level lists. The #pragma pack(1) directive removes all padding
 * to minimize memory footprint and improve cache efficiency.
 *
 * Memory Layout (packed):
 *   IntrusiveNode: 16 bytes (prev + next pointers)
 *   id:             8 bytes
 *   price:          8 bytes
 *   qty:            4 bytes
 *   side:           1 byte
 *   -----------------------
 *   Total:         37 bytes (vs 40+ with padding)
 *
 * @note Using #pragma pack(1) may cause unaligned access on some architectures.
 *       On x86-64 this is generally fine, but may have performance implications
 *       on ARM or other architectures.
 */
#pragma pack(push, 1)
struct Order : IntrusiveNode {
  uint64_t id;    ///< Unique order identifier
  uint64_t price; ///< Price in ticks (fixed-point, e.g., price * 10000)
  uint32_t qty;   ///< Remaining quantity (shares)
  char side;      ///< 'B' = Buy, 'S' = Sell

  // ========================================================================
  // Constructors
  // ========================================================================

  /**
   * @brief Default constructor for pool pre-allocation.
   */
  Order() noexcept = default;

  /**
   * @brief Construct order with all fields.
   */
  Order(uint64_t order_id, uint64_t order_price, uint32_t order_qty,
        char order_side) noexcept
      : id(order_id), price(order_price), qty(order_qty), side(order_side) {}

  /**
   * @brief Construct order with Side enum.
   */
  Order(uint64_t order_id, uint64_t order_price, uint32_t order_qty,
        Side order_side) noexcept
      : id(order_id), price(order_price), qty(order_qty),
        side(static_cast<char>(order_side)) {}

  // ========================================================================
  // Accessors
  // ========================================================================

  [[nodiscard]] constexpr bool is_buy() const noexcept { return side == 'B'; }

  [[nodiscard]] constexpr bool is_sell() const noexcept { return side == 'S'; }

  [[nodiscard]] constexpr Side get_side() const noexcept {
    return static_cast<Side>(side);
  }

  /**
   * @brief Convert fixed-point price to double.
   *
   * Assumes price is stored as price * 10000 (4 decimal places).
   */
  [[nodiscard]] double price_as_double() const noexcept {
    return static_cast<double>(price) / 10000.0;
  }

  // ========================================================================
  // Modifiers
  // ========================================================================

  /**
   * @brief Reduce quantity (partial fill).
   *
   * @param fill_qty Quantity to remove
   * @return Remaining quantity after fill
   */
  uint32_t reduce_qty(uint32_t fill_qty) noexcept {
    if (fill_qty >= qty) {
      qty = 0;
    } else {
      qty -= fill_qty;
    }
    return qty;
  }

  /**
   * @brief Check if order is fully filled.
   */
  [[nodiscard]] constexpr bool is_filled() const noexcept { return qty == 0; }
};
#pragma pack(pop)

// ============================================================================
// Compile-time Verification
// ============================================================================

// Verify Order inherits from IntrusiveNode (required for IntrusiveList)
static_assert(std::is_base_of_v<IntrusiveNode, Order>,
              "Order must inherit from IntrusiveNode");

// Verify Order satisfies IntrusiveListElement concept
static_assert(IntrusiveListElement<Order>,
              "Order must satisfy IntrusiveListElement concept");

// Verify packed size (pointers are 8 bytes on 64-bit)
// IntrusiveNode: 16 bytes, Order fields: 21 bytes, Total: 37 bytes
static_assert(sizeof(Order) == 37, "Order should be 37 bytes when packed");

// Note: offsetof cannot be used on Order because it inherits from
// IntrusiveNode, making it a non-standard-layout type. The layout is verified
// at runtime in tests.

// ============================================================================
// Type Aliases for Order Book
// ============================================================================

/**
 * @brief List of orders at a single price level.
 *
 * Orders are stored in FIFO order (time priority).
 */
using OrderList = IntrusiveList<Order>;

/**
 * @brief Price type for order book (in ticks).
 */
using Price = uint64_t;

/**
 * @brief Quantity type.
 */
using Quantity = uint32_t;

/**
 * @brief Order ID type.
 */
using OrderId = uint64_t;

} // namespace book
