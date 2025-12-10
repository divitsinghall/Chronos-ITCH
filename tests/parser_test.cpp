/**
 * @file parser_test.cpp
 * @brief Unit tests for ITCH Parser and Visitor pattern.
 */

#include <gtest/gtest.h>
#include <itch/parser.hpp>
#include <vector>

namespace itch::test {

// ============================================================================
// Test Visitor - Counts messages
// ============================================================================

struct CountingVisitor : DefaultVisitor {
  int add_order_count = 0;
  int order_executed_count = 0;
  int system_event_count = 0;
  int unknown_count = 0;
  char last_unknown_type = 0;

  void on_add_order(const AddOrder & /*msg*/) { ++add_order_count; }
  void on_order_executed(const OrderExecuted & /*msg*/) {
    ++order_executed_count;
  }
  void on_system_event(const MessageHeader & /*msg*/) { ++system_event_count; }
  void on_unknown(char msg_type, const char * /*data*/, size_t /*len*/) {
    ++unknown_count;
    last_unknown_type = msg_type;
  }
};

// ============================================================================
// Test Visitor - Captures message data
// ============================================================================

struct CapturingVisitor : DefaultVisitor {
  std::vector<uint64_t> order_refs;
  std::vector<uint32_t> shares;

  void on_add_order(const AddOrder &msg) {
    order_refs.push_back(static_cast<uint64_t>(msg.order_ref));
    shares.push_back(static_cast<uint32_t>(msg.shares));
  }

  void on_order_executed(const OrderExecuted &msg) {
    order_refs.push_back(static_cast<uint64_t>(msg.order_ref));
  }
};

// ============================================================================
// Parser Tests
// ============================================================================

TEST(ParserTest, ParseAddOrder) {
  unsigned char buffer[36] = {
      'A',                                            // msg_type
      0x00, 0x01,                                     // stock_locate
      0x00, 0x02,                                     // tracking_number
      0x00, 0x00, 0x3B, 0x9A, 0xCA, 0x00,             // timestamp
      0x00, 0x00, 0x00, 0x00, 0x49, 0x96, 0x02, 0xD2, // order_ref
      'B',                                            // side
      0x00, 0x00, 0x01, 0xF4,                         // shares = 500
      'A',  'A',  'P',  'L',  ' ',  ' ',  ' ',  ' ',  // stock
      0x00, 0x0F, 0x42, 0x40                          // price
  };

  CountingVisitor visitor;
  Parser parser;
  ParseResult result = parser.parse(reinterpret_cast<const char *>(buffer),
                                    sizeof(buffer), visitor);

  EXPECT_EQ(result, ParseResult::Ok);
  EXPECT_EQ(visitor.add_order_count, 1);
  EXPECT_EQ(visitor.order_executed_count, 0);
  EXPECT_EQ(visitor.unknown_count, 0);
}

TEST(ParserTest, ParseOrderExecuted) {
  unsigned char buffer[31] = {
      'E',                                            // msg_type
      0x00, 0x2A,                                     // stock_locate = 42
      0x00, 0x64,                                     // tracking_number = 100
      0x00, 0x00, 0x1D, 0xCD, 0x65, 0x00,             // timestamp
      0x00, 0x00, 0x00, 0x02, 0x4C, 0xB0, 0x16, 0xEA, // order_ref
      0x00, 0x00, 0x00, 0xC8,                         // executed_shares = 200
      0x00, 0x00, 0x01, 0x1F, 0x71, 0xFB, 0x04, 0xCB  // match_number
  };

  CountingVisitor visitor;
  Parser parser;
  ParseResult result = parser.parse(reinterpret_cast<const char *>(buffer),
                                    sizeof(buffer), visitor);

  EXPECT_EQ(result, ParseResult::Ok);
  EXPECT_EQ(visitor.add_order_count, 0);
  EXPECT_EQ(visitor.order_executed_count, 1);
}

TEST(ParserTest, BufferTooSmall_ReturnsError) {
  unsigned char buffer[5] = {'A', 0x00, 0x01, 0x00, 0x02}; // Too small

  CountingVisitor visitor;
  Parser parser;
  ParseResult result = parser.parse(reinterpret_cast<const char *>(buffer),
                                    sizeof(buffer), visitor);

  EXPECT_EQ(result, ParseResult::BufferTooSmall);
  EXPECT_EQ(visitor.add_order_count, 0);
}

TEST(ParserTest, UnknownType_DispatchesToOnUnknown) {
  unsigned char buffer[11] = {'Z', // Unknown message type
                              0x00, 0x01, 0x00, 0x02, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00};

  CountingVisitor visitor;
  Parser parser;
  ParseResult result = parser.parse(reinterpret_cast<const char *>(buffer),
                                    sizeof(buffer), visitor);

  EXPECT_EQ(result, ParseResult::UnknownType);
  EXPECT_EQ(visitor.unknown_count, 1);
  EXPECT_EQ(visitor.last_unknown_type, 'Z');
}

// ============================================================================
// parse_buffer Tests (multiple messages)
// ============================================================================

TEST(ParserTest, ParseBuffer_MultipleMessages) {
  // Buffer with 2 AddOrder messages
  std::vector<unsigned char> buffer;

  // First AddOrder (order_ref = 1234567890)
  unsigned char msg1[36] = {
      'A',  0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x3B, 0x9A, 0xCA, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x49, 0x96, 0x02, 0xD2, 'B',  0x00, 0x00, 0x01, 0xF4,
      'A',  'A',  'P',  'L',  ' ',  ' ',  ' ',  ' ',  0x00, 0x0F, 0x42, 0x40};

  // Second AddOrder (order_ref = 9876543210)
  unsigned char msg2[36] = {
      'A',  0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x3B, 0x9A, 0xCA, 0x00, 0x00,
      0x00, 0x00, 0x02, 0x4C, 0xB0, 0x16, 0xEA, 'S',  0x00, 0x00, 0x03, 0xE8,
      'G',  'O',  'O',  'G',  ' ',  ' ',  ' ',  ' ',  0x00, 0x1E, 0x84, 0x80};

  buffer.insert(buffer.end(), msg1, msg1 + 36);
  buffer.insert(buffer.end(), msg2, msg2 + 36);

  CapturingVisitor visitor;
  Parser parser;
  size_t consumed = parser.parse_buffer(
      reinterpret_cast<const char *>(buffer.data()), buffer.size(), visitor);

  EXPECT_EQ(consumed, 72u); // 36 + 36
  EXPECT_EQ(visitor.order_refs.size(), 2u);
  EXPECT_EQ(visitor.order_refs[0], 1234567890ull);
  EXPECT_EQ(visitor.order_refs[1], 9876543210ull);
  EXPECT_EQ(visitor.shares[0], 500u);
  EXPECT_EQ(visitor.shares[1], 1000u);
}

TEST(ParserTest, ParseBuffer_MixedMessageTypes) {
  std::vector<unsigned char> buffer;

  // AddOrder
  unsigned char add_order[36] = {
      'A',  0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x3B, 0x9A, 0xCA, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x49, 0x96, 0x02, 0xD2, 'B',  0x00, 0x00, 0x01, 0xF4,
      'A',  'A',  'P',  'L',  ' ',  ' ',  ' ',  ' ',  0x00, 0x0F, 0x42, 0x40};

  // OrderExecuted
  unsigned char order_executed[31] = {
      'E',  0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x3B, 0x9A, 0xCA, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x49, 0x96, 0x02, 0xD2, 0x00, 0x00, 0x00,
      0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

  buffer.insert(buffer.end(), add_order, add_order + 36);
  buffer.insert(buffer.end(), order_executed, order_executed + 31);

  CountingVisitor visitor;
  Parser parser;
  size_t consumed = parser.parse_buffer(
      reinterpret_cast<const char *>(buffer.data()), buffer.size(), visitor);

  EXPECT_EQ(consumed, 67u); // 36 + 31
  EXPECT_EQ(visitor.add_order_count, 1);
  EXPECT_EQ(visitor.order_executed_count, 1);
}

TEST(ParserTest, ParseBuffer_IncompleteMessage_StopsEarly) {
  // Full AddOrder + partial second message
  std::vector<unsigned char> buffer;

  unsigned char add_order[36] = {
      'A',  0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x3B, 0x9A, 0xCA, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x49, 0x96, 0x02, 0xD2, 'B',  0x00, 0x00, 0x01, 0xF4,
      'A',  'A',  'P',  'L',  ' ',  ' ',  ' ',  ' ',  0x00, 0x0F, 0x42, 0x40};

  buffer.insert(buffer.end(), add_order, add_order + 36);
  buffer.push_back('A'); // Incomplete second message
  buffer.push_back(0x00);
  buffer.push_back(0x01);

  CountingVisitor visitor;
  Parser parser;
  size_t consumed = parser.parse_buffer(
      reinterpret_cast<const char *>(buffer.data()), buffer.size(), visitor);

  EXPECT_EQ(consumed, 36u); // Only first message consumed
  EXPECT_EQ(visitor.add_order_count, 1);
}

// ============================================================================
// get_message_size Tests
// ============================================================================

TEST(MessageSizeTest, KnownTypes) {
  EXPECT_EQ(get_message_size(msg_type::AddOrder), 36u);
  EXPECT_EQ(get_message_size(msg_type::OrderExecuted), 31u);
  EXPECT_EQ(get_message_size(msg_type::SystemEvent), sizeof(MessageHeader));
}

TEST(MessageSizeTest, UnknownType_ReturnsZero) {
  EXPECT_EQ(get_message_size('Z'), 0u);
  EXPECT_EQ(get_message_size('\0'), 0u);
}

// ============================================================================
// Convenience Function Test
// ============================================================================

TEST(ParserTest, ParseMessageFunction) {
  unsigned char buffer[36] = {
      'A',  0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x3B, 0x9A, 0xCA, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x49, 0x96, 0x02, 0xD2, 'B',  0x00, 0x00, 0x01, 0xF4,
      'A',  'A',  'P',  'L',  ' ',  ' ',  ' ',  ' ',  0x00, 0x0F, 0x42, 0x40};

  CountingVisitor visitor;
  ParseResult result = parse_message(reinterpret_cast<const char *>(buffer),
                                     sizeof(buffer), visitor);

  EXPECT_EQ(result, ParseResult::Ok);
  EXPECT_EQ(visitor.add_order_count, 1);
}

} // namespace itch::test
