#pragma once

#include <cstdint>

namespace lob {

// Strong typedefs via enum class trick — zero overhead, type-safe, no implicit
// conversions between OrderId/Price/Quantity.

enum class OrderId : uint64_t {};
enum class Price : int64_t {};   // integer ticks — no floating-point on hot path
enum class Quantity : uint64_t {};
enum class Timestamp : uint64_t {};  // nanoseconds since epoch

enum class Side : uint8_t { Buy, Sell };

enum class OrderType : uint8_t { Limit, Market };

// Helpers to convert strong typedefs to/from their underlying types.
[[nodiscard]] constexpr uint64_t to_uint(OrderId id) noexcept {
    return static_cast<uint64_t>(id);
}
[[nodiscard]] constexpr int64_t to_int(Price p) noexcept {
    return static_cast<int64_t>(p);
}
[[nodiscard]] constexpr uint64_t to_uint(Quantity q) noexcept {
    return static_cast<uint64_t>(q);
}
[[nodiscard]] constexpr uint64_t to_uint(Timestamp t) noexcept {
    return static_cast<uint64_t>(t);
}

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

}  // namespace lob
