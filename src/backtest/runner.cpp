#include "lob/backtest/runner.hpp"

#include "lob/matching_engine.hpp"
#include "lob/order.hpp"

#include <limits>
#include <memory>
#include <unordered_map>

namespace lob::backtest {

// Synthetic orders represent the "market" — these IDs will never belong to the strategy.
static constexpr uint64_t SYNTH_ASK_ID  = std::numeric_limits<uint64_t>::max();
static constexpr uint64_t SYNTH_BID_ID  = std::numeric_limits<uint64_t>::max() - 1;
// Strategy order IDs start at 1 and increment — all are well below this threshold.
static constexpr uint64_t SYNTH_MIN_ID  = std::numeric_limits<uint64_t>::max() - 1;
static constexpr uint64_t SYNTH_QTY     = 1'000'000;

BacktestRunner::BacktestRunner(Strategy& strategy, double starting_cash)
    : strategy_(strategy), portfolio_(starting_cash) {}

PerformanceReport BacktestRunner::run(const std::vector<MarketEvent>& events) {
    if (events.empty()) return compute_report(portfolio_);

    // Per-run state — isolated here so run() leaves no side effects if called again.
    uint64_t next_id = 1;
    std::vector<std::unique_ptr<Order>> order_pool;
    order_pool.reserve(events.size() * 2);

    // Maps strategy order id → side so fills can be attributed correctly.
    std::unordered_map<uint64_t, Side> strategy_sides;

    // Two reusable Order objects represent the synthetic bid/ask each tick.
    // They are value-initialised (prev/next = nullptr) and reset before each submit.
    Order synth_ask{};
    Order synth_bid{};

    // Build the engine here so its EventHandler can capture local references.
    MatchingEngine engine(EventHandler{
        [&](const TradeEvent& e) {
            uint64_t maker_id = to_uint(e.maker_id);
            uint64_t taker_id = to_uint(e.taker_id);

            // A strategy fill is any trade where a strategy order was involved.
            // Synthetic IDs are >= SYNTH_MIN_ID and will never appear in strategy_sides.
            if (maker_id < SYNTH_MIN_ID) {
                auto it = strategy_sides.find(maker_id);
                if (it != strategy_sides.end()) {
                    Fill f{e.maker_id, it->second, e.price, e.quantity, e.timestamp};
                    portfolio_.on_fill(f);
                    strategy_.on_fill(f);
                }
            }
            if (taker_id < SYNTH_MIN_ID) {
                auto it = strategy_sides.find(taker_id);
                if (it != strategy_sides.end()) {
                    Fill f{e.taker_id, it->second, e.price, e.quantity, e.timestamp};
                    portfolio_.on_fill(f);
                    strategy_.on_fill(f);
                }
            }
        },
        [](const OrderAccepted&)  {},
        [](const OrderCancelled&) {},
        [](const OrderRejected&)  {},  // expected for synthetic orders after full consumption
    });

    // Wire the strategy's submit() to allocate Orders into order_pool and
    // forward them to the engine.
    strategy_.set_submit([&](Side side, OrderType type, Price price, Quantity qty,
                             Timestamp ts) -> OrderId {
        OrderId id{next_id++};
        auto    o               = std::make_unique<Order>();
        o->id                   = id;
        o->side                 = side;
        o->type                 = type;
        o->price                = price;
        o->quantity             = qty;
        o->remaining_quantity   = qty;
        o->timestamp            = ts;
        Order* raw              = o.get();
        order_pool.push_back(std::move(o));
        strategy_sides[to_uint(id)] = side;
        engine.submit(raw);
        return id;
    });

    auto reset_synth = [](Order& o, uint64_t id, Side side, Price price, Timestamp ts) {
        o.id                = OrderId{id};
        o.side              = side;
        o.type              = OrderType::Limit;
        o.price             = price;
        o.quantity          = Quantity{SYNTH_QTY};
        o.remaining_quantity = Quantity{SYNTH_QTY};
        o.timestamp         = ts;
        o.prev              = nullptr;
        o.next              = nullptr;
    };

    for (const auto& event : events) {
        // Step 1: resting synthetic SELL at the ask.
        // Fills any strategy buy limits that are now at-market or better.
        reset_synth(synth_ask, SYNTH_ASK_ID, Side::Sell, event.ask, event.timestamp);
        engine.submit(&synth_ask);

        // Step 2: resting synthetic BUY at the bid.
        // Fills any strategy sell limits that are now at-market or better.
        reset_synth(synth_bid, SYNTH_BID_ID, Side::Buy, event.bid, event.timestamp);
        engine.submit(&synth_bid);

        // Step 3: strategy observes the tick and may submit aggressive or passive orders.
        strategy_.on_event(event);

        // Step 4: remove synthetic orders — on_rejected is a no-op if already consumed.
        engine.cancel(OrderId{SYNTH_ASK_ID});
        engine.cancel(OrderId{SYNTH_BID_ID});

        // Step 5: mark portfolio at current mid.
        int64_t mid = (to_int(event.bid) + to_int(event.ask)) / 2;
        portfolio_.mark(event.timestamp, Price{mid});
    }

    return compute_report(portfolio_);
}

}  // namespace lob::backtest
