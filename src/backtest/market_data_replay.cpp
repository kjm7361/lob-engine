#include "lob/backtest/market_data_replay.hpp"
#include "lob/order.hpp"

#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_set>

namespace lob::backtest {

std::vector<MarketDataEvent> load_market_data_csv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return {};

    std::vector<MarketDataEvent> events;
    std::string line;
    std::getline(file, line);  // skip header row

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tok;

        try {
            std::getline(ss, tok, ',');
            uint64_t ts = std::stoull(tok);

            std::getline(ss, tok, ',');
            MarketDataEventType et;
            if      (tok == "add")    et = MarketDataEventType::Add;
            else if (tok == "market") et = MarketDataEventType::Market;
            else if (tok == "cancel") et = MarketDataEventType::Cancel;
            else continue;

            std::getline(ss, tok, ',');
            Side side;
            if      (tok == "buy")  side = Side::Buy;
            else if (tok == "sell") side = Side::Sell;
            else continue;

            std::getline(ss, tok, ',');
            int64_t price = std::stoll(tok);

            std::getline(ss, tok, ',');
            uint64_t qty = std::stoull(tok);

            events.push_back({
                Timestamp{ts},
                et,
                side,
                Price{price},
                Quantity{qty},
            });
        } catch (...) {
            continue;  // skip malformed rows
        }
    }
    return events;
}

ReplayResult replay(const std::vector<MarketDataEvent>& events, uint64_t start_id) {
    ReplayResult result{};
    result.events_replayed = events.size();

    // All Order objects live here; raw pointers are handed to the engine.
    // They must outlive the engine — cleanup cancels drain the book first.
    std::vector<std::unique_ptr<Order>> pool;
    pool.reserve(events.size());

    // IDs of limit orders that may still be resting at end of replay.
    std::unordered_set<uint64_t> live_limit_ids;

    // Guards against counting cleanup-cancel rejections as replay rejections.
    bool counting = true;

    MatchingEngine engine(EventHandler{
        [&](const TradeEvent& e) {
            ++result.trades_generated;
            result.trades.push_back(e);
        },
        [](const OrderAccepted&)  {},
        [](const OrderCancelled&) {},
        [&](const OrderRejected& e) {
            if (counting) {
                ++result.orders_rejected;
                // Rejected limit orders never rested, so remove from live set.
                live_limit_ids.erase(to_uint(e.id));
            }
        },
    });

    uint64_t next_id = start_id;

    for (const auto& ev : events) {
        switch (ev.event_type) {
            case MarketDataEventType::Add: {
                auto o               = std::make_unique<Order>();
                o->id                = OrderId{next_id++};
                o->side              = ev.side;
                o->type              = OrderType::Limit;
                o->price             = ev.price;
                o->quantity          = ev.quantity;
                o->remaining_quantity = ev.quantity;
                o->timestamp         = ev.timestamp;
                live_limit_ids.insert(to_uint(o->id));
                engine.submit(o.get());
                pool.push_back(std::move(o));
                break;
            }
            case MarketDataEventType::Market: {
                auto o               = std::make_unique<Order>();
                o->id                = OrderId{next_id++};
                o->side              = ev.side;
                o->type              = OrderType::Market;
                o->price             = Price{0};
                o->quantity          = ev.quantity;
                o->remaining_quantity = ev.quantity;
                o->timestamp         = ev.timestamp;
                // Market orders can't rest, so not added to live_limit_ids.
                engine.submit(o.get());
                pool.push_back(std::move(o));
                break;
            }
            case MarketDataEventType::Cancel: {
                // price field encodes the order_id to cancel.
                uint64_t cancel_id = static_cast<uint64_t>(to_int(ev.price));
                engine.cancel(OrderId{cancel_id});
                live_limit_ids.erase(cancel_id);
                break;
            }
        }
    }

    // Drain any remaining resting limit orders so the engine holds no pointers
    // into pool after this function returns. on_rejected fires for already-consumed
    // orders — the counting guard ensures those aren't counted as replay rejections.
    counting = false;
    for (uint64_t id : live_limit_ids) {
        engine.cancel(OrderId{id});
    }

    return result;
}

}  // namespace lob::backtest
