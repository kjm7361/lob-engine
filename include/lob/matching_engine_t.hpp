// Zero-overhead templated matching engine.
// Replace std::function<> callbacks (virtual dispatch + potential heap alloc)
// with a concrete Handler type so every event dispatch is a direct call the
// compiler can inline.  Use this on latency-sensitive paths; keep MatchingEngine
// (matching_engine.hpp) when you need runtime-configurable callbacks.
#pragma once

#include <cassert>
#include <optional>

#include "matching_engine.hpp"  // TradeEvent, OrderAccepted, OrderCancelled, OrderRejected, RejectReason
#include "order_book.hpp"
#include "types.hpp"

namespace lob {

// ── C++20 concept ─────────────────────────────────────────────────────────────

// Any struct/class with these four methods satisfies HandlerConcept.
// Methods may be static or non-static; they must accept the event by const-ref.
template <typename H>
concept HandlerConcept =
    requires(H& h,
             const TradeEvent&     te,
             const OrderAccepted&  oa,
             const OrderCancelled& oc,
             const OrderRejected&  rej) {
        h.on_trade(te);
        h.on_accepted(oa);
        h.on_cancelled(oc);
        h.on_rejected(rej);
    };

// ── Built-in handlers ─────────────────────────────────────────────────────────

// Drop every event on the floor — zero overhead when the compiler can see the
// call sites (all four are trivially inlinable to nothing).
struct NullHandler {
    static void on_trade    (const TradeEvent&)     noexcept {}
    static void on_accepted (const OrderAccepted&)  noexcept {}
    static void on_cancelled(const OrderCancelled&) noexcept {}
    static void on_rejected (const OrderRejected&)  noexcept {}
};
static_assert(HandlerConcept<NullHandler>);

// ── MatchingEngineT<H> ────────────────────────────────────────────────────────

// Identical semantics to MatchingEngine; see matching_engine.hpp for the
// contract.  The full match logic is in this header so the compiler can
// instantiate (and inline) everything when the translation unit is compiled.
template <HandlerConcept H = NullHandler>
class MatchingEngineT {
   public:
    explicit MatchingEngineT(H handler = H{}) : handler_(std::move(handler)) {}

    // Not copyable — OrderBook holds raw Order pointers from the caller.
    MatchingEngineT(const MatchingEngineT&)            = delete;
    MatchingEngineT& operator=(const MatchingEngineT&) = delete;
    MatchingEngineT(MatchingEngineT&&)                 = default;
    MatchingEngineT& operator=(MatchingEngineT&&)      = default;

    void submit(Order* o);
    void cancel(OrderId id);
    void modify(OrderId id, std::optional<Price> new_price,
                std::optional<Quantity> new_qty, Timestamp ts);

    // Replay-mode — no matching, no callbacks.
    void insert_passive(Order* o) { book_.add_order(o); }
    bool reduce_passive(OrderId id, Quantity qty);
    bool delete_passive(OrderId id) { return book_.cancel_order(id); }

    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
    [[nodiscard]] OrderBook&       book() noexcept       { return book_; }

    // Expose the stored handler for inspection in tests.
    [[nodiscard]] const H& handler() const noexcept { return handler_; }
    [[nodiscard]] H&       handler() noexcept       { return handler_; }

   private:
    OrderBook book_;
    H         handler_;

    void match_limit (Order* taker);
    void match_market(Order* taker);
    void execute_fill(Order* maker, Order* taker, Quantity qty, Timestamp ts);
};

// ── Implementation (must live in this header for template instantiation) ──────

template <HandlerConcept H>
void MatchingEngineT<H>::submit(Order* o) {
    if (to_uint(o->remaining_quantity) == 0) {
        handler_.on_rejected({o->id, RejectReason::InvalidQuantity});
        return;
    }
    if (o->type == OrderType::Market)
        match_market(o);
    else
        match_limit(o);
}

template <HandlerConcept H>
void MatchingEngineT<H>::cancel(OrderId id) {
    Order* o = book_.find(id);
    if (o == nullptr) {
        handler_.on_rejected({id, RejectReason::UnknownId});
        return;
    }
    Quantity remaining = o->remaining_quantity;
    book_.cancel_order(id);
    handler_.on_cancelled({id, remaining});
}

template <HandlerConcept H>
void MatchingEngineT<H>::modify(OrderId id, std::optional<Price> new_price,
                                 std::optional<Quantity> new_qty, Timestamp ts) {
    Order* o = book_.find(id);
    if (o == nullptr) {
        handler_.on_rejected({id, RejectReason::UnknownId});
        return;
    }

    Price    target_price = new_price.value_or(o->price);
    Quantity target_qty   = new_qty.value_or(o->remaining_quantity);

    bool loses_priority = (target_price != o->price) ||
                          (to_uint(target_qty) > to_uint(o->remaining_quantity));

    if (!loses_priority) {
        book_.modify_order(id, std::nullopt, target_qty, nullptr);
        return;
    }

    Quantity old_remaining = o->remaining_quantity;
    book_.cancel_order(id);
    handler_.on_cancelled({id, old_remaining});

    o->price              = target_price;
    o->quantity           = target_qty;
    o->remaining_quantity = target_qty;
    o->timestamp          = ts;
    o->prev               = nullptr;
    o->next               = nullptr;
    submit(o);
}

template <HandlerConcept H>
bool MatchingEngineT<H>::reduce_passive(OrderId id, Quantity qty) {
    Order* o = book_.find(id);
    if (o == nullptr) return false;

    uint64_t rem = to_uint(o->remaining_quantity);
    uint64_t red = to_uint(qty);

    if (red >= rem) {
        book_.cancel_order(id);
    } else {
        auto& side_map = (o->side == Side::Buy) ? book_.bids() : book_.asks();
        auto  it       = side_map.find(o->price);
        if (it != side_map.end())
            it->second.adjust_quantity(o, Quantity{rem - red});
    }
    return true;
}

template <HandlerConcept H>
void MatchingEngineT<H>::match_limit(Order* taker) {
    auto& opposite = (taker->side == Side::Buy) ? book_.asks() : book_.bids();

    while (to_uint(taker->remaining_quantity) > 0 && !opposite.empty()) {
        auto  best_it    = (taker->side == Side::Buy)
                               ? opposite.begin()
                               : std::prev(opposite.end());
        Price level_price = best_it->first;

        bool crosses = (taker->side == Side::Buy)
                           ? (to_int(taker->price) >= to_int(level_price))
                           : (to_int(taker->price) <= to_int(level_price));
        if (!crosses) break;

        PriceLevel& level = best_it->second;

        while (to_uint(taker->remaining_quantity) > 0 && !level.empty()) {
            Order*   maker    = level.front();
            Quantity fill_qty = Quantity{
                std::min(to_uint(taker->remaining_quantity),
                         to_uint(maker->remaining_quantity))};
            execute_fill(maker, taker, fill_qty, taker->timestamp);
            if (to_uint(maker->remaining_quantity) == 0)
                book_.cancel_order(maker->id);
        }
    }

    if (to_uint(taker->remaining_quantity) > 0) {
        handler_.on_accepted(
            {taker->id, taker->side, taker->price, taker->remaining_quantity});
        book_.add_order(taker);
    }
}

template <HandlerConcept H>
void MatchingEngineT<H>::match_market(Order* taker) {
    auto& opposite = (taker->side == Side::Buy) ? book_.asks() : book_.bids();

    while (to_uint(taker->remaining_quantity) > 0 && !opposite.empty()) {
        auto best_it = (taker->side == Side::Buy)
                           ? opposite.begin()
                           : std::prev(opposite.end());
        PriceLevel& level = best_it->second;

        while (to_uint(taker->remaining_quantity) > 0 && !level.empty()) {
            Order*   maker    = level.front();
            Quantity fill_qty = Quantity{
                std::min(to_uint(taker->remaining_quantity),
                         to_uint(maker->remaining_quantity))};
            execute_fill(maker, taker, fill_qty, taker->timestamp);
            if (to_uint(maker->remaining_quantity) == 0)
                book_.cancel_order(maker->id);
        }
    }

    if (to_uint(taker->remaining_quantity) > 0)
        handler_.on_rejected({taker->id, RejectReason::MarketOrderNoLiquidity});
}

template <HandlerConcept H>
void MatchingEngineT<H>::execute_fill(Order* maker, Order* taker, Quantity qty,
                                       Timestamp ts) {
    assert(to_uint(qty) > 0);
    assert(to_uint(qty) <= to_uint(maker->remaining_quantity));
    assert(to_uint(qty) <= to_uint(taker->remaining_quantity));

    Quantity new_maker_qty =
        Quantity{to_uint(maker->remaining_quantity) - to_uint(qty)};

    auto& side_map = (maker->side == Side::Buy) ? book_.bids() : book_.asks();
    auto  it       = side_map.find(maker->price);
    if (it != side_map.end())
        it->second.adjust_quantity(maker, new_maker_qty);

    taker->remaining_quantity =
        Quantity{to_uint(taker->remaining_quantity) - to_uint(qty)};

    handler_.on_trade({maker->id, taker->id, maker->price, qty, ts});
}

}  // namespace lob
