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

    // Modify a resting order in place.
    //   Quantity decrease, same price  → reduce in-place, order keeps FIFO position.
    //   Price change or qty increase   → cancel + re-add at back of the target level's
    //                                    queue (same order_id); fires on_cancelled then
    //                                    on_accepted/on_trade as appropriate.
    // Fires on_rejected(UnknownId) and returns without state change if id is not found.
    void modify(OrderId id, std::optional<Price> new_price,
                std::optional<Quantity> new_qty, Timestamp ts);

    // ── Replay-mode operations ──────────────────────────────────────────────
    // These bypass the matching loop and fire NO event callbacks.
    // Used by the ITCH replay driver to reconstruct historical book state
    // without triggering re-matching (exchanges already handled that).

    // Place a resting order directly on the book without matching.
    void insert_passive(Order* o);

    // Reduce a resting order's remaining quantity in place.
    // Removes the order from the book if the reduction is >= current remaining.
    // Returns false if the id is not found.
    bool reduce_passive(OrderId id, Quantity qty);

    // Remove a resting order from the book without firing any callbacks.
    // Returns false if the id is not found.
    bool delete_passive(OrderId id);

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
