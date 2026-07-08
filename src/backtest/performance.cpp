#include "lob/backtest/performance.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

namespace lob::backtest {

PerformanceReport compute_report(const Portfolio& portfolio) {
    PerformanceReport r{};
    r.trade_count = portfolio.trade_count();

    const auto& curve = portfolio.equity_curve();
    if (curve.empty()) return r;

    double start = portfolio.starting_cash();
    double end   = curve.back().equity;
    r.total_return_pct = (end - start) / start * 100.0;

    // Step returns for Sharpe calculation.
    std::vector<double> rets;
    rets.reserve(curve.size());
    for (size_t i = 1; i < curve.size(); ++i) {
        double prev = curve[i - 1].equity;
        if (prev != 0.0) rets.push_back((curve[i].equity - prev) / prev);
    }

    if (rets.size() > 1) {
        double mean = std::accumulate(rets.begin(), rets.end(), 0.0) /
                      static_cast<double>(rets.size());
        double var = 0.0;
        for (double x : rets) var += (x - mean) * (x - mean);
        var /= static_cast<double>(rets.size() - 1);
        double std_dev = std::sqrt(var);
        // Annualise assuming each event = 1 minute, 390 min/day, 252 days/year.
        double annualise = std::sqrt(390.0 * 252.0);
        r.sharpe_ratio = (std_dev > 1e-12) ? mean / std_dev * annualise : 0.0;
    }

    // Max drawdown: largest peak-to-trough drop in equity.
    double peak   = curve[0].equity;
    double max_dd = 0.0;
    for (const auto& pt : curve) {
        peak   = std::max(peak, pt.equity);
        double dd = (peak > 0.0) ? (peak - pt.equity) / peak * 100.0 : 0.0;
        max_dd = std::max(max_dd, dd);
    }
    r.max_drawdown_pct = max_dd;

    r.avg_trade_pnl = (r.trade_count > 0)
                          ? portfolio.realized_pnl() / r.trade_count
                          : 0.0;

    return r;
}

void PerformanceReport::print(const std::string& strategy_name) const {
    auto flag = std::cout.flags();
    std::cout << std::fixed;

    std::cout << "\n=== Backtest: " << strategy_name << " ===\n";
    std::cout << "  Total Return  : " << std::setw(8) << std::setprecision(2)
              << total_return_pct << " %\n";
    std::cout << "  Sharpe Ratio  : " << std::setw(8) << std::setprecision(2)
              << sharpe_ratio << "\n";
    std::cout << "  Max Drawdown  : " << std::setw(8) << std::setprecision(2)
              << max_drawdown_pct << " %\n";
    std::cout << "  Trades        : " << std::setw(8) << trade_count << "\n";
    std::cout << "  Avg Trade PnL : " << std::setw(8) << std::setprecision(4)
              << avg_trade_pnl << "\n\n";

    std::cout.flags(flag);
}

}  // namespace lob::backtest
