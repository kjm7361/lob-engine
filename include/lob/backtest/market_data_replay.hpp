// MarketDataReplay: load and replay raw order-book events into a fresh matching engine.
//
// CSV format (header required):
//   timestamp,event_type,side,price,quantity
//
//   event_type : add | market | cancel
//   side       : buy | sell   (ignored for cancel)
//   price      : tick integer; 0 for market orders;
//                order_id (uint64) to cancel for cancel events
//   quantity   : 0 for cancel events
//
// Malformed rows are silently skipped. Replay assigns order IDs sequentially
// starting at start_id. Any limit orders still resting at the end of replay are
// cancelled before the internal order pool is freed, so there are no dangling
// engine pointers after replay() returns.
#pragma once

#include "market_data_event.hpp"
#include "lob/matching_engine.hpp"

#include <string>
#include <vector>

namespace lob::backtest {

// Load events from a CSV file. Returns an empty vector on open failure.
std::vector<MarketDataEvent> load_market_data_csv(const std::string& path);

struct ReplayResult {
    size_t                  events_replayed;
    size_t                  trades_generated;
    size_t                  orders_rejected;  // only counts rejections during replay, not cleanup
    std::vector<TradeEvent> trades;
};

// Replay events through a freshly-constructed matching engine and return the results.
ReplayResult replay(const std::vector<MarketDataEvent>& events, uint64_t start_id = 1);

}  // namespace lob::backtest
