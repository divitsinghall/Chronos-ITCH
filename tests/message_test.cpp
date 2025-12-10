/**
 * @file message_test.cpp
 * @brief Unit tests for ITCH 5.0 message parsing.
 *
 * Tests verify:
 * 1. BigEndian<T> correctly swaps bytes on access
 * 2. Struct sizes and offsets match ITCH 5.0 spec
 * 3. Zero-copy parsing works correctly with real message data
 */
#include <cstring>
#include <gtest/gtest.h>
#include <itch/messages.hpp>

namespace itch::test {

// ============================================================================
// BigEndian<T> Tests
// ============================================================================

TEST(BigEndianTest, U16_SwapsOnAccess) {
  // Create a buffer with big-endian 0x1234
  alignas(2) unsigned char buffer[2] = {0x12, 0x34};

  const auto *be_val = reinterpret_cast<const be_u16 *>(buffer);

  // On little-endian systems, accessing should swap to 0x3412...
  // but semantically we want host order, so the VALUE should be 0x1234
  uint16_t host_val = *be_val;

  // The byte swap converts network order (0x1234) to host order
  // On little-endian: raw bytes 0x12,0x34 represent 0x3412 natively
  // After ntoh swap: 0x1234 (the logical value)
  EXPECT_EQ(host_val, 0x1234);
}

TEST(BigEndianTest, U32_SwapsOnAccess) {
  alignas(4) unsigned char buffer[4] = {0x12, 0x34, 0x56, 0x78};

  const auto *be_val = reinterpret_cast<const be_u32 *>(buffer);
  uint32_t host_val = *be_val;

  EXPECT_EQ(host_val, 0x12345678u);
}

TEST(BigEndianTest, U64_SwapsOnAccess) {
  alignas(8) unsigned char buffer[8] = {0x01, 0x02, 0x03, 0x04,
                                        0x05, 0x06, 0x07, 0x08};

  const auto *be_val = reinterpret_cast<const be_u64 *>(buffer);
  uint64_t host_val = *be_val;

  EXPECT_EQ(host_val, 0x0102030405060708ull);
}

TEST(BigEndianTest, RawReturnsUnswappedValue) {
  alignas(4) unsigned char buffer[4] = {0x12, 0x34, 0x56, 0x78};

  const auto *be_val = reinterpret_cast<const be_u32 *>(buffer);

  // raw() should return the bytes as-is (no swap)
  // On little-endian, the raw memory 0x12345678 in BE is 0x78563412 natively
  uint32_t raw_val = be_val->raw();

  // The raw value is the memory layout interpreted as little-endian
  EXPECT_EQ(raw_val, 0x78563412u);
}

// ============================================================================
// Timestamp48 Tests
// ============================================================================

TEST(Timestamp48Test, ConvertsToNanoseconds) {
  // 48-bit timestamp representing 45,296,789,012,345 nanoseconds
  // = 12:34:56.789012345 (about 12.5 hours into the day)
  Timestamp48 ts;
  ts.bytes[0] = 0x00; // MSB
  ts.bytes[1] = 0x00;
  ts.bytes[2] = 0x29;
  ts.bytes[3] = 0x40;
  ts.bytes[4] = 0x69;
  ts.bytes[5] = 0x79; // LSB

  // 0x0000294069'79 = 45,296,789,369 nanoseconds
  uint64_t expected = (0x00ull << 40) | (0x00ull << 32) | (0x29ull << 24) |
                      (0x40ull << 16) | (0x69ull << 8) | 0x79ull;

  EXPECT_EQ(ts.nanoseconds(), expected);
  EXPECT_EQ(static_cast<uint64_t>(ts), expected);
}

TEST(Timestamp48Test, MaxValue) {
  // Max 48-bit value
  Timestamp48 ts;
  std::memset(ts.bytes, 0xFF, 6);

  uint64_t max_48bit = (1ull << 48) - 1; // 0xFFFFFFFFFFFF
  EXPECT_EQ(ts.nanoseconds(), max_48bit);
}

// ============================================================================
// StockSymbol Tests
// ============================================================================

TEST(StockSymbolTest, MatchesExactSymbol) {
  StockSymbol sym;
  std::memcpy(sym.data, "AAPL    ", 8); // Right-padded with spaces

  EXPECT_TRUE(sym.equals("AAPL"));
  EXPECT_TRUE(sym.equals("AAPL    "));
  EXPECT_FALSE(sym.equals("GOOG"));
  EXPECT_FALSE(sym.equals("AAP")); // Too short
}

// ============================================================================
// MessageHeader Tests
// ============================================================================

TEST(MessageHeaderTest, SizeIs11Bytes) { EXPECT_EQ(sizeof(MessageHeader), 11); }

TEST(MessageHeaderTest, ParsesCorrectly) {
  // Build a raw message header buffer
  alignas(8) unsigned char buffer[11] = {
      'A',                               // msg_type
      0x00, 0x42,                        // stock_locate = 66 (BE)
      0x00, 0x01,                        // tracking_number = 1 (BE)
      0x00, 0x00, 0x00, 0x0B, 0xEB, 0xC2 // timestamp = 781250 ns (BE)
  };

  const auto *hdr =
      parse<MessageHeader>(reinterpret_cast<const char *>(buffer));

  EXPECT_EQ(hdr->msg_type, 'A');
  EXPECT_EQ(static_cast<uint16_t>(hdr->stock_locate), 66);
  EXPECT_EQ(static_cast<uint16_t>(hdr->tracking_number), 1);
  EXPECT_EQ(hdr->timestamp.nanoseconds(), 781250ull);
}

// ============================================================================
// AddOrder Tests
// ============================================================================

TEST(AddOrderTest, SizeIs36Bytes) { EXPECT_EQ(sizeof(AddOrder), 36); }

TEST(AddOrderTest, ParsesRealMessage) {
  // Build buffer using a simpler approach - set ALL 36 bytes explicitly
  unsigned char buffer[36] = {
      // Offset 0: msg_type
      'A',
      // Offset 1-2: stock_locate = 1 (BE)
      0x00, 0x01,
      // Offset 3-4: tracking_number = 2 (BE)
      0x00, 0x02,
      // Offset 5-10: timestamp = 1000000000 ns (0x3B9ACA00 in 6 bytes BE)
      0x00, 0x00, 0x3B, 0x9A, 0xCA, 0x00,
      // Offset 11-18: order_ref = 1234567890 (0x499602D2 in 8 bytes BE)
      0x00, 0x00, 0x00, 0x00, 0x49, 0x96, 0x02, 0xD2,
      // Offset 19: side = 'B'
      'B',
      // Offset 20-23: shares = 500 (0x1F4 in 4 bytes BE)
      0x00, 0x00, 0x01, 0xF4,
      // Offset 24-31: stock = "AAPL    " (8 chars)
      'A', 'A', 'P', 'L', ' ', ' ', ' ', ' ',
      // Offset 32-35: price = 1000000 = $100.00 (0xF4240 in 4 bytes BE)
      0x00, 0x0F, 0x42, 0x40};

  const auto *msg = parse<AddOrder>(reinterpret_cast<const char *>(buffer));

  EXPECT_EQ(msg->msg_type, 'A');
  EXPECT_EQ(static_cast<uint16_t>(msg->stock_locate), 1);
  EXPECT_EQ(static_cast<uint16_t>(msg->tracking_number), 2);
  EXPECT_EQ(msg->timestamp.nanoseconds(), 1000000000ull); // 1 second
  EXPECT_EQ(static_cast<uint64_t>(msg->order_ref), 1234567890ull);
  EXPECT_TRUE(msg->is_buy());
  EXPECT_FALSE(msg->is_sell());
  EXPECT_EQ(static_cast<uint32_t>(msg->shares), 500u);
  EXPECT_TRUE(msg->stock.equals("AAPL"));
  EXPECT_EQ(static_cast<uint32_t>(msg->price), 1000000u);
  EXPECT_DOUBLE_EQ(msg->price_double(), 100.00);
}

TEST(AddOrderTest, SellSideWorks) {
  alignas(8) unsigned char buffer[36] = {};
  buffer[0] = 'A';
  buffer[19] = 'S'; // Sell side

  const auto *msg = parse<AddOrder>(reinterpret_cast<const char *>(buffer));
  EXPECT_TRUE(msg->is_sell());
  EXPECT_FALSE(msg->is_buy());
}

// ============================================================================
// OrderExecuted Tests
// ============================================================================

TEST(OrderExecutedTest, SizeIs31Bytes) { EXPECT_EQ(sizeof(OrderExecuted), 31); }

TEST(OrderExecutedTest, ParsesRealMessage) {
  // Build buffer using inline initializer - set ALL 31 bytes explicitly
  unsigned char buffer[31] = {
      // Offset 0: msg_type
      'E',
      // Offset 1-2: stock_locate = 42 (BE)
      0x00, 0x2A,
      // Offset 3-4: tracking_number = 100 (BE)
      0x00, 0x64,
      // Offset 5-10: timestamp = 500000000 ns (0x1DCD6500 in 6 bytes BE)
      0x00, 0x00, 0x1D, 0xCD, 0x65, 0x00,
      // Offset 11-18: order_ref = 9876543210 (0x24CB016EA in 8 bytes BE)
      0x00, 0x00, 0x00, 0x02, 0x4C, 0xB0, 0x16, 0xEA,
      // Offset 19-22: executed_shares = 200 (0xC8 in 4 bytes BE)
      0x00, 0x00, 0x00, 0xC8,
      // Offset 23-30: match_number = 1234567890123 (0x11F71FB04CB in 8 bytes
      // BE)
      0x00, 0x00, 0x01, 0x1F, 0x71, 0xFB, 0x04, 0xCB};

  const auto *msg =
      parse<OrderExecuted>(reinterpret_cast<const char *>(buffer));

  EXPECT_EQ(msg->msg_type, 'E');
  EXPECT_EQ(static_cast<uint16_t>(msg->stock_locate), 42);
  EXPECT_EQ(static_cast<uint16_t>(msg->tracking_number), 100);
  EXPECT_EQ(msg->timestamp.nanoseconds(), 500000000ull);
  EXPECT_EQ(static_cast<uint64_t>(msg->order_ref), 9876543210ull);
  EXPECT_EQ(static_cast<uint32_t>(msg->executed_shares), 200u);
  EXPECT_EQ(static_cast<uint64_t>(msg->match_number), 1234567890123ull);
}

// ============================================================================
// Zero-Copy Verification
// ============================================================================

TEST(ZeroCopyTest, ParseDoesNotCopy) {
  alignas(8) unsigned char buffer[36] = {};
  buffer[0] = 'A';

  const auto *msg = parse<AddOrder>(reinterpret_cast<const char *>(buffer));

  // Verify parse returns pointer to same memory
  EXPECT_EQ(reinterpret_cast<const void *>(msg),
            reinterpret_cast<const void *>(buffer));
}

TEST(ZeroCopyTest, GetMsgTypeWorks) {
  char buffer[2] = {'E', 0};
  EXPECT_EQ(get_msg_type(buffer), 'E');
}

} // namespace itch::test
