// PerformanceReport: summary metrics computed from a completed backtest run.
#pragma once

#include "portfolio.hpp"

#include <string>

namespace lob::backtest {

struct PerformanceReport {
    double total_return_pct;  // (final_equity - starting_cash) / starting_cash * 100
    double sharpe_ratio;      // annualised, assuming each event = 1 minute
    double max_drawdown_pct;  // worst peak-to-trough drop, as a percentage
    double avg_trade_pnl;     // realized_pnl / trade_count (0 if no trades)
    int    trade_count;

    void print(const std::string& strategy_name) const;
};

PerformanceReport compute_report(const Portfolio& portfolio);

}  // namespace lob::backtest
