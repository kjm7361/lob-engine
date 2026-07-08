// Strategy: abstract base class for a backtestable trading strategy.
// Subclass this, implement on_event and on_fill, and call submit() to place orders.
#pragma once

#include "lob/types.hpp"
#include "market_event.hpp"

#include <functional>
#include <string>

namespace lob::backtest {

// A completed fill delivered to the strategy after one of its orders matches.
struct Fill {
    OrderId   order_id;
    Side      side;
    Price     price;     // always the maker's resting price
    Quantity  quantity;
    Timestamp timestamp;
};

// Injected by the runner so the strategy can place orders without a direct
// dependency on the matching engine.
using SubmitFn = std::function<OrderId(Side, OrderType, Price, Quantity, Timestamp)>;

class Strategy {
   public:
    virtual ~Strategy() = default;

    // Called for every market tick before synthetic liquidity is cleaned up.
    virtual void on_event(const MarketEvent& event) = 0;

    // Called whenever one of this strategy's orders fills (partial or full).
    virtual void on_fill(const Fill& fill) = 0;

    virtual std::string name() const = 0;

    void set_submit(SubmitFn fn) { submit_ = std::move(fn); }

   protected:
    // Place an order. Returns the assigned OrderId.
    OrderId submit(Side side, OrderType type, Price price, Quantity qty, Timestamp ts) {
        return submit_(side, type, price, qty, ts);
    }

   private:
    SubmitFn submit_;
};

}  // namespace lob::backtest
