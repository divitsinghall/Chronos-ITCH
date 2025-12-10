/**
 * @file matching_test.cpp
 * @brief Comprehensive tests for OrderBook matching engine.
 *
 * Tests Price-Time Priority (FIFO at each price level), order matching,
 * partial fills, and order cancellation.
 */

#include "book/order_book.hpp"
#include <gtest/gtest.h>

using namespace book;

// ============================================================================
// Test Fixture
// ============================================================================

class MatchingTest : public ::testing::Test {
protected:
  static constexpr std::size_t POOL_CAPACITY = 1000;

  MemPool<Order, POOL_CAPACITY> pool_;
  OrderBook<POOL_CAPACITY> book_{pool_};

  void SetUp() override {
    // Reset state before each test
  }
};

// ============================================================================
// Scenario 1: Resting Orders (No Match)
// ============================================================================

TEST_F(MatchingTest, RestingOrders_NoMatch) {
  // Add Buy @ 100 (1.0000 in fixed-point)
  ASSERT_TRUE(book_.add_order(1, 1000000, 100, Side::Buy));

  // Add Sell @ 101 (1.0100 in fixed-point)
  ASSERT_TRUE(book_.add_order(2, 1010000, 50, Side::Sell));

  // Verify both orders are resting
  EXPECT_EQ(book_.order_count(), 2);

  // Verify best bid/ask
  ASSERT_TRUE(book_.best_bid().has_value());
  ASSERT_TRUE(book_.best_ask().has_value());
  EXPECT_EQ(book_.best_bid().value(), 1000000); // 100.0000
  EXPECT_EQ(book_.best_ask().value(), 1010000); // 101.0000

  // Verify spread is 1.00 (10000 ticks)
  ASSERT_TRUE(book_.spread().has_value());
  EXPECT_EQ(book_.spread().value(), 10000); // 1.0000 spread

  // Verify volumes
  EXPECT_EQ(book_.best_bid_volume(), 100);
  EXPECT_EQ(book_.best_ask_volume(), 50);
}

TEST_F(MatchingTest, RestingOrders_SameSide) {
  // Multiple buys at different prices
  ASSERT_TRUE(book_.add_order(1, 1000000, 100, Side::Buy)); // 100.0000
  ASSERT_TRUE(book_.add_order(2, 990000, 200, Side::Buy));  // 99.0000
  ASSERT_TRUE(book_.add_order(3, 1010000, 50, Side::Buy));  // 101.0000

  // Best bid should be highest price
  EXPECT_EQ(book_.best_bid().value(), 1010000);
  EXPECT_EQ(book_.bid_level_count(), 3);
  EXPECT_EQ(book_.order_count(), 3);
}

// ============================================================================
// Scenario 2: Crossing Orders (Matching)
// ============================================================================

TEST_F(MatchingTest, CrossingOrder_FullMatch) {
  // Add resting Buy @ 100
  ASSERT_TRUE(book_.add_order(1, 1000000, 100, Side::Buy));

  // Add crossing Sell @ 99 (should match at 100)
  ASSERT_TRUE(book_.add_order(2, 990000, 100, Side::Sell));

  // Both orders should be fully filled - book should be empty
  EXPECT_TRUE(book_.empty());
  EXPECT_EQ(book_.order_count(), 0);
  EXPECT_FALSE(book_.best_bid().has_value());
  EXPECT_FALSE(book_.best_ask().has_value());
}

TEST_F(MatchingTest, CrossingOrder_PartialFill_TakerRests) {
  // Add resting Buy @ 100 for 50 shares
  ASSERT_TRUE(book_.add_order(1, 1000000, 50, Side::Buy));

  // Add crossing Sell @ 99 for 100 shares
  // 50 should match, 50 should rest
  ASSERT_TRUE(book_.add_order(2, 990000, 100, Side::Sell));

  // Buy should be fully filled
  EXPECT_FALSE(book_.best_bid().has_value());

  // Remaining 50 shares should rest as ask
  ASSERT_TRUE(book_.best_ask().has_value());
  EXPECT_EQ(book_.best_ask().value(), 990000); // Rests at 99.0000
  EXPECT_EQ(book_.best_ask_volume(), 50);
  EXPECT_EQ(book_.order_count(), 1);
}

TEST_F(MatchingTest, CrossingOrder_PartialFill_MakerPartiallyFilled) {
  // Add resting Buy @ 100 for 100 shares
  ASSERT_TRUE(book_.add_order(1, 1000000, 100, Side::Buy));

  // Add crossing Sell @ 99 for 30 shares
  ASSERT_TRUE(book_.add_order(2, 990000, 30, Side::Sell));

  // Sell should be fully filled
  EXPECT_FALSE(book_.best_ask().has_value());

  // 70 shares should remain on bid
  ASSERT_TRUE(book_.best_bid().has_value());
  EXPECT_EQ(book_.best_bid().value(), 1000000);
  EXPECT_EQ(book_.best_bid_volume(), 70);
  EXPECT_EQ(book_.order_count(), 1);
}

TEST_F(MatchingTest, CrossingOrder_MultipleLevels) {
  // Build order book with multiple bid levels
  ASSERT_TRUE(book_.add_order(1, 1000000, 50, Side::Buy)); // 100.00 x 50
  ASSERT_TRUE(book_.add_order(2, 990000, 100, Side::Buy)); // 99.00 x 100
  ASSERT_TRUE(book_.add_order(3, 980000, 200, Side::Buy)); // 98.00 x 200

  EXPECT_EQ(book_.bid_level_count(), 3);

  // Sell 120 @ 98 - should sweep through 100 and 99 levels
  ASSERT_TRUE(book_.add_order(4, 980000, 120, Side::Sell));

  // Should have matched 50 @ 100.00, 70 @ 99.00
  // Remaining: 0 @ 100, 30 @ 99, 200 @ 98
  EXPECT_EQ(book_.bid_level_count(), 2);
  EXPECT_EQ(book_.best_bid().value(), 990000);
  EXPECT_EQ(book_.best_bid_volume(), 30);
}

// ============================================================================
// Scenario 3: Order Cancellation
// ============================================================================

TEST_F(MatchingTest, CancelOrder_SingleOrder) {
  // Add order
  ASSERT_TRUE(book_.add_order(1, 1000000, 100, Side::Buy));
  EXPECT_EQ(book_.order_count(), 1);

  // Cancel it
  ASSERT_TRUE(book_.cancel_order(1));

  // Book should be empty
  EXPECT_TRUE(book_.empty());
  EXPECT_EQ(book_.order_count(), 0);
  EXPECT_FALSE(book_.best_bid().has_value());
}

TEST_F(MatchingTest, CancelOrder_NonExistent) {
  // Try to cancel non-existent order
  EXPECT_FALSE(book_.cancel_order(999));
}

TEST_F(MatchingTest, CancelOrder_FromMiddle) {
  // Add multiple orders at same price level
  ASSERT_TRUE(book_.add_order(1, 1000000, 100, Side::Buy));
  ASSERT_TRUE(book_.add_order(2, 1000000, 200, Side::Buy));
  ASSERT_TRUE(book_.add_order(3, 1000000, 150, Side::Buy));

  EXPECT_EQ(book_.best_bid_volume(), 450);

  // Cancel middle order
  ASSERT_TRUE(book_.cancel_order(2));

  EXPECT_EQ(book_.order_count(), 2);
  EXPECT_EQ(book_.best_bid_volume(), 250); // 100 + 150
  EXPECT_EQ(book_.bid_level_count(), 1);
}

TEST_F(MatchingTest, CancelOrder_RemovesPriceLevel) {
  // Add orders at different prices
  ASSERT_TRUE(book_.add_order(1, 1000000, 100, Side::Buy)); // 100.00
  ASSERT_TRUE(book_.add_order(2, 990000, 100, Side::Buy));  // 99.00

  EXPECT_EQ(book_.bid_level_count(), 2);
  EXPECT_EQ(book_.best_bid().value(), 1000000);

  // Cancel best bid
  ASSERT_TRUE(book_.cancel_order(1));

  // Level should be removed, best bid changes
  EXPECT_EQ(book_.bid_level_count(), 1);
  EXPECT_EQ(book_.best_bid().value(), 990000);
}

// ============================================================================
// Scenario 4: Price-Time Priority (FIFO)
// ============================================================================

TEST_F(MatchingTest, FIFO_SamePriceLevel) {
  // Add multiple orders at same price (time priority)
  ASSERT_TRUE(book_.add_order(1, 1000000, 100, Side::Buy)); // First
  ASSERT_TRUE(book_.add_order(2, 1000000, 100, Side::Buy)); // Second
  ASSERT_TRUE(book_.add_order(3, 1000000, 100, Side::Buy)); // Third

  // Sell 150 shares - should fill order 1 fully, order 2 partially
  ASSERT_TRUE(book_.add_order(4, 990000, 150, Side::Sell));

  // Remaining: order 2 with 50, order 3 with 100
  EXPECT_EQ(book_.order_count(), 2);
  EXPECT_EQ(book_.best_bid_volume(), 150); // 50 + 100
}

TEST_F(MatchingTest, FIFO_VerifyOrderRemoval) {
  // Add orders at same price
  ASSERT_TRUE(book_.add_order(1, 1000000, 50, Side::Buy));
  ASSERT_TRUE(book_.add_order(2, 1000000, 50, Side::Buy));

  // Sell exactly 50 - should remove order 1 only
  ASSERT_TRUE(book_.add_order(3, 990000, 50, Side::Sell));

  // Order 1 should be gone, order 2 should remain
  EXPECT_FALSE(book_.cancel_order(1)); // Can't cancel - already filled
  EXPECT_TRUE(book_.cancel_order(2));  // Can cancel - still resting
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(MatchingTest, DuplicateOrderId_Rejected) {
  ASSERT_TRUE(book_.add_order(1, 1000000, 100, Side::Buy));
  EXPECT_FALSE(book_.add_order(1, 1010000, 50, Side::Sell)); // Same ID
  EXPECT_EQ(book_.order_count(), 1);
}

TEST_F(MatchingTest, SellSide_Matching) {
  // Build ask book
  ASSERT_TRUE(book_.add_order(1, 1010000, 100, Side::Sell)); // 101.00
  ASSERT_TRUE(book_.add_order(2, 1020000, 100, Side::Sell)); // 102.00

  EXPECT_EQ(book_.best_ask().value(), 1010000);

  // Aggressive buy sweeps through
  ASSERT_TRUE(book_.add_order(3, 1020000, 150, Side::Buy));

  // Should have matched 100 @ 101.00, 50 @ 102.00
  EXPECT_EQ(book_.order_count(), 1);
  EXPECT_EQ(book_.best_ask().value(), 1020000);
  EXPECT_EQ(book_.best_ask_volume(), 50);
}

TEST_F(MatchingTest, EmptyBook_Accessors) {
  EXPECT_TRUE(book_.empty());
  EXPECT_EQ(book_.order_count(), 0);
  EXPECT_FALSE(book_.best_bid().has_value());
  EXPECT_FALSE(book_.best_ask().has_value());
  EXPECT_FALSE(book_.spread().has_value());
  EXPECT_EQ(book_.best_bid_volume(), 0);
  EXPECT_EQ(book_.best_ask_volume(), 0);
}

TEST_F(MatchingTest, PoolExhaustion) {
  // Create a small pool to test exhaustion
  MemPool<Order, 2> small_pool;
  OrderBook<2> small_book(small_pool);

  ASSERT_TRUE(small_book.add_order(1, 1000000, 100, Side::Buy));
  ASSERT_TRUE(small_book.add_order(2, 1010000, 100, Side::Sell));

  // Pool is full, next add should fail
  EXPECT_FALSE(small_book.add_order(3, 990000, 50, Side::Buy));
}

// ============================================================================
// Volume Tracking
// ============================================================================

TEST_F(MatchingTest, VolumeTracking_AfterMatch) {
  ASSERT_TRUE(book_.add_order(1, 1000000, 100, Side::Buy));
  EXPECT_EQ(book_.best_bid_volume(), 100);

  // Partial fill
  ASSERT_TRUE(book_.add_order(2, 990000, 30, Side::Sell));
  EXPECT_EQ(book_.best_bid_volume(), 70);

  // Another partial fill
  ASSERT_TRUE(book_.add_order(3, 990000, 20, Side::Sell));
  EXPECT_EQ(book_.best_bid_volume(), 50);
}

TEST_F(MatchingTest, VolumeTracking_AfterCancel) {
  ASSERT_TRUE(book_.add_order(1, 1000000, 100, Side::Buy));
  ASSERT_TRUE(book_.add_order(2, 1000000, 200, Side::Buy));
  EXPECT_EQ(book_.best_bid_volume(), 300);

  ASSERT_TRUE(book_.cancel_order(1));
  EXPECT_EQ(book_.best_bid_volume(), 200);
}

// ============================================================================
// Main (if needed for standalone execution)
// ============================================================================

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
