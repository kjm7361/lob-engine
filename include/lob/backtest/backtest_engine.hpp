// BacktestEngine: primary entry point for strategy backtesting.
//
// Replays a sequence of MarketEvent (bid/ask) ticks through the matching engine,
// calling strategy.on_event() per tick, capturing fills, and tracking PnL in a
// Portfolio. Unlike BacktestRunner, the strategy is passed to run() rather than
// the constructor — the same engine instance can run multiple strategies without
// reconstruction.
//
// This is a strategy execution prototype for educational market-microstructure
// simulation. It is not a production-grade backtesting framework.
#pragma once

#include "lob/backtest/market_event.hpp"
#include "lob/backtest/performance.hpp"
#include "lob/backtest/portfolio.hpp"
#include "lob/backtest/strategy.hpp"

#include <optional>
#include <vector>

namespace lob::backtest {

class BacktestEngine {
   public:
    explicit BacktestEngine(double starting_cash = 100'000.0);

    // Replay events through the strategy. Portfolio state is fully reset
    // between calls, so this can be called multiple times safely.
    PerformanceReport run(Strategy& strategy, const std::vector<MarketEvent>& events);

    // Valid only after at least one call to run().
    [[nodiscard]] const Portfolio& portfolio() const noexcept;

   private:
    double                   starting_cash_;
    std::optional<Portfolio> portfolio_;
};

}  // namespace lob::backtest
