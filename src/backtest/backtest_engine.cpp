#include "lob/backtest/backtest_engine.hpp"
#include "lob/backtest/runner.hpp"

namespace lob::backtest {

BacktestEngine::BacktestEngine(double starting_cash)
    : starting_cash_(starting_cash) {}

PerformanceReport BacktestEngine::run(Strategy& strategy,
                                      const std::vector<MarketEvent>& events) {
    // Construct a fresh BacktestRunner on each call so all internal state
    // (order pool, id counter, engine) is fully reset between runs.
    BacktestRunner runner(strategy, starting_cash_);
    auto report = runner.run(events);
    portfolio_.emplace(runner.portfolio());
    return report;
}

const Portfolio& BacktestEngine::portfolio() const noexcept {
    return *portfolio_;
}

}  // namespace lob::backtest
