// BacktestRunner: replays market events through a strategy and the matching engine.
//
// Per tick the runner:
//   1. Places a synthetic resting SELL at the event's ask (representing market supply).
//   2. Places a synthetic resting BUY  at the event's bid (representing market demand).
//      Steps 1-2 fill any resting strategy limit orders that have crossed the new market.
//   3. Calls strategy.on_event() — the strategy may submit new orders, which match
//      against the still-resting synthetic orders.
//   4. Cancels the synthetic orders.
//   5. Marks the portfolio at the current mid price.
//
// Call run() once per BacktestRunner instance.
#pragma once

#include "lob/types.hpp"
#include "market_event.hpp"
#include "performance.hpp"
#include "portfolio.hpp"
#include "strategy.hpp"

#include <vector>

namespace lob::backtest {

class BacktestRunner {
   public:
    explicit BacktestRunner(Strategy& strategy, double starting_cash = 100'000.0);

    PerformanceReport run(const std::vector<MarketEvent>& events);

    [[nodiscard]] const Portfolio& portfolio() const noexcept { return portfolio_; }

   private:
    Strategy& strategy_;
    Portfolio portfolio_;
};

}  // namespace lob::backtest
