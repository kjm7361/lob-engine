// performance_report.hpp — canonical include for PerformanceReport and compute_report.
//
// Metrics computed:
//   total_return_pct  — (final_equity − starting_cash) / starting_cash × 100
//   sharpe_ratio      — annualised step-return Sharpe (assumes 1 event = 1 minute)
//   max_drawdown_pct  — largest peak-to-trough equity drop as a percentage
//   avg_trade_pnl     — realized_pnl / trade_count  (0 if no trades)
//   trade_count
//
// Only metrics listed above are calculated. No look-ahead bias correction,
// transaction cost model, or slippage model is applied.
#pragma once

#include "performance.hpp"
