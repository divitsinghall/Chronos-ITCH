#pragma once

/**
 * @file intrusive_list.hpp
 * @brief Zero-allocation intrusive doubly-linked list for HFT order management.
 *
 * DESIGN PRINCIPLES:
 * 1. Objects ARE the nodes - no separate node allocation.
 * 2. O(1) removal from middle of list (critical for order cancellation).
 * 3. Cache-friendly when combined with MemPool (contiguous storage).
 * 4. C++20 concepts for type-safe template constraints.
 *
 * USAGE:
 *   struct Order : IntrusiveNode { ... };
 *   IntrusiveList<Order> orders;
 *   orders.push_back(&order);
 *   orders.remove(&order);  // O(1) removal!
 */

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace book {

// ============================================================================
// IntrusiveNode - Base class for list elements
// ============================================================================

/**
 * @brief Base node for intrusive doubly-linked list.
 *
 * Objects that want to be stored in an IntrusiveList must inherit from this.
 * The pointers are mutable to allow modification even for const iterators
 * (the node linkage is an implementation detail, not part of the object's
 * logical const-ness).
 *
 * @note Both pointers are initialized to nullptr to indicate "not in list".
 */
struct IntrusiveNode {
  IntrusiveNode *prev = nullptr;
  IntrusiveNode *next = nullptr;

  /**
   * @brief Check if this node is currently linked in a list.
   */
  [[nodiscard]] constexpr bool is_linked() const noexcept {
    return prev != nullptr || next != nullptr;
  }

  /**
   * @brief Unlink this node (reset pointers).
   *
   * Called after removal from list to mark as unlinked.
   */
  constexpr void unlink() noexcept {
    prev = nullptr;
    next = nullptr;
  }

  // Default constructible and destructible
  IntrusiveNode() = default;
  ~IntrusiveNode() = default;

  // Non-copyable to prevent accidental slicing
  IntrusiveNode(const IntrusiveNode &) = delete;
  IntrusiveNode &operator=(const IntrusiveNode &) = delete;

  // Movable (but move doesn't transfer linkage - moved-to node starts unlinked)
  // This is critical: if we copied prev/next, they would point to the OLD
  // neighbors which may be in deallocated memory after a vector resize
  IntrusiveNode(IntrusiveNode &&) noexcept : prev(nullptr), next(nullptr) {}
  IntrusiveNode &operator=(IntrusiveNode &&) noexcept {
    // Don't copy prev/next - that would create dangling pointers
    // Just leave this node's linkage unchanged
    return *this;
  }
};

// ============================================================================
// C++20 Concept for IntrusiveList elements
// ============================================================================

/**
 * @brief Concept requiring type T to derive from IntrusiveNode.
 *
 * This ensures compile-time type safety - only types that inherit
 * from IntrusiveNode can be stored in IntrusiveList.
 */
template <typename T>
concept IntrusiveListElement =
    std::is_base_of_v<IntrusiveNode, T> && std::is_class_v<T>;

// ============================================================================
// IntrusiveList - O(1) insertion/removal doubly-linked list
// ============================================================================

/**
 * @brief Intrusive doubly-linked list with O(1) operations.
 *
 * Unlike std::list, this list doesn't allocate nodes. Elements must
 * inherit from IntrusiveNode, and the list manipulates those embedded
 * pointers directly.
 *
 * @tparam T Element type (must satisfy IntrusiveListElement concept)
 *
 * Key properties:
 * - O(1) push_front, push_back, remove
 * - O(n) size (by design - we optimize for insertion/removal)
 * - No memory allocation (elements provide their own node storage)
 * - Cache-friendly iteration (especially with MemPool)
 *
 * @example
 *   struct Order : IntrusiveNode {
 *       uint64_t id;
 *       uint32_t qty;
 *   };
 *
 *   IntrusiveList<Order> orders;
 *   Order o1{.id = 1, .qty = 100};
 *   orders.push_back(&o1);
 *
 *   for (auto& order : orders) {
 *       process(order);
 *   }
 */
template <IntrusiveListElement T> class IntrusiveList {
public:
  // ========================================================================
  // Iterator
  // ========================================================================

  /**
   * @brief Bidirectional iterator for IntrusiveList.
   */
  template <bool IsConst> class Iterator {
  public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::conditional_t<IsConst, const T, T>;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    Iterator() noexcept : node_(nullptr) {}
    explicit Iterator(IntrusiveNode *node) noexcept : node_(node) {}

    // Allow conversion from non-const to const iterator
    template <bool Other>
      requires(!IsConst && Other)
    Iterator(const Iterator<Other> &other) noexcept : node_(other.node_) {}

    [[nodiscard]] reference operator*() const noexcept {
      return *static_cast<pointer>(node_);
    }

    [[nodiscard]] pointer operator->() const noexcept {
      return static_cast<pointer>(node_);
    }

    Iterator &operator++() noexcept {
      node_ = node_->next;
      return *this;
    }

    Iterator operator++(int) noexcept {
      Iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    Iterator &operator--() noexcept {
      node_ = node_->prev;
      return *this;
    }

    Iterator operator--(int) noexcept {
      Iterator tmp = *this;
      --(*this);
      return tmp;
    }

    [[nodiscard]] bool operator==(const Iterator &other) const noexcept {
      return node_ == other.node_;
    }

    [[nodiscard]] bool operator!=(const Iterator &other) const noexcept {
      return node_ != other.node_;
    }

  private:
    template <bool> friend class Iterator;
    IntrusiveNode *node_;
  };

  using iterator = Iterator<false>;
  using const_iterator = Iterator<true>;

  // ========================================================================
  // Construction / Destruction
  // ========================================================================

  /**
   * @brief Construct empty list with sentinel node.
   *
   * The sentinel is a dummy node that simplifies edge cases:
   * - head_.next points to first element (or &head_ if empty)
   * - head_.prev points to last element (or &head_ if empty)
   */
  IntrusiveList() noexcept {
    head_.next = &head_;
    head_.prev = &head_;
  }

  // Non-copyable (elements may be in only one list)
  IntrusiveList(const IntrusiveList &) = delete;
  IntrusiveList &operator=(const IntrusiveList &) = delete;

  // Movable
  IntrusiveList(IntrusiveList &&other) noexcept {
    head_.next = &head_;
    head_.prev = &head_;
    splice_all(other);
  }

  IntrusiveList &operator=(IntrusiveList &&other) noexcept {
    if (this != &other) {
      clear();
      splice_all(other);
    }
    return *this;
  }

  ~IntrusiveList() { clear(); }

  // ========================================================================
  // Capacity
  // ========================================================================

  /**
   * @brief Check if list is empty.
   */
  [[nodiscard]] constexpr bool empty() const noexcept {
    return head_.next == &head_;
  }

  /**
   * @brief Count elements in list (O(n)).
   *
   * @note Deliberately not cached to optimize for insertion/removal.
   */
  [[nodiscard]] std::size_t size() const noexcept {
    std::size_t count = 0;
    for (auto it = begin(); it != end(); ++it) {
      ++count;
    }
    return count;
  }

  // ========================================================================
  // Element Access
  // ========================================================================

  /**
   * @brief Get reference to first element.
   *
   * @pre !empty()
   */
  [[nodiscard]] T &front() noexcept { return *static_cast<T *>(head_.next); }

  [[nodiscard]] const T &front() const noexcept {
    return *static_cast<const T *>(head_.next);
  }

  /**
   * @brief Get reference to last element.
   *
   * @pre !empty()
   */
  [[nodiscard]] T &back() noexcept { return *static_cast<T *>(head_.prev); }

  [[nodiscard]] const T &back() const noexcept {
    return *static_cast<const T *>(head_.prev);
  }

  // ========================================================================
  // Iterators
  // ========================================================================

  [[nodiscard]] iterator begin() noexcept { return iterator(head_.next); }

  [[nodiscard]] const_iterator begin() const noexcept {
    return const_iterator(const_cast<IntrusiveNode *>(head_.next));
  }

  [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }

  [[nodiscard]] iterator end() noexcept { return iterator(&head_); }

  [[nodiscard]] const_iterator end() const noexcept {
    return const_iterator(const_cast<IntrusiveNode *>(&head_));
  }

  [[nodiscard]] const_iterator cend() const noexcept { return end(); }

  // ========================================================================
  // Modifiers
  // ========================================================================

  /**
   * @brief Add element to front of list.
   *
   * @param elem Pointer to element (must not be nullptr, must not be linked)
   *
   * Complexity: O(1)
   */
  void push_front(T *elem) noexcept { insert_after(&head_, elem); }

  /**
   * @brief Add element to back of list.
   *
   * @param elem Pointer to element (must not be nullptr, must not be linked)
   *
   * Complexity: O(1)
   */
  void push_back(T *elem) noexcept { insert_before(&head_, elem); }

  /**
   * @brief Remove element from front of list.
   *
   * @pre !empty()
   *
   * Complexity: O(1)
   */
  void pop_front() noexcept { remove(static_cast<T *>(head_.next)); }

  /**
   * @brief Remove element from back of list.
   *
   * @pre !empty()
   *
   * Complexity: O(1)
   */
  void pop_back() noexcept { remove(static_cast<T *>(head_.prev)); }

  /**
   * @brief Remove specific element from list.
   *
   * @param elem Pointer to element to remove (must be in this list)
   *
   * Complexity: O(1) - this is the key advantage of intrusive lists!
   *
   * @note No search required - element knows its own position via
   *       its inherited prev/next pointers.
   */
  void remove(T *elem) noexcept {
    IntrusiveNode *node = static_cast<IntrusiveNode *>(elem);

    // Unlink from neighbors
    node->prev->next = node->next;
    node->next->prev = node->prev;

    // Mark as unlinked
    node->unlink();
  }

  /**
   * @brief Remove all elements from list.
   *
   * Complexity: O(n) - must unlink each element
   */
  void clear() noexcept {
    while (!empty()) {
      pop_front();
    }
  }

  /**
   * @brief Insert element before position.
   *
   * @param pos Iterator position
   * @param elem Element to insert
   * @return Iterator to inserted element
   *
   * Complexity: O(1)
   */
  iterator insert(iterator pos, T *elem) noexcept {
    insert_before(pos.node_, elem);
    return iterator(elem);
  }

  /**
   * @brief Erase element at position.
   *
   * @param pos Iterator to element to erase
   * @return Iterator to element following erased element
   *
   * Complexity: O(1)
   */
  iterator erase(iterator pos) noexcept {
    iterator next(pos.node_->next);
    remove(&*pos);
    return next;
  }

private:
  // Sentinel node (not a real element)
  // head_.next = first element, head_.prev = last element
  mutable IntrusiveNode head_;

  /**
   * @brief Insert node after specified position.
   */
  void insert_after(IntrusiveNode *pos, T *elem) noexcept {
    IntrusiveNode *node = static_cast<IntrusiveNode *>(elem);

    node->prev = pos;
    node->next = pos->next;
    pos->next->prev = node;
    pos->next = node;
  }

  /**
   * @brief Insert node before specified position.
   */
  void insert_before(IntrusiveNode *pos, T *elem) noexcept {
    IntrusiveNode *node = static_cast<IntrusiveNode *>(elem);

    node->next = pos;
    node->prev = pos->prev;
    pos->prev->next = node;
    pos->prev = node;
  }

  /**
   * @brief Move all elements from other list to this list.
   */
  void splice_all(IntrusiveList &other) noexcept {
    if (other.empty()) {
      return;
    }

    // Link other's elements into this list
    other.head_.next->prev = head_.prev;
    head_.prev->next = other.head_.next;
    other.head_.prev->next = &head_;
    head_.prev = other.head_.prev;

    // Reset other to empty
    other.head_.next = &other.head_;
    other.head_.prev = &other.head_;
  }
};

} // namespace book
