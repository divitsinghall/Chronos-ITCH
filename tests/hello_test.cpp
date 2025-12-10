/**
 * @file hello_test.cpp
 * @brief Placeholder tests to verify GTest integration and compat.hpp
 * correctness.
 */

#include <cstdint>
#include <gtest/gtest.h>
#include <itch/compat.hpp>

namespace itch::test {

// ============================================================================
// Byte Swap Tests
// ============================================================================

TEST(CompatTest, Bswap16_ReversesByteOrder) {
  EXPECT_EQ(itch::bswap16(0x1234), 0x3412);
  EXPECT_EQ(itch::bswap16(0x0000), 0x0000);
  EXPECT_EQ(itch::bswap16(0xFFFF), 0xFFFF);
  EXPECT_EQ(itch::bswap16(0xFF00), 0x00FF);
  EXPECT_EQ(itch::bswap16(0x00FF), 0xFF00);
}

TEST(CompatTest, Bswap32_ReversesByteOrder) {
  EXPECT_EQ(itch::bswap32(0x12345678), 0x78563412u);
  EXPECT_EQ(itch::bswap32(0x00000000), 0x00000000u);
  EXPECT_EQ(itch::bswap32(0xFFFFFFFF), 0xFFFFFFFFu);
  EXPECT_EQ(itch::bswap32(0xFF000000), 0x000000FFu);
  EXPECT_EQ(itch::bswap32(0x000000FF), 0xFF000000u);
}

TEST(CompatTest, Bswap64_ReversesByteOrder) {
  EXPECT_EQ(itch::bswap64(0x123456789ABCDEF0ull), 0xF0DEBC9A78563412ull);
  EXPECT_EQ(itch::bswap64(0x0000000000000000ull), 0x0000000000000000ull);
  EXPECT_EQ(itch::bswap64(0xFFFFFFFFFFFFFFFFull), 0xFFFFFFFFFFFFFFFFull);
}

// ============================================================================
// Network-to-Host Conversion Tests
// ============================================================================

TEST(CompatTest, Ntoh_SingleByte_NoChange) {
  uint8_t val = 0x42;
  EXPECT_EQ(itch::ntoh(val), 0x42);
}

TEST(CompatTest, Ntoh_16bit_SwapsCorrectly) {
  uint16_t big_endian_value = 0x1234; // Big endian representation
  uint16_t expected_host = 0x3412;    // Little endian on x86

  // On little endian systems, ntoh should swap
  if constexpr (!itch::kIsBigEndian) {
    EXPECT_EQ(itch::ntoh(big_endian_value), expected_host);
  } else {
    EXPECT_EQ(itch::ntoh(big_endian_value), big_endian_value);
  }
}

TEST(CompatTest, Ntoh_32bit_SwapsCorrectly) {
  uint32_t big_endian_value = 0x12345678;
  uint32_t expected_host = 0x78563412;

  if constexpr (!itch::kIsBigEndian) {
    EXPECT_EQ(itch::ntoh(big_endian_value), expected_host);
  } else {
    EXPECT_EQ(itch::ntoh(big_endian_value), big_endian_value);
  }
}

// ============================================================================
// Constexpr Verification
// ============================================================================

TEST(CompatTest, ByteSwapIsConstexpr) {
  constexpr uint32_t swapped = itch::bswap32(0x12345678);
  EXPECT_EQ(swapped, 0x78563412u);

  constexpr uint64_t swapped64 = itch::bswap64(0x123456789ABCDEF0ull);
  EXPECT_EQ(swapped64, 0xF0DEBC9A78563412ull);
}

} // namespace itch::test
