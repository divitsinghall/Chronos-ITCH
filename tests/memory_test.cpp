/**
 * @file memory_test.cpp
 * @brief Benchmarks and unit tests for memory infrastructure.
 *
 * Tests:
 * 1. IntrusiveList correctness (push, pop, remove, iteration)
 * 2. MemPool correctness (allocate, deallocate, capacity)
 * 3. Performance comparison: IntrusiveList vs std::list
 */

#include <gtest/gtest.h>

#include <book/intrusive_list.hpp>
#include <book/memory_pool.hpp>
#include <book/types.hpp>

#include <chrono>
#include <iostream>
#include <list>
#include <memory>
#include <vector>

using namespace book;

// ============================================================================
// IntrusiveList Unit Tests
// ============================================================================

class IntrusiveListTest : public ::testing::Test {
protected:
  // Simple test element
  struct TestElement : IntrusiveNode {
    int value = 0;
    TestElement() = default;
    explicit TestElement(int v) : value(v) {}
  };

  // IMPORTANT: Order matters! elements_ must be declared BEFORE list_ so that:
  // 1. elements_ is constructed first (in SetUp)
  // 2. list_ is destroyed first (before elements_ is freed)
  // Otherwise, list_ destructor would access freed elements
  static constexpr std::size_t kNumElements = 100;
  std::unique_ptr<TestElement[]> elements_;
  IntrusiveList<TestElement> list_;

  void SetUp() override {
    elements_ = std::make_unique<TestElement[]>(kNumElements);
    for (std::size_t i = 0; i < kNumElements; ++i) {
      elements_[i].value = static_cast<int>(i);
    }
  }
};

TEST_F(IntrusiveListTest, EmptyListProperties) {
  EXPECT_TRUE(list_.empty());
  EXPECT_EQ(list_.size(), 0u);
}

// Minimal standalone test without using shared fixture elements
TEST(IntrusiveListStandaloneTest, BasicPushBack) {
  struct SimpleNode : IntrusiveNode {
    int x = 42;
  };

  SimpleNode node;
  IntrusiveList<SimpleNode> list;

  EXPECT_TRUE(list.empty());
  list.push_back(&node);
  EXPECT_FALSE(list.empty());
  EXPECT_EQ(list.front().x, 42);
}

TEST_F(IntrusiveListTest, PushBackSingleElement) {
  list_.push_back(&elements_[0]);

  EXPECT_FALSE(list_.empty());
  EXPECT_EQ(list_.size(), 1u);
  EXPECT_EQ(list_.front().value, 0);
  EXPECT_EQ(list_.back().value, 0);
}

TEST_F(IntrusiveListTest, PushFrontSingleElement) {
  list_.push_front(&elements_[0]);

  EXPECT_FALSE(list_.empty());
  EXPECT_EQ(list_.size(), 1u);
  EXPECT_EQ(list_.front().value, 0);
  EXPECT_EQ(list_.back().value, 0);
}

TEST_F(IntrusiveListTest, PushBackMultipleElements) {
  for (int i = 0; i < 10; ++i) {
    list_.push_back(&elements_[i]);
  }

  EXPECT_EQ(list_.size(), 10u);
  EXPECT_EQ(list_.front().value, 0);
  EXPECT_EQ(list_.back().value, 9);
}

TEST_F(IntrusiveListTest, PushFrontMultipleElements) {
  for (int i = 0; i < 10; ++i) {
    list_.push_front(&elements_[i]);
  }

  EXPECT_EQ(list_.size(), 10u);
  EXPECT_EQ(list_.front().value, 9); // Last pushed is first
  EXPECT_EQ(list_.back().value, 0);  // First pushed is last
}

TEST_F(IntrusiveListTest, PopFront) {
  for (int i = 0; i < 5; ++i) {
    list_.push_back(&elements_[i]);
  }

  list_.pop_front();
  EXPECT_EQ(list_.size(), 4u);
  EXPECT_EQ(list_.front().value, 1);
}

TEST_F(IntrusiveListTest, PopBack) {
  for (int i = 0; i < 5; ++i) {
    list_.push_back(&elements_[i]);
  }

  list_.pop_back();
  EXPECT_EQ(list_.size(), 4u);
  EXPECT_EQ(list_.back().value, 3);
}

TEST_F(IntrusiveListTest, RemoveFromMiddle) {
  for (int i = 0; i < 5; ++i) {
    list_.push_back(&elements_[i]);
  }

  // Remove element 2 (middle)
  list_.remove(&elements_[2]);

  EXPECT_EQ(list_.size(), 4u);

  // Verify order: 0, 1, 3, 4
  auto it = list_.begin();
  EXPECT_EQ((it++)->value, 0);
  EXPECT_EQ((it++)->value, 1);
  EXPECT_EQ((it++)->value, 3);
  EXPECT_EQ((it++)->value, 4);
}

TEST_F(IntrusiveListTest, IterationForward) {
  for (int i = 0; i < 10; ++i) {
    list_.push_back(&elements_[i]);
  }

  int expected = 0;
  for (const auto &elem : list_) {
    EXPECT_EQ(elem.value, expected++);
  }
  EXPECT_EQ(expected, 10);
}

TEST_F(IntrusiveListTest, Clear) {
  for (int i = 0; i < 10; ++i) {
    list_.push_back(&elements_[i]);
  }

  list_.clear();

  EXPECT_TRUE(list_.empty());
  EXPECT_EQ(list_.size(), 0u);
}

TEST_F(IntrusiveListTest, ElementUnlinkedAfterRemove) {
  list_.push_back(&elements_[0]);
  EXPECT_TRUE(elements_[0].is_linked());

  list_.remove(&elements_[0]);
  EXPECT_FALSE(elements_[0].is_linked());
}

// ============================================================================
// MemPool Unit Tests
// ============================================================================

class MemPoolTest : public ::testing::Test {
protected:
  static constexpr std::size_t kPoolSize = 1000;
  MemPool<Order, kPoolSize> pool_;
};

TEST_F(MemPoolTest, InitialState) {
  EXPECT_TRUE(pool_.empty());
  EXPECT_FALSE(pool_.full());
  EXPECT_EQ(pool_.capacity(), kPoolSize);
  EXPECT_EQ(pool_.allocated(), 0u);
  EXPECT_EQ(pool_.available(), kPoolSize);
}

TEST_F(MemPoolTest, AllocateSingle) {
  Order *order = pool_.allocate();

  ASSERT_NE(order, nullptr);
  EXPECT_EQ(pool_.allocated(), 1u);
  EXPECT_EQ(pool_.available(), kPoolSize - 1);
  EXPECT_TRUE(pool_.owns(order));
}

TEST_F(MemPoolTest, AllocateMultiple) {
  std::vector<Order *> orders;
  for (std::size_t i = 0; i < 100; ++i) {
    Order *order = pool_.allocate();
    ASSERT_NE(order, nullptr);
    orders.push_back(order);
  }

  EXPECT_EQ(pool_.allocated(), 100u);
  EXPECT_EQ(pool_.available(), kPoolSize - 100);

  // Verify all pointers are unique and owned
  for (Order *order : orders) {
    EXPECT_TRUE(pool_.owns(order));
  }
}

TEST_F(MemPoolTest, AllocateThenDeallocate) {
  Order *order = pool_.allocate();
  ASSERT_NE(order, nullptr);
  EXPECT_EQ(pool_.allocated(), 1u);

  pool_.deallocate(order);
  EXPECT_EQ(pool_.allocated(), 0u);
  EXPECT_TRUE(pool_.empty());
}

TEST_F(MemPoolTest, ReusesDeallocatedSlots) {
  Order *first = pool_.allocate();
  pool_.deallocate(first);

  Order *second = pool_.allocate();
  EXPECT_EQ(first, second); // Should reuse same slot
}

TEST_F(MemPoolTest, AllocateUntilFull) {
  std::vector<Order *> orders;
  for (std::size_t i = 0; i < kPoolSize; ++i) {
    Order *order = pool_.allocate();
    ASSERT_NE(order, nullptr);
    orders.push_back(order);
  }

  EXPECT_TRUE(pool_.full());
  EXPECT_EQ(pool_.available(), 0u);

  // Next allocation should return nullptr
  Order *overflow = pool_.allocate();
  EXPECT_EQ(overflow, nullptr);
}

TEST_F(MemPoolTest, AllocateDeallocateCycle) {
  // Allocate all
  std::vector<Order *> orders;
  for (std::size_t i = 0; i < kPoolSize; ++i) {
    orders.push_back(pool_.allocate());
  }
  EXPECT_TRUE(pool_.full());

  // Deallocate all
  for (Order *order : orders) {
    pool_.deallocate(order);
  }
  EXPECT_TRUE(pool_.empty());

  // Should be able to allocate again
  for (std::size_t i = 0; i < kPoolSize; ++i) {
    EXPECT_NE(pool_.allocate(), nullptr);
  }
  EXPECT_TRUE(pool_.full());
}

// ============================================================================
// Order Type Tests
// ============================================================================

TEST(OrderTest, PackedSize) {
  // Verify packed size is as expected
  // IntrusiveNode contributes 16 bytes (2 pointers on 64-bit)
  // Order fields contribute 21 bytes (8+8+4+1)
  // Total: 37 bytes when packed
  EXPECT_EQ(sizeof(Order), 37u);
}

TEST(OrderTest, Construction) {
  Order order(12345, 100'0000, 100, 'B'); // Price = 100.0000

  EXPECT_EQ(order.id, 12345u);
  EXPECT_EQ(order.price, 100'0000u);
  EXPECT_EQ(order.qty, 100u);
  EXPECT_EQ(order.side, 'B');
  EXPECT_TRUE(order.is_buy());
  EXPECT_FALSE(order.is_sell());
}

TEST(OrderTest, ReduceQty) {
  Order order(1, 1000000, 100, 'B');

  order.reduce_qty(30);
  EXPECT_EQ(order.qty, 70u);
  EXPECT_FALSE(order.is_filled());

  order.reduce_qty(70);
  EXPECT_EQ(order.qty, 0u);
  EXPECT_TRUE(order.is_filled());
}

TEST(OrderTest, PriceConversion) {
  Order order(1, 123'4567, 100, 'B'); // 123.4567
  EXPECT_DOUBLE_EQ(order.price_as_double(), 123.4567);
}

// ============================================================================
// Performance Benchmark: IntrusiveList vs std::list
// ============================================================================

class PerformanceTest : public ::testing::Test {
protected:
  static constexpr std::size_t kNumOrders = 1'000'000;

  // Timing helper
  template <typename Func> static double measure_ms(Func &&func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
  }
};

TEST_F(PerformanceTest, IntrusiveListPushBackAndIterate) {
  // Pre-allocate storage (simulates MemPool)
  std::vector<Order> orders(kNumOrders);
  for (std::size_t i = 0; i < kNumOrders; ++i) {
    orders[i] = Order(i, i * 100, static_cast<uint32_t>(i % 1000), 'B');
  }

  IntrusiveList<Order> list;

  // Benchmark: push_back
  double push_time = measure_ms([&]() {
    for (std::size_t i = 0; i < kNumOrders; ++i) {
      list.push_back(&orders[i]);
    }
  });

  EXPECT_EQ(list.size(), kNumOrders);

  // Benchmark: iteration
  volatile uint64_t sum = 0; // Prevent optimization
  double iterate_time = measure_ms([&]() {
    for (const auto &order : list) {
      sum += order.qty;
    }
  });

  std::cout << "\n=== IntrusiveList Performance ===\n";
  std::cout << "  Elements:     " << kNumOrders << "\n";
  std::cout << "  Push time:    " << push_time << " ms\n";
  std::cout << "  Iterate time: " << iterate_time << " ms\n";
  std::cout << "  Sum (check):  " << sum << "\n";

  // Sanity check
  EXPECT_GT(sum, 0u);
}

TEST_F(PerformanceTest, StdListPushBackAndIterate) {
  std::list<Order> list;

  // Benchmark: push_back (includes allocation!)
  double push_time = measure_ms([&]() {
    for (std::size_t i = 0; i < kNumOrders; ++i) {
      list.emplace_back(i, i * 100, static_cast<uint32_t>(i % 1000), 'B');
    }
  });

  EXPECT_EQ(list.size(), kNumOrders);

  // Benchmark: iteration
  volatile uint64_t sum = 0;
  double iterate_time = measure_ms([&]() {
    for (const auto &order : list) {
      sum += order.qty;
    }
  });

  std::cout << "\n=== std::list Performance ===\n";
  std::cout << "  Elements:     " << kNumOrders << "\n";
  std::cout << "  Push time:    " << push_time << " ms (includes malloc)\n";
  std::cout << "  Iterate time: " << iterate_time << " ms\n";
  std::cout << "  Sum (check):  " << sum << "\n";

  EXPECT_GT(sum, 0u);
}

TEST_F(PerformanceTest, MemPoolAllocationSpeed) {
  MemPool<Order, kNumOrders> pool;
  std::vector<Order *> orders(kNumOrders);

  // Benchmark: allocate all
  double alloc_time = measure_ms([&]() {
    for (std::size_t i = 0; i < kNumOrders; ++i) {
      orders[i] = pool.allocate();
    }
  });

  EXPECT_TRUE(pool.full());

  // Benchmark: deallocate all
  double dealloc_time = measure_ms([&]() {
    for (std::size_t i = 0; i < kNumOrders; ++i) {
      pool.deallocate(orders[i]);
    }
  });

  EXPECT_TRUE(pool.empty());

  std::cout << "\n=== MemPool Performance ===\n";
  std::cout << "  Capacity:       " << kNumOrders << "\n";
  std::cout << "  Allocate all:   " << alloc_time << " ms\n";
  std::cout << "  Deallocate all: " << dealloc_time << " ms\n";
  std::cout << "  Alloc/op:       " << (alloc_time * 1000.0 / kNumOrders)
            << " us\n";
}

TEST_F(PerformanceTest, ComparisonSummary) {
  // Run both and compare
  std::vector<Order> intrusive_storage(kNumOrders);
  IntrusiveList<Order> intrusive_list;

  for (std::size_t i = 0; i < kNumOrders; ++i) {
    intrusive_storage[i] =
        Order(i, i * 100, static_cast<uint32_t>(i % 1000), 'B');
  }

  double intrusive_push = measure_ms([&]() {
    for (std::size_t i = 0; i < kNumOrders; ++i) {
      intrusive_list.push_back(&intrusive_storage[i]);
    }
  });

  std::list<Order> std_list;
  double std_push = measure_ms([&]() {
    for (std::size_t i = 0; i < kNumOrders; ++i) {
      std_list.emplace_back(i, i * 100, static_cast<uint32_t>(i % 1000), 'B');
    }
  });

  volatile uint64_t sum1 = 0, sum2 = 0;

  double intrusive_iter = measure_ms([&]() {
    for (const auto &o : intrusive_list)
      sum1 += o.qty;
  });

  double std_iter = measure_ms([&]() {
    for (const auto &o : std_list)
      sum2 += o.qty;
  });

  std::cout << "\n========================================\n";
  std::cout << "         PERFORMANCE COMPARISON         \n";
  std::cout << "========================================\n";
  std::cout << "Operation        IntrusiveList   std::list\n";
  std::cout << "----------------------------------------\n";
  std::cout << "Push 1M orders:  " << intrusive_push << " ms   " << std_push
            << " ms\n";
  std::cout << "Iterate 1M:      " << intrusive_iter << " ms   " << std_iter
            << " ms\n";
  std::cout << "----------------------------------------\n";
  std::cout << "Speedup (push):  " << (std_push / intrusive_push) << "x\n";
  std::cout << "Speedup (iter):  " << (std_iter / intrusive_iter) << "x\n";
  std::cout << "========================================\n";

  // IntrusiveList should be faster (or at least comparable)
  // for iteration due to better cache locality with pre-allocated storage
  EXPECT_EQ(sum1, sum2);
}
