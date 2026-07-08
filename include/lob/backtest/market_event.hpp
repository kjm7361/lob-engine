// MarketEvent: one tick of market data. The BacktestRunner replays a sequence of
// these through the matching engine to simulate a trading session.
#pragma once

#include "lob/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lob::backtest {

struct MarketEvent {
    Timestamp timestamp;  // nanoseconds (or any monotonic tick counter)
    Price     bid;        // best bid, integer ticks
    Price     ask;        // best ask, integer ticks
    Price     last_trade; // price of the most recent trade
    Quantity  volume;     // volume at last_trade
};

// Load events from a CSV file.
// Expected columns (header row skipped): timestamp,bid,ask,last_trade,volume
// All prices are integer ticks. Returns an empty vector on open failure.
std::vector<MarketEvent> load_csv(const std::string& path);

// Generate a synthetic mean-reverting price series for testing and demos.
// Spread is always 2 ticks: ask = mid+1, bid = mid-1.
std::vector<MarketEvent> synthetic_feed(int64_t mid_price, size_t n_events,
                                        uint64_t seed = 42);

}  // namespace lob::backtest
