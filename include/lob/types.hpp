// Core types: strong typedefs for OrderId, Price (integer ticks), Quantity, Timestamp, Side.
#pragma once

#include <cstdint>

namespace lob {

// Wrapping primitives in enum classes so you can't accidentally pass a Price
// where an OrderId is expected. Zero runtime cost.
enum class OrderId : uint64_t {};
enum class Price : int64_t {};   // always integer ticks, never a double
enum class Quantity : uint64_t {};
enum class Timestamp : uint64_t {};  // nanoseconds

enum class Side : uint8_t { Buy, Sell };

enum class OrderType : uint8_t { Limit, Market };

// Use these when you need to do arithmetic on the underlying value.
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
