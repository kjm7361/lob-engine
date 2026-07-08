#include "lob/backtest/portfolio.hpp"

#include <algorithm>
#include <cmath>

namespace lob::backtest {

Portfolio::Portfolio(double starting_cash)
    : starting_cash_(starting_cash), cash_(starting_cash) {}

void Portfolio::on_fill(const Fill& fill, double tick_value) {
    double price = static_cast<double>(to_int(fill.price));
    double qty   = static_cast<double>(to_uint(fill.quantity));

    if (fill.side == Side::Buy) {
        cash_ -= qty * price * tick_value;

        if (position_ < 0.0) {
            // Covering a short: the profit is (entry price - cover price) per unit.
            double cover = std::min(qty, -position_);
            realized_pnl_ += cover * (avg_price_ - price) * tick_value;
            qty       -= cover;
            position_ += cover;
        }
        if (qty > 0.0) {
            // Opening or extending a long: blend into average cost.
            avg_price_ = (position_ * avg_price_ + qty * price) / (position_ + qty);
            position_ += qty;
        }
    } else {
        cash_ += qty * price * tick_value;

        if (position_ > 0.0) {
            // Closing a long: the profit is (sell price - entry price) per unit.
            double close = std::min(qty, position_);
            realized_pnl_ += close * (price - avg_price_) * tick_value;
            qty       -= close;
            position_ -= close;
        }
        if (qty > 0.0) {
            // Opening or extending a short: blend into average cost.
            double abs_pos = std::abs(position_);
            avg_price_ = (abs_pos * avg_price_ + qty * price) / (abs_pos + qty);
            position_ -= qty;
        }
    }

    ++trade_count_;
}

void Portfolio::mark(Timestamp ts, Price mid, double tick_value) {
    double mid_price    = static_cast<double>(to_int(mid));
    unrealized_pnl_     = position_ * (mid_price - avg_price_) * tick_value;
    double equity       = cash_ + position_ * mid_price * tick_value;
    equity_curve_.push_back({ts, equity});
}

}  // namespace lob::backtest
