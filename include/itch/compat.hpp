#pragma once

/**
 * @file compat.hpp
 * @brief Endianness utilities for NASDAQ ITCH protocol parsing.
 * 
 * NASDAQ TotalView-ITCH 5.0 uses Big Endian byte order.
 * x86/x86_64 CPUs are Little Endian.
 * This header provides zero-overhead byte swap utilities.
 */

#include <cstdint>
#include <bit>

namespace itch {

// ============================================================================
// Compile-time endianness detection
// ============================================================================
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    inline constexpr bool kIsBigEndian = true;
#else
    inline constexpr bool kIsBigEndian = false;
#endif

// ============================================================================
// Byte swap utilities (Big Endian -> Little Endian)
// ============================================================================

/**
 * @brief Swap bytes of a 16-bit integer (network to host order).
 * @param val Big-endian 16-bit value from ITCH buffer.
 * @return Little-endian value for CPU consumption.
 */
[[nodiscard]] inline constexpr uint16_t bswap16(uint16_t val) noexcept {
    if constexpr (kIsBigEndian) {
        return val;  // No swap needed on Big Endian systems
    } else {
#if __cplusplus >= 202302L
        return std::byteswap(val);  // C++23 standard
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap16(val);
#elif defined(_MSC_VER)
        return _byteswap_ushort(val);
#else
        return static_cast<uint16_t>((val << 8) | (val >> 8));
#endif
    }
}

/**
 * @brief Swap bytes of a 32-bit integer (network to host order).
 * @param val Big-endian 32-bit value from ITCH buffer.
 * @return Little-endian value for CPU consumption.
 */
[[nodiscard]] inline constexpr uint32_t bswap32(uint32_t val) noexcept {
    if constexpr (kIsBigEndian) {
        return val;
    } else {
#if __cplusplus >= 202302L
        return std::byteswap(val);
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap32(val);
#elif defined(_MSC_VER)
        return _byteswap_ulong(val);
#else
        return ((val & 0xFF000000u) >> 24) |
               ((val & 0x00FF0000u) >> 8)  |
               ((val & 0x0000FF00u) << 8)  |
               ((val & 0x000000FFu) << 24);
#endif
    }
}

/**
 * @brief Swap bytes of a 64-bit integer (network to host order).
 * @param val Big-endian 64-bit value from ITCH buffer.
 * @return Little-endian value for CPU consumption.
 */
[[nodiscard]] inline constexpr uint64_t bswap64(uint64_t val) noexcept {
    if constexpr (kIsBigEndian) {
        return val;
    } else {
#if __cplusplus >= 202302L
        return std::byteswap(val);
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_bswap64(val);
#elif defined(_MSC_VER)
        return _byteswap_uint64(val);
#else
        return ((val & 0xFF00000000000000ull) >> 56) |
               ((val & 0x00FF000000000000ull) >> 40) |
               ((val & 0x0000FF0000000000ull) >> 24) |
               ((val & 0x000000FF00000000ull) >> 8)  |
               ((val & 0x00000000FF000000ull) << 8)  |
               ((val & 0x0000000000FF0000ull) << 24) |
               ((val & 0x000000000000FF00ull) << 40) |
               ((val & 0x00000000000000FFull) << 56);
#endif
    }
}

// ============================================================================
// Type-safe network-to-host conversion
// ============================================================================

/**
 * @brief Convert network byte order (Big Endian) to host byte order.
 * @tparam T Integral type (uint16_t, uint32_t, uint64_t).
 * @param val Network byte order value.
 * @return Host byte order value.
 */
template <typename T>
[[nodiscard]] inline constexpr T ntoh(T val) noexcept {
    static_assert(std::is_integral_v<T>, "ntoh requires integral type");
    
    if constexpr (sizeof(T) == 1) {
        return val;  // Single byte, no swap needed
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(bswap16(static_cast<uint16_t>(val)));
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(bswap32(static_cast<uint32_t>(val)));
    } else if constexpr (sizeof(T) == 8) {
        return static_cast<T>(bswap64(static_cast<uint64_t>(val)));
    } else {
        static_assert(sizeof(T) <= 8, "Unsupported type size for byte swap");
    }
}

}  // namespace itch
