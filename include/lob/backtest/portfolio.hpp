// Portfolio: tracks net position, cash, and PnL across fills.
#pragma once

#include "lob/types.hpp"
#include "strategy.hpp"

#include <vector>

namespace lob::backtest {

struct EquityPoint {
    Timestamp timestamp;
    double    equity;  // cash + mark-to-market position value
};

class Portfolio {
   public:
    explicit Portfolio(double starting_cash);

    // Update position and cash from a fill.
    // tick_value converts one price tick to dollars (default 1.0 = 1 tick = $1).
    void on_fill(const Fill& fill, double tick_value = 1.0);

    // Snapshot the current equity at the given mid price and timestamp.
    void mark(Timestamp ts, Price mid, double tick_value = 1.0);

    [[nodiscard]] double position()       const noexcept { return position_; }
    [[nodiscard]] double cash()           const noexcept { return cash_; }
    [[nodiscard]] double realized_pnl()   const noexcept { return realized_pnl_; }
    [[nodiscard]] double unrealized_pnl() const noexcept { return unrealized_pnl_; }
    [[nodiscard]] int    trade_count()    const noexcept { return trade_count_; }
    [[nodiscard]] double starting_cash()  const noexcept { return starting_cash_; }
    [[nodiscard]] const std::vector<EquityPoint>& equity_curve() const noexcept {
        return equity_curve_;
    }

   private:
    double starting_cash_;
    double cash_;
    double position_{0.0};      // net units: positive = long, negative = short
    double avg_price_{0.0};     // average entry price (always positive)
    double realized_pnl_{0.0};
    double unrealized_pnl_{0.0};
    int    trade_count_{0};
    std::vector<EquityPoint> equity_curve_;
};

}  // namespace lob::backtest
