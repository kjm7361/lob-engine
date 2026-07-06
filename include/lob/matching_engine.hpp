#pragma once

#include <functional>
#include <string>

#include "order_book.hpp"
#include "types.hpp"

namespace lob {

// ── Events ────────────────────────────────────────────────────────────────────

struct TradeEvent {
    OrderId   maker_id;   // resting order
    OrderId   taker_id;   // aggressor
    Price     price;      // maker's price (trades at resting price)
    Quantity  quantity;   // shares/contracts exchanged
    Timestamp timestamp;
};

struct OrderAccepted {
    OrderId id;
    Side    side;
    Price   price;    // zero for market orders
    Quantity quantity;
};

struct OrderCancelled {
    OrderId  id;
    Quantity remaining_quantity;
};

enum class RejectReason : uint8_t {
    UnknownId,
    InvalidQuantity,  // zero or overflow
    MarketOrderNoLiquidity,
};

struct OrderRejected {
    OrderId      id;
    RejectReason reason;
};

// ── Callback bundle ───────────────────────────────────────────────────────────
// std::function for Phase 1.  Phase 2 will template MatchingEngine on a
// handler type (CRTP or concept) to eliminate the virtual dispatch overhead.

struct EventHandler {
    std::function<void(const TradeEvent&)>     on_trade;
    std::function<void(const OrderAccepted&)>  on_accepted;
    std::function<void(const OrderCancelled&)> on_cancelled;
    std::function<void(const OrderRejected&)>  on_rejected;
};

// ── MatchingEngine ────────────────────────────────────────────────────────────

class MatchingEngine {
   public:
    explicit MatchingEngine(EventHandler handler);

    // Submit a new order.  Takes ownership of the Order object.
    void submit(Order* o);

    // Cancel a resting order by id.
    void cancel(OrderId id);

    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
    [[nodiscard]] OrderBook&       book() noexcept { return book_; }

   private:
    OrderBook    book_;
    EventHandler handler_;

    void match_limit(Order* taker);
    void match_market(Order* taker);

    // Execute a fill between a resting maker and an aggressor taker.
    // Decrements remaining_quantity on both sides and fires a TradeEvent.
    void execute_fill(Order* maker, Order* taker, Quantity qty,
                      Timestamp ts);
};

}  // namespace lob
