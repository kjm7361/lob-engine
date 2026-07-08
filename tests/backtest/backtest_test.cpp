// Backtest module unit tests: portfolio accounting, performance metrics, runner integration.
#include <gtest/gtest.h>

#include <deque>
#include <fstream>
#include <numeric>

#include "lob/backtest/market_event.hpp"
#include "lob/backtest/performance.hpp"
#include "lob/backtest/portfolio.hpp"
#include "lob/backtest/runner.hpp"
#include "lob/backtest/strategy.hpp"

using namespace lob;
using namespace lob::backtest;

// ── Helpers ──────────────────────────────────────────────────────────────────

static Fill make_fill(Side side, int64_t price, uint64_t qty) {
    return Fill{OrderId{1}, side, Price{price}, Quantity{qty}, Timestamp{0}};
}

// ── Portfolio tests ───────────────────────────────────────────────────────────

TEST(Portfolio, BuyThenSell_RealizedPnL) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(Side::Buy,  100, 10));
    p.on_fill(make_fill(Side::Sell, 105, 10));

    EXPECT_NEAR(p.realized_pnl(), 50.0, 1e-9);
    EXPECT_NEAR(p.position(),      0.0, 1e-9);
    EXPECT_EQ  (p.trade_count(),     2);
}

TEST(Portfolio, ShortThenCover_RealizedPnL) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(Side::Sell,  100, 10));
    p.on_fill(make_fill(Side::Buy,    95, 10));

    EXPECT_NEAR(p.realized_pnl(), 50.0, 1e-9);
    EXPECT_NEAR(p.position(),      0.0, 1e-9);
}

TEST(Portfolio, PartialClose_CorrectSplit) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(Side::Buy,  100, 20));  // long 20 @ 100
    p.on_fill(make_fill(Side::Sell, 110, 10));  // close half

    EXPECT_NEAR(p.realized_pnl(), 100.0, 1e-9);  // 10 * (110 - 100)
    EXPECT_NEAR(p.position(),      10.0, 1e-9);  // still long 10
}

TEST(Portfolio, MarkToMarket_UnrealizedPnL) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(Side::Buy, 100, 10));  // long 10 @ 100
    p.mark(Timestamp{1}, Price{110});

    EXPECT_NEAR(p.unrealized_pnl(), 100.0, 1e-9);   // 10 * (110 - 100)
    // cash decreased by 100*10 = 1000; equity = cash + position * mark = 99000 + 1100 = 100100
    EXPECT_EQ  (p.equity_curve().size(), 1u);
    EXPECT_NEAR(p.equity_curve()[0].equity, 100'100.0, 1e-9);
}

TEST(Portfolio, CashAccountingIsConsistent) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(Side::Buy,  100, 10));  // spend 1000
    EXPECT_NEAR(p.cash(), 99'000.0, 1e-9);

    p.on_fill(make_fill(Side::Sell, 105, 10));  // receive 1050
    EXPECT_NEAR(p.cash(), 100'050.0, 1e-9);
}

// ── Performance report tests ──────────────────────────────────────────────────

TEST(Performance, TotalReturn_FlatEquity) {
    Portfolio p(100'000.0);
    p.mark(Timestamp{0}, Price{100});  // equity ≈ starting cash (no position)

    auto r = compute_report(p);
    EXPECT_NEAR(r.total_return_pct, 0.0, 1e-6);
    EXPECT_EQ  (r.trade_count, 0);
}

TEST(Performance, MaxDrawdown_Detected) {
    Portfolio p(100'000.0);
    p.on_fill(make_fill(Side::Buy, 100, 100));  // long 100 @ 100
    p.mark(Timestamp{0}, Price{100});  // equity = 100k
    p.mark(Timestamp{1}, Price{90});   // equity = 99k cash + 100*90 = 99k - ... let's compute
    // cash after buy: 100k - 10k = 90k; mark @ 90: equity = 90k + 100*90 = 90k + 9k = 99k

    auto r = compute_report(p);
    // first equity = 90k + 100*100 = 100k; second = 90k + 100*90 = 99k → dd = 1%
    EXPECT_GT(r.max_drawdown_pct, 0.0);
}

// ── Synthetic feed tests ──────────────────────────────────────────────────────

TEST(SyntheticFeed, SpreadIsAlwaysTwo) {
    auto events = synthetic_feed(1000, 200, 42);
    ASSERT_EQ(events.size(), 200u);
    for (const auto& e : events) {
        EXPECT_EQ(to_int(e.ask) - to_int(e.bid), 2)
            << "spread must be exactly 2 ticks (bid = mid-1, ask = mid+1)";
        EXPECT_EQ(to_int(e.last_trade), (to_int(e.bid) + to_int(e.ask)) / 2);
    }
}

TEST(SyntheticFeed, MeanRevertsAroundStartPrice) {
    auto events = synthetic_feed(1000, 500, 7);
    int64_t sum = 0;
    for (const auto& e : events) sum += (to_int(e.bid) + to_int(e.ask)) / 2;
    double mean_mid = static_cast<double>(sum) / static_cast<double>(events.size());
    // With mean reversion kappa=0.05 the long-run mean should be near 1000.
    EXPECT_NEAR(mean_mid, 1000.0, 50.0);
}

// ── Runner integration tests ──────────────────────────────────────────────────

// Strategy that buys on tick 0, sells on tick 1, then does nothing.
class BuyThenSellStrategy : public Strategy {
    int tick_{0};
   public:
    std::string name() const override { return "BuyThenSell"; }
    void on_event(const MarketEvent& e) override {
        if (tick_ == 0)
            submit(Side::Buy,  OrderType::Market, Price{0}, Quantity{10}, e.timestamp);
        if (tick_ == 1)
            submit(Side::Sell, OrderType::Market, Price{0}, Quantity{10}, e.timestamp);
        ++tick_;
    }
    void on_fill(const Fill&) override {}
};

TEST(Runner, MarketOrders_TwoFillsRecorded) {
    auto events = synthetic_feed(1000, 10, 42);
    BuyThenSellStrategy strat;
    BacktestRunner runner(strat, 100'000.0);

    auto report = runner.run(events);

    // Two market orders → two fills → trade_count = 2
    EXPECT_EQ(runner.portfolio().trade_count(), 2);
    EXPECT_EQ(runner.portfolio().equity_curve().size(), events.size());
    EXPECT_EQ(report.trade_count, 2);
}

TEST(Runner, NetPositionFlat_AfterRoundTrip) {
    auto events = synthetic_feed(1000, 10, 42);
    BuyThenSellStrategy strat;
    BacktestRunner runner(strat, 100'000.0);
    runner.run(events);

    EXPECT_NEAR(runner.portfolio().position(), 0.0, 1e-9);
}

// Strategy that places a limit buy 5 ticks below the starting mid,
// then closes with a market sell once filled.
class LimitBuyStrategy : public Strategy {
    bool    order_placed_{false};
    bool    filled_{false};
    int64_t entry_price_{0};
   public:
    std::string name() const override { return "LimitBuy"; }

    void on_event(const MarketEvent& e) override {
        if (!order_placed_) {
            // Place a limit buy 5 ticks below the first ask.
            int64_t limit = to_int(e.bid) - 5;
            submit(Side::Buy, OrderType::Limit, Price{limit}, Quantity{5}, e.timestamp);
            entry_price_ = limit;
            order_placed_ = true;
        }
        if (filled_) {
            // Close the position with a market sell.
            submit(Side::Sell, OrderType::Market, Price{0}, Quantity{5}, e.timestamp);
            filled_ = false;
        }
    }

    void on_fill(const Fill& f) override {
        if (f.side == Side::Buy) filled_ = true;
    }
};

TEST(Runner, LimitOrder_FillsWhenMarketReaches) {
    // Run for enough ticks that the mean-reverting price crosses below bid-5.
    auto events = synthetic_feed(1000, 500, 99);
    LimitBuyStrategy strat;
    BacktestRunner   runner(strat, 100'000.0);
    runner.run(events);

    // The limit order should have filled at least once over 500 ticks.
    EXPECT_GT(runner.portfolio().trade_count(), 0);
}

// ── CSV round-trip test ───────────────────────────────────────────────────────

TEST(LoadCSV, RoundTrip) {
    const std::string path = "/tmp/lob_test_events.csv";
    {
        std::ofstream f(path);
        f << "timestamp,bid,ask,last_trade,volume\n";
        f << "1000,99,101,100,500\n";
        f << "1001,100,102,101,300\n";
    }

    auto events = load_csv(path);
    ASSERT_EQ(events.size(), 2u);

    EXPECT_EQ(to_uint(events[0].timestamp),  1000u);
    EXPECT_EQ(to_int (events[0].bid),           99);
    EXPECT_EQ(to_int (events[0].ask),          101);
    EXPECT_EQ(to_uint(events[0].volume),       500u);

    EXPECT_EQ(to_int (events[1].last_trade),   101);
}
