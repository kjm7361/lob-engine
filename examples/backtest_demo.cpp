// backtest_demo: mean-reversion strategy backtested against a synthetic LOB feed.
//
// The strategy watches a rolling 20-tick mean of the mid price. When the ask
// is more than 3 ticks below the mean (price is cheap) it buys. When the bid
// is more than 3 ticks above the mean (price is expensive) it sells short.
// It closes the position when the price reverts back to the mean.
#include <deque>
#include <iomanip>
#include <iostream>
#include <numeric>

#include "lob/backtest/market_event.hpp"
#include "lob/backtest/performance.hpp"
#include "lob/backtest/runner.hpp"
#include "lob/backtest/strategy.hpp"

using namespace lob;
using namespace lob::backtest;

class MeanReversionStrategy : public Strategy {
    static constexpr size_t   WINDOW    = 20;
    static constexpr int64_t  THRESHOLD = 3;   // ticks from rolling mean
    static constexpr uint64_t LOT       = 10;

    std::deque<int64_t> window_;
    int64_t             position_{0};  // local position mirror for entry/exit logic

   public:
    std::string name() const override { return "MeanReversion(w=20, t=3)"; }

    void on_event(const MarketEvent& event) override {
        int64_t mid = (to_int(event.bid) + to_int(event.ask)) / 2;
        window_.push_back(mid);
        if (window_.size() > WINDOW) window_.pop_front();
        if (static_cast<int>(window_.size()) < static_cast<int>(WINDOW)) return;

        int64_t sum  = std::accumulate(window_.begin(), window_.end(), int64_t{0});
        int64_t mean = sum / static_cast<int64_t>(WINDOW);

        int64_t ask = to_int(event.ask);
        int64_t bid = to_int(event.bid);

        if (position_ == 0) {
            if (ask < mean - THRESHOLD) {
                // Ask is cheap relative to mean — buy.
                submit(Side::Buy, OrderType::Market, Price{0}, Quantity{LOT}, event.timestamp);
                position_ += static_cast<int64_t>(LOT);
            } else if (bid > mean + THRESHOLD) {
                // Bid is rich relative to mean — short.
                submit(Side::Sell, OrderType::Market, Price{0}, Quantity{LOT}, event.timestamp);
                position_ -= static_cast<int64_t>(LOT);
            }
        } else if (position_ > 0 && bid >= mean) {
            // Long position reverted — close.
            submit(Side::Sell, OrderType::Market, Price{0}, Quantity{LOT}, event.timestamp);
            position_ = 0;
        } else if (position_ < 0 && ask <= mean) {
            // Short position reverted — cover.
            submit(Side::Buy, OrderType::Market, Price{0}, Quantity{LOT}, event.timestamp);
            position_ = 0;
        }
    }

    void on_fill(const Fill& f) override {
        std::cout << "  [FILL] " << (f.side == Side::Buy ? "BUY " : "SELL")
                  << "  qty=" << to_uint(f.quantity)
                  << "  @" << to_int(f.price) << "\n";
    }
};

static void print_equity_sample(const Portfolio& p, size_t step = 50) {
    std::cout << "\n--- Equity Curve (every " << step << " ticks) ---\n";
    std::cout << std::fixed << std::setprecision(2);
    const auto& curve = p.equity_curve();
    for (size_t i = 0; i < curve.size(); i += step) {
        std::cout << "  t=" << std::setw(5) << to_uint(curve[i].timestamp)
                  << "  equity=$" << curve[i].equity << "\n";
    }
    if (!curve.empty()) {
        const auto& last = curve.back();
        std::cout << "  t=" << std::setw(5) << to_uint(last.timestamp)
                  << "  equity=$" << last.equity << "  (final)\n";
    }
}

int main() {
    std::cout << "Generating 500-tick synthetic mean-reverting feed (mid=1000)...\n";
    auto events = synthetic_feed(1000, 500, 42);

    MeanReversionStrategy strategy;
    BacktestRunner        runner(strategy, 100'000.0);

    std::cout << "\nRunning backtest...\n\n";
    auto report = runner.run(events);

    print_equity_sample(runner.portfolio());
    report.print(strategy.name());

    return 0;
}
