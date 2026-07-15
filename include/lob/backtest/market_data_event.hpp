// MarketDataEvent: one raw order-book event parsed from a CSV replay file.
// Used for deterministic order-book reconstruction and educational LOB simulation.
#pragma once

#include "lob/types.hpp"

#include <cstdint>

namespace lob::backtest {

enum class MarketDataEventType : uint8_t {
    Add,     // rest a limit order on the book
    Market,  // submit an immediate-or-cancel market order
    Cancel,  // remove a resting limit order by id
};

struct MarketDataEvent {
    Timestamp           timestamp;
    MarketDataEventType event_type;
    Side                side;
    // For Add:    resting limit price (integer ticks)
    // For Market: 0 (price is irrelevant)
    // For Cancel: the order_id to cancel (reuses the price field to avoid extra columns)
    Price    price;
    Quantity quantity;  // 0 for Cancel events
};

}  // namespace lob::backtest
