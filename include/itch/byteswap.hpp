#pragma once
// Big-endian load helpers. ITCH is a big-endian wire protocol, but x86/ARM are
// little-endian, so every multi-byte integer has to be swapped on read.
//
// I do the swap with explicit shifts instead of memcpy + __builtin_bswap so the
// code is correct regardless of host endianness and the compiler can still fold
// it into a single movbe/rev instruction at -O3. Loading straight through a
// packed struct member would be unaligned + wrong-endian, so decoding always
// goes through these.

#include <cstdint>

namespace itch {

inline uint16_t load_be16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

inline uint32_t load_be32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline uint64_t load_be64(const uint8_t* p) noexcept {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | p[i];
    return v;
}

// ITCH timestamps are 48-bit (6 bytes) nanoseconds since midnight.
inline uint64_t load_be48(const uint8_t* p) noexcept {
    uint64_t v = 0;
    for (int i = 0; i < 6; ++i)
        v = (v << 8) | p[i];
    return v;
}

// Mirror helpers for the synthetic feed generator (host -> big-endian).
inline void store_be16(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

inline void store_be32(uint8_t* p, uint32_t v) noexcept {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

inline void store_be48(uint8_t* p, uint64_t v) noexcept {
    for (int i = 5; i >= 0; --i) {
        p[i] = static_cast<uint8_t>(v & 0xFF);
        v >>= 8;
    }
}

inline void store_be64(uint8_t* p, uint64_t v) noexcept {
    for (int i = 7; i >= 0; --i) {
        p[i] = static_cast<uint8_t>(v & 0xFF);
        v >>= 8;
    }
}

}  // namespace itch
