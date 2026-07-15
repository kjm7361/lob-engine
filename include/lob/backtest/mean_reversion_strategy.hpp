// MeanReversionStrategy: header-only educational strategy for the backtest module.
//
// Rationale: when the ask is more than `threshold` ticks below the rolling mean
// the price looks cheap — buy. When the bid is more than `threshold` ticks above
// the mean the price looks expensive — sell short. Close when the price reverts
// back to the mean.
//
// This is a deterministic, educational prototype. It makes no claim of
// profitability on live markets.
#pragma once

#include "lob/backtest/market_event.hpp"
#include "lob/backtest/strategy.hpp"
#include "lob/types.hpp"

#include <deque>
#include <numeric>
#include <string>

namespace lob::backtest {

class MeanReversionStrategy : public Strategy {
   public:
    // window    : rolling mean period (number of ticks)
    // threshold : entry signal distance from mean, in ticks
    // lot       : order size in units
    explicit MeanReversionStrategy(size_t window = 20, int64_t threshold = 3,
                                   uint64_t lot = 10)
        : window_(window), threshold_(threshold), lot_(lot) {}

    std::string name() const override {
        return "MeanReversion(w=" + std::to_string(window_) +
               ",t=" + std::to_string(threshold_) + ")";
    }

    void on_event(const MarketEvent& event) override {
        int64_t mid = (to_int(event.bid) + to_int(event.ask)) / 2;
        history_.push_back(mid);
        if (history_.size() > window_) history_.pop_front();
        if (history_.size() < window_) return;

        int64_t sum  = std::accumulate(history_.begin(), history_.end(), int64_t{0});
        int64_t mean = sum / static_cast<int64_t>(window_);

        int64_t ask = to_int(event.ask);
        int64_t bid = to_int(event.bid);

        if (position_ == 0) {
            if (ask < mean - threshold_) {
                submit(Side::Buy, OrderType::Market, Price{0}, Quantity{lot_},
                       event.timestamp);
                position_ += static_cast<int64_t>(lot_);
            } else if (bid > mean + threshold_) {
                submit(Side::Sell, OrderType::Market, Price{0}, Quantity{lot_},
                       event.timestamp);
                position_ -= static_cast<int64_t>(lot_);
            }
        } else if (position_ > 0 && bid >= mean) {
            submit(Side::Sell, OrderType::Market, Price{0}, Quantity{lot_},
                   event.timestamp);
            position_ = 0;
        } else if (position_ < 0 && ask <= mean) {
            submit(Side::Buy, OrderType::Market, Price{0}, Quantity{lot_},
                   event.timestamp);
            position_ = 0;
        }
    }

    void on_fill(const Fill&) override {}

   private:
    size_t              window_;
    int64_t             threshold_;
    uint64_t            lot_;
    std::deque<int64_t> history_;
    int64_t             position_{0};
};

}  // namespace lob::backtest
