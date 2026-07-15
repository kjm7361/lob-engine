// simple_backtest: minimal backtesting demonstration for the LOB engine.
//
// Part 1 — Order-book event replay
//   Loads sample_events.csv and submits each row as a raw limit/market/cancel
//   operation into a fresh MatchingEngine, printing the resulting trade log.
//   This demonstrates deterministic LOB reconstruction from a CSV event stream.
//
// Part 2 — Strategy backtest
//   Runs MeanReversionStrategy against a 500-tick synthetic mean-reverting feed
//   via BacktestEngine + Portfolio, then prints a performance report.
//   This is an educational market-microstructure simulator, not a live-trading
//   system or a claim of profitable strategy performance.
#include <iomanip>
#include <iostream>
#include <string>

#include "lob/backtest/backtest_engine.hpp"
#include "lob/backtest/market_data_event.hpp"
#include "lob/backtest/market_data_replay.hpp"
#include "lob/backtest/mean_reversion_strategy.hpp"
#include "lob/backtest/performance_report.hpp"

using namespace lob;
using namespace lob::backtest;

static void run_order_replay(const std::string& csv_path) {
    std::cout << "=== Part 1: Order-book event replay (" << csv_path << ") ===\n";

    auto events = load_market_data_csv(csv_path);
    if (events.empty()) {
        std::cout << "  No events loaded — check that the file exists.\n\n";
        return;
    }

    auto result = replay(events);

    std::cout << "  Events replayed  : " << result.events_replayed  << "\n";
    std::cout << "  Trades generated : " << result.trades_generated << "\n";
    std::cout << "  Orders rejected  : " << result.orders_rejected  << "\n";

    if (!result.trades.empty()) {
        std::cout << "\n  Trade log:\n";
        for (const auto& t : result.trades) {
            std::cout << "    maker=" << std::setw(3) << to_uint(t.maker_id)
                      << "  taker=" << std::setw(3) << to_uint(t.taker_id)
                      << "  @" << std::setw(6) << to_int(t.price)
                      << "  x" << to_uint(t.quantity) << "\n";
        }
    }
    std::cout << "\n";
}

static void run_strategy_backtest() {
    auto events = synthetic_feed(10000, 500, 42);

    MeanReversionStrategy strategy(20, 3, 10);
    BacktestEngine engine(100'000.0);
    auto report = engine.run(strategy, events);

    const auto& port = engine.portfolio();
    double total_pnl = port.realized_pnl() + port.unrealized_pnl();

    std::cout << std::fixed;
    std::cout << "Backtest: " << strategy.name() << "\n";
    std::cout << "Events replayed : " << events.size()                      << "\n";
    std::cout << "Trades          : " << report.trade_count                  << "\n";
    std::cout << "Final Position  : " << std::setprecision(2) << port.position() << "\n";
    std::cout << "Cash            : $" << std::setprecision(2) << port.cash()    << "\n";
    std::cout << "Total PnL       : $" << std::setprecision(4) << total_pnl      << "\n";
    std::cout << "Max Drawdown    : " << std::setprecision(2) << report.max_drawdown_pct << " %\n";
    std::cout << "Sharpe Ratio    : " << std::setprecision(2) << report.sharpe_ratio     << "\n";
    std::cout << "Total Return    : " << std::setprecision(2) << report.total_return_pct << " %\n";
    std::cout << "Avg Trade PnL   : $" << std::setprecision(4) << report.avg_trade_pnl  << "\n";
}

int main(int argc, char* argv[]) {
    std::string csv_path = (argc > 1) ? argv[1] : "examples/data/sample_events.csv";
    run_order_replay(csv_path);
    run_strategy_backtest();
    return 0;
}
