#pragma once

/**
 * @file memory_pool.hpp
 * @brief Pre-allocated object pool with O(1) allocate/deallocate for HFT.
 *
 * DESIGN PRINCIPLES:
 * 1. Zero runtime malloc - all memory allocated at construction.
 * 2. O(1) allocation/deallocation via index-based free list.
 * 3. Cache-friendly - objects stored contiguously in memory.
 * 4. No OS calls during trading - pure integer arithmetic.
 *
 * USAGE:
 *   MemPool<Order, 1'000'000> pool;  // Pre-allocate 1M orders
 *   Order* order = pool.allocate();  // O(1) - pop from free stack
 *   pool.deallocate(order);          // O(1) - push to free stack
 */

#include <cstddef>
#include <type_traits>
#include <vector>

namespace book {

// ============================================================================
// MemPool - Pre-allocated Object Pool
// ============================================================================

/**
 * @brief Pre-allocated memory pool with O(1) allocate/deallocate.
 *
 * This pool allocates a contiguous block of memory at construction time
 * and manages object recycling through an index-based free list (stack).
 *
 * Key properties:
 * - Single allocation at startup (no malloc during trading)
 * - O(1) allocate: pop index from free stack
 * - O(1) deallocate: push index to free stack
 * - Cache-friendly: objects are stored contiguously
 * - No fragmentation: fixed-size objects in fixed locations
 *
 * @tparam T Object type (must be default constructible)
 * @tparam Capacity Maximum number of objects the pool can hold
 *
 * @note Objects are NOT constructed on allocate() or destroyed on deallocate().
 *       The caller is responsible for placement new and explicit destructor
 *       calls if needed. For POD types like Order, this is not necessary.
 *
 * @example
 *   MemPool<Order, 1'000'000> pool;
 *
 *   // Allocate (no construction - just returns raw memory)
 *   Order* order = pool.allocate();
 *   order->id = 12345;
 *   order->qty = 100;
 *
 *   // Deallocate (no destruction - just marks slot as free)
 *   pool.deallocate(order);
 */
template <typename T, std::size_t Capacity>
  requires std::is_default_constructible_v<T>
class MemPool {
public:
  // ========================================================================
  // Types
  // ========================================================================

  using value_type = T;
  using size_type = std::size_t;
  using pointer = T *;
  using const_pointer = const T *;

  // ========================================================================
  // Construction
  // ========================================================================

  /**
   * @brief Construct pool with pre-allocated storage.
   *
   * Allocates `Capacity` objects and initializes free list with all indices.
   * This is the ONLY allocation that happens - nothing during trading.
   *
   * @note This may throw std::bad_alloc if allocation fails.
   */
  MemPool() : buffer_(Capacity), free_list_(Capacity) {
    // Initialize free list: [Capacity-1, Capacity-2, ..., 1, 0]
    // Stack order means index 0 will be allocated first (LIFO)
    for (size_type i = 0; i < Capacity; ++i) {
      free_list_[i] = Capacity - 1 - i;
    }
    free_count_ = Capacity;
  }

  // Non-copyable (owns unique memory)
  MemPool(const MemPool &) = delete;
  MemPool &operator=(const MemPool &) = delete;

  // Non-movable (addresses must remain stable)
  MemPool(MemPool &&) = delete;
  MemPool &operator=(MemPool &&) = delete;

  ~MemPool() = default;

  // ========================================================================
  // Capacity
  // ========================================================================

  /**
   * @brief Maximum number of objects the pool can hold.
   */
  [[nodiscard]] static constexpr size_type capacity() noexcept {
    return Capacity;
  }

  /**
   * @brief Number of currently allocated objects.
   */
  [[nodiscard]] size_type allocated() const noexcept {
    return Capacity - free_count_;
  }

  /**
   * @brief Number of free slots available.
   */
  [[nodiscard]] size_type available() const noexcept { return free_count_; }

  /**
   * @brief Check if pool is empty (all slots free).
   */
  [[nodiscard]] bool empty() const noexcept { return free_count_ == Capacity; }

  /**
   * @brief Check if pool is full (no slots free).
   */
  [[nodiscard]] bool full() const noexcept { return free_count_ == 0; }

  // ========================================================================
  // Allocation
  // ========================================================================

  /**
   * @brief Allocate an object from the pool.
   *
   * @return Pointer to uninitialized object, or nullptr if pool is full.
   *
   * Complexity: O(1) - just pops an index from the free stack.
   *
   * @note The returned memory is NOT constructed. For non-POD types,
   *       use placement new: `new (ptr) T(args...)`.
   *
   * @code
   *   Order* order = pool.allocate();
   *   if (order) {
   *       order->id = 12345;
   *       // ... use order
   *   }
   * @endcode
   */
  [[nodiscard]] pointer allocate() noexcept {
    if (free_count_ == 0) [[unlikely]] {
      return nullptr;
    }

    // Pop index from free stack
    --free_count_;
    const size_type index = free_list_[free_count_];

    return &buffer_[index];
  }

  /**
   * @brief Deallocate an object back to the pool.
   *
   * @param ptr Pointer to object previously obtained from allocate().
   *            Must not be nullptr. Must be from this pool.
   *
   * Complexity: O(1) - just pushes index to free stack.
   *
   * @note The object is NOT destroyed. For non-POD types, explicitly
   *       call the destructor first: `ptr->~T()`.
   *
   * @warning Deallocating a pointer not from this pool is undefined behavior.
   * @warning Double-deallocation is undefined behavior.
   */
  void deallocate(pointer ptr) noexcept {
    // Calculate index from pointer
    const size_type index = static_cast<size_type>(ptr - buffer_.data());

    // Push index to free stack
    free_list_[free_count_] = index;
    ++free_count_;
  }

  // ========================================================================
  // Validation (Debug)
  // ========================================================================

  /**
   * @brief Check if pointer belongs to this pool.
   *
   * Useful for debug assertions.
   */
  [[nodiscard]] bool owns(const_pointer ptr) const noexcept {
    return ptr >= buffer_.data() && ptr < buffer_.data() + Capacity;
  }

  /**
   * @brief Get pointer to underlying storage.
   *
   * Useful for cache analysis and debugging.
   */
  [[nodiscard]] pointer data() noexcept { return buffer_.data(); }

  [[nodiscard]] const_pointer data() const noexcept { return buffer_.data(); }

private:
  // Contiguous storage for all objects
  std::vector<T> buffer_;

  // Free list as stack of indices
  // free_list_[0..free_count_-1] contain available indices
  std::vector<size_type> free_list_;

  // Number of free slots (also serves as stack top index)
  size_type free_count_ = 0;
};

// ============================================================================
// Compile-time verification
// ============================================================================

// Verify pool is non-copyable and non-movable
static_assert(!std::is_copy_constructible_v<MemPool<int, 10>>);
static_assert(!std::is_copy_assignable_v<MemPool<int, 10>>);
static_assert(!std::is_move_constructible_v<MemPool<int, 10>>);
static_assert(!std::is_move_assignable_v<MemPool<int, 10>>);

} // namespace book
