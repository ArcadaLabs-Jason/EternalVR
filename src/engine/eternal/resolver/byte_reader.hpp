#pragma once

// Bounds-checked little-endian reads from a byte span.
//
// Every read of the executable goes through here, so malformed or truncated input produces an empty
// optional instead of an out-of-bounds access. Reads assemble values byte by byte, which is correct
// on any host endianness and has no alignment requirements.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace evr::resolver {

using ByteSpan = std::span<const std::byte>;

// True when [offset, offset + size) lies inside a buffer of `total` bytes, without overflowing.
constexpr bool rangeFits(std::size_t total, std::size_t offset, std::size_t size) {
    return offset <= total && size <= total - offset;
}

template <std::unsigned_integral T>
std::optional<T> readLe(ByteSpan bytes, std::size_t offset) {
    if (!rangeFits(bytes.size(), offset, sizeof(T))) {
        return std::nullopt;
    }
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const auto byte = static_cast<T>(std::to_integer<std::uint8_t>(bytes[offset + i]));
        value = static_cast<T>(value | static_cast<T>(byte << (8 * i)));
    }
    return value;
}

inline std::optional<std::int32_t> readLeI32(ByteSpan bytes, std::size_t offset) {
    const std::optional<std::uint32_t> raw = readLe<std::uint32_t>(bytes, offset);
    if (!raw) {
        return std::nullopt;
    }
    // Two's-complement reinterpretation, well-defined since C++20.
    return static_cast<std::int32_t>(*raw);
}

} // namespace evr::resolver
