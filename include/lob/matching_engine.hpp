// MatchingEngine: price-time priority matching for limit and market orders, event-driven output.
#pragma once

#include <functional>

#include "order_book.hpp"
#include "types.hpp"

namespace lob {

struct TradeEvent {
    OrderId   maker_id;  // the order that was resting on the book
    OrderId   taker_id;  // the incoming order that crossed
    Price     price;     // always the maker's price — takers get the resting price
    Quantity  quantity;  // how much traded
    Timestamp timestamp;
};

struct OrderAccepted {
    OrderId  id;
    Side     side;
    Price    price;     // zero for market orders
    Quantity quantity;
};

struct OrderCancelled {
    OrderId  id;
    Quantity remaining_quantity;
};

enum class RejectReason : uint8_t {
    UnknownId,               // cancel/modify for an id not on the book
    InvalidQuantity,         // submitted with qty = 0
    MarketOrderNoLiquidity,  // market order ran out of book and can't rest
};

struct OrderRejected {
    OrderId      id;
    RejectReason reason;
};

// std::function callbacks for Phase 1. Phase 2 will template on a handler concept
// (CRTP) to remove the indirection from the hot path.
struct EventHandler {
    std::function<void(const TradeEvent&)>     on_trade;
    std::function<void(const OrderAccepted&)>  on_accepted;
    std::function<void(const OrderCancelled&)> on_cancelled;
    std::function<void(const OrderRejected&)>  on_rejected;
};

class MatchingEngine {
   public:
    explicit MatchingEngine(EventHandler handler);

    // Match what you can against the opposite side, then rest any remainder.
    // Caller keeps ownership of the Order — don't free it while it may be resting.
    void submit(Order* o);

    // Remove a resting order. Fires on_rejected if the id is unknown.
    void cancel(OrderId id);

    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
    [[nodiscard]] OrderBook&       book() noexcept { return book_; }

   private:
    OrderBook    book_;
    EventHandler handler_;

    void match_limit(Order* taker);
    void match_market(Order* taker);

    // Executes one fill between a resting maker and an incoming taker.
    void execute_fill(Order* maker, Order* taker, Quantity qty, Timestamp ts);
};

}  // namespace lob
