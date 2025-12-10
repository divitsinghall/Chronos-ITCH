#pragma once

/**
 * @file messages.hpp
 * @brief Zero-copy ITCH 5.0 message structures with lazy byte-swap semantics.
 *
 * DESIGN PRINCIPLES:
 * 1. All structs are packed to match wire format exactly (no padding).
 * 2. BigEndian<T> wrapper provides transparent byte-swap on access.
 * 3. No memcpy, no allocations - direct reinterpret_cast from buffer.
 * 4. Fields are swapped lazily on operator T(), not all at once.
 *
 * USAGE:
 *   const auto* msg = reinterpret_cast<const AddOrder*>(buffer);
 *   uint64_t order_ref = msg->order_ref;  // Auto byte-swaps on access
 */

#include "compat.hpp"
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace itch {

// ============================================================================
// BigEndian<T> - Lazy Byte-Swap Wrapper
// ============================================================================

/**
 * @brief A transparent wrapper for big-endian values that swaps on access.
 *
 * This class stores data in network byte order (big-endian) and converts
 * to host byte order (little-endian on x86) only when accessed.
 *
 * The struct is trivially copyable and has the same size/alignment as T,
 * making it safe for use in packed structures with reinterpret_cast.
 *
 * @tparam T Integral type (uint8_t, uint16_t, uint32_t, uint64_t)
 *
 * @example
 *   BigEndian<uint32_t> price;  // Stored as big-endian
 *   uint32_t host_price = price;  // Swapped to host order on access
 */
template <typename T> class BigEndian {
  static_assert(std::is_integral_v<T>, "BigEndian<T> requires integral type");
  static_assert(std::is_unsigned_v<T>, "BigEndian<T> requires unsigned type");

public:
  using value_type = T;

  /**
   * @brief Implicit conversion to host byte order.
   *
   * This is the "lazy swap" - byte swap happens HERE, not at construction.
   * Compiles to a single BSWAP instruction on x86.
   *
   * @return Value in host byte order (little-endian on x86).
   */
  [[nodiscard]] constexpr operator T() const noexcept { return ntoh(value_); }

  /**
   * @brief Get raw big-endian value without byte swap.
   *
   * Useful for hashing, comparison, or passing to network functions.
   *
   * @return Value in network byte order (big-endian).
   */
  [[nodiscard]] constexpr T raw() const noexcept { return value_; }

private:
  T value_; // Stored in big-endian (network byte order)
};

// Verify BigEndian is trivially copyable (required for reinterpret_cast safety)
static_assert(std::is_trivially_copyable_v<BigEndian<uint16_t>>);
static_assert(std::is_trivially_copyable_v<BigEndian<uint32_t>>);
static_assert(std::is_trivially_copyable_v<BigEndian<uint64_t>>);

// Verify sizes match underlying types
static_assert(sizeof(BigEndian<uint16_t>) == sizeof(uint16_t));
static_assert(sizeof(BigEndian<uint32_t>) == sizeof(uint32_t));
static_assert(sizeof(BigEndian<uint64_t>) == sizeof(uint64_t));

// ============================================================================
// Type Aliases for ITCH Fields
// ============================================================================

using be_u16 = BigEndian<uint16_t>; // 2-byte big-endian integer
using be_u32 = BigEndian<uint32_t>; // 4-byte big-endian integer
using be_u64 = BigEndian<uint64_t>; // 8-byte big-endian integer

// ============================================================================
// Timestamp Handling (48-bit / 6 bytes)
// ============================================================================

/**
 * @brief 48-bit timestamp (nanoseconds since midnight).
 *
 * ITCH timestamps are 6 bytes, representing nanoseconds since midnight.
 * Max value: 24h * 60m * 60s * 1e9ns = 86,400,000,000,000 (fits in 47 bits).
 *
 * Layout: [B5][B4][B3][B2][B1][B0] where B5 is most significant (big-endian).
 */
struct __attribute__((packed)) Timestamp48 {
  uint8_t bytes[6];

  /**
   * @brief Convert to 64-bit nanoseconds since midnight.
   *
   * Manually reconstructs the 48-bit value from 6 bytes.
   */
  [[nodiscard]] constexpr uint64_t nanoseconds() const noexcept {
    return (static_cast<uint64_t>(bytes[0]) << 40) |
           (static_cast<uint64_t>(bytes[1]) << 32) |
           (static_cast<uint64_t>(bytes[2]) << 24) |
           (static_cast<uint64_t>(bytes[3]) << 16) |
           (static_cast<uint64_t>(bytes[4]) << 8) |
           (static_cast<uint64_t>(bytes[5]));
  }

  /**
   * @brief Implicit conversion to nanoseconds.
   */
  [[nodiscard]] constexpr operator uint64_t() const noexcept {
    return nanoseconds();
  }
};

static_assert(sizeof(Timestamp48) == 6, "Timestamp48 must be exactly 6 bytes");

// ============================================================================
// Stock Symbol (8 bytes, right-padded with spaces)
// ============================================================================

/**
 * @brief 8-character stock symbol (ASCII, space-padded on right).
 *
 * No byte swap needed - just raw ASCII characters.
 */
struct __attribute__((packed)) StockSymbol {
  char data[8];

  /**
   * @brief Check if symbol matches a given string.
   *
   * Comparison verifies exact match - str must match data exactly,
   * with remaining data chars being spaces (right-padded).
   */
  [[nodiscard]] bool equals(const char *str) const noexcept {
    int i = 0;
    // Match characters while str has content
    for (; i < 8 && str[i] != '\0'; ++i) {
      if (data[i] != str[i])
        return false;
    }
    // Verify remaining chars in data are spaces (right-padding)
    for (; i < 8; ++i) {
      if (data[i] != ' ')
        return false;
    }
    // Verify str doesn't have extra characters beyond 8
    return str[i] == '\0' || i == 8;
  }
};

static_assert(sizeof(StockSymbol) == 8, "StockSymbol must be exactly 8 bytes");

// ============================================================================
// Message Type Constants
// ============================================================================

namespace msg_type {
inline constexpr char SystemEvent = 'S';
inline constexpr char StockDirectory = 'R';
inline constexpr char StockTradingAction = 'H';
inline constexpr char RegSHORestriction = 'Y';
inline constexpr char MarketParticipantPosition = 'L';
inline constexpr char MWCBDeclineLevel = 'V';
inline constexpr char MWCBStatus = 'W';
inline constexpr char IPOQuotingPeriod = 'K';
inline constexpr char AddOrder = 'A';
inline constexpr char AddOrderMPID = 'F';
inline constexpr char OrderExecuted = 'E';
inline constexpr char OrderExecutedWithPrice = 'C';
inline constexpr char OrderCancel = 'X';
inline constexpr char OrderDelete = 'D';
inline constexpr char OrderReplace = 'U';
inline constexpr char Trade = 'P';
inline constexpr char CrossTrade = 'Q';
inline constexpr char BrokenTrade = 'B';
inline constexpr char NOII = 'I';
} // namespace msg_type

// ============================================================================
// Message Header (Common to All Messages)
// ============================================================================

/**
 * @brief Common header for all ITCH 5.0 messages.
 *
 * Layout (9 bytes):
 *   Offset 0: Message Type (1 byte) - char
 *   Offset 1: Stock Locate (2 bytes) - uint16_t BE
 *   Offset 3: Tracking Number (2 bytes) - uint16_t BE
 *   Offset 5: Timestamp (6 bytes) - 48-bit nanoseconds since midnight
 */
struct __attribute__((packed)) MessageHeader {
  char msg_type;          // Offset 0: Message type ('A', 'E', etc.)
  be_u16 stock_locate;    // Offset 1: Locate code identifying security
  be_u16 tracking_number; // Offset 3: NASDAQ internal tracking number
  Timestamp48 timestamp;  // Offset 5: Nanoseconds since midnight
};

static_assert(sizeof(MessageHeader) == 11, "MessageHeader must be 11 bytes");
static_assert(offsetof(MessageHeader, msg_type) == 0);
static_assert(offsetof(MessageHeader, stock_locate) == 1);
static_assert(offsetof(MessageHeader, tracking_number) == 3);
static_assert(offsetof(MessageHeader, timestamp) == 5);

// ============================================================================
// Add Order Message (Type 'A') - No MPID Attribution
// ============================================================================

/**
 * @brief Add Order message indicating new order added to book.
 *
 * Total size: 36 bytes
 *
 * Layout:
 *   Offset  0: Message Type (1 byte) = 'A'
 *   Offset  1: Stock Locate (2 bytes)
 *   Offset  3: Tracking Number (2 bytes)
 *   Offset  5: Timestamp (6 bytes)
 *   Offset 11: Order Reference Number (8 bytes)
 *   Offset 19: Buy/Sell Indicator (1 byte) 'B' or 'S'
 *   Offset 20: Shares (4 bytes)
 *   Offset 24: Stock Symbol (8 bytes)
 *   Offset 32: Price (4 bytes) - price * 10000 (4 decimal places)
 */
struct __attribute__((packed)) AddOrder {
  // Header fields (11 bytes)
  char msg_type;          // Offset 0: 'A'
  be_u16 stock_locate;    // Offset 1
  be_u16 tracking_number; // Offset 3
  Timestamp48 timestamp;  // Offset 5

  // Order fields (25 bytes)
  be_u64 order_ref;  // Offset 11: Unique order reference number
  char side;         // Offset 19: 'B' = Buy, 'S' = Sell
  be_u32 shares;     // Offset 20: Number of shares
  StockSymbol stock; // Offset 24: Stock symbol (8 chars)
  be_u32 price;      // Offset 32: Price * 10000

  // Accessors
  [[nodiscard]] constexpr bool is_buy() const noexcept { return side == 'B'; }
  [[nodiscard]] constexpr bool is_sell() const noexcept { return side == 'S'; }

  /**
   * @brief Get price as double (converts from fixed-point).
   */
  [[nodiscard]] double price_double() const noexcept {
    return static_cast<double>(static_cast<uint32_t>(price)) / 10000.0;
  }
};

static_assert(sizeof(AddOrder) == 36, "AddOrder must be 36 bytes");
static_assert(offsetof(AddOrder, msg_type) == 0);
static_assert(offsetof(AddOrder, stock_locate) == 1);
static_assert(offsetof(AddOrder, tracking_number) == 3);
static_assert(offsetof(AddOrder, timestamp) == 5);
static_assert(offsetof(AddOrder, order_ref) == 11);
static_assert(offsetof(AddOrder, side) == 19);
static_assert(offsetof(AddOrder, shares) == 20);
static_assert(offsetof(AddOrder, stock) == 24);
static_assert(offsetof(AddOrder, price) == 32);

// ============================================================================
// Order Executed Message (Type 'E')
// ============================================================================

/**
 * @brief Order Executed message sent when order on book is executed.
 *
 * Total size: 31 bytes
 *
 * Layout:
 *   Offset  0: Message Type (1 byte) = 'E'
 *   Offset  1: Stock Locate (2 bytes)
 *   Offset  3: Tracking Number (2 bytes)
 *   Offset  5: Timestamp (6 bytes)
 *   Offset 11: Order Reference Number (8 bytes)
 *   Offset 19: Executed Shares (4 bytes)
 *   Offset 23: Match Number (8 bytes)
 */
struct __attribute__((packed)) OrderExecuted {
  // Header fields (11 bytes)
  char msg_type;          // Offset 0: 'E'
  be_u16 stock_locate;    // Offset 1
  be_u16 tracking_number; // Offset 3
  Timestamp48 timestamp;  // Offset 5

  // Execution fields (20 bytes)
  be_u64 order_ref;       // Offset 11: Order being executed
  be_u32 executed_shares; // Offset 19: Number of shares executed
  be_u64 match_number;    // Offset 23: Match ID

  /**
   * @brief Get header reference for polymorphic access.
   */
  [[nodiscard]] const MessageHeader &header() const noexcept {
    return *reinterpret_cast<const MessageHeader *>(this);
  }
};

static_assert(sizeof(OrderExecuted) == 31, "OrderExecuted must be 31 bytes");
static_assert(offsetof(OrderExecuted, msg_type) == 0);
static_assert(offsetof(OrderExecuted, stock_locate) == 1);
static_assert(offsetof(OrderExecuted, tracking_number) == 3);
static_assert(offsetof(OrderExecuted, timestamp) == 5);
static_assert(offsetof(OrderExecuted, order_ref) == 11);
static_assert(offsetof(OrderExecuted, executed_shares) == 19);
static_assert(offsetof(OrderExecuted, match_number) == 23);

// ============================================================================
// Zero-Copy Message Parsing
// ============================================================================

/**
 * @brief Cast raw buffer to message type (zero-copy).
 *
 * @tparam T Message struct type (AddOrder, OrderExecuted, etc.)
 * @param buffer Raw network buffer
 * @return Pointer to message (no copy, direct overlay)
 *
 * @warning Buffer must be properly aligned and contain valid ITCH data.
 * @warning Buffer lifetime must exceed usage of returned pointer.
 *
 * @example
 *   const char* buffer = ...;  // Raw ITCH data
 *   if (buffer[0] == msg_type::AddOrder) {
 *       const auto* msg = parse<AddOrder>(buffer);
 *       uint64_t order_ref = msg->order_ref;  // Auto byte-swap
 *   }
 */
template <typename T>
[[nodiscard]] inline const T *parse(const char *buffer) noexcept {
  static_assert(std::is_trivially_copyable_v<T>,
                "Message must be trivially copyable");
  return reinterpret_cast<const T *>(buffer);
}

/**
 * @brief Get message type from buffer.
 */
[[nodiscard]] inline char get_msg_type(const char *buffer) noexcept {
  return buffer[0];
}

} // namespace itch
