// MatchingEngine: limit and market order routing, FIFO price-time matching, trade event dispatch.
#include "lob/matching_engine.hpp"

#include <cassert>

namespace lob {

MatchingEngine::MatchingEngine(EventHandler handler)
    : handler_(std::move(handler)) {}

void MatchingEngine::submit(Order* o) {
    if (to_uint(o->remaining_quantity) == 0) {
        if (handler_.on_rejected) {
            handler_.on_rejected({o->id, RejectReason::InvalidQuantity});
        }
        return;
    }
    if (o->type == OrderType::Market) {
        match_market(o);
    } else {
        match_limit(o);
    }
}

void MatchingEngine::cancel(OrderId id) {
    Order* o = book_.find(id);
    if (o == nullptr) {
        if (handler_.on_rejected) {
            handler_.on_rejected({id, RejectReason::UnknownId});
        }
        return;
    }
    Quantity remaining = o->remaining_quantity;
    book_.cancel_order(id);
    if (handler_.on_cancelled) {
        handler_.on_cancelled({id, remaining});
    }
}

void MatchingEngine::match_limit(Order* taker) {
    auto& opposite = (taker->side == Side::Buy) ? book_.asks() : book_.bids();

    while (to_uint(taker->remaining_quantity) > 0 && !opposite.empty()) {
        auto best_it = (taker->side == Side::Buy) ? opposite.begin()
                                                   : std::prev(opposite.end());
        Price level_price = best_it->first;

        bool crosses = (taker->side == Side::Buy)
                           ? (to_int(taker->price) >= to_int(level_price))
                           : (to_int(taker->price) <= to_int(level_price));
        if (!crosses) break;

        PriceLevel& level = best_it->second;

        // Within each level, fill the oldest order first (FIFO).
        while (to_uint(taker->remaining_quantity) > 0 && !level.empty()) {
            Order*   maker    = level.front();
            Quantity fill_qty = Quantity{
                std::min(to_uint(taker->remaining_quantity),
                         to_uint(maker->remaining_quantity))};
            execute_fill(maker, taker, fill_qty, taker->timestamp);

            if (to_uint(maker->remaining_quantity) == 0) {
                book_.cancel_order(maker->id);
            }
        }
    }

    // Any unfilled remainder rests on the book.
    if (to_uint(taker->remaining_quantity) > 0) {
        if (handler_.on_accepted) {
            handler_.on_accepted(
                {taker->id, taker->side, taker->price, taker->remaining_quantity});
        }
        book_.add_order(taker);
    }
}

void MatchingEngine::match_market(Order* taker) {
    auto& opposite = (taker->side == Side::Buy) ? book_.asks() : book_.bids();

    // Same walk as limit matching, but no price check — take whatever's available.
    while (to_uint(taker->remaining_quantity) > 0 && !opposite.empty()) {
        auto best_it = (taker->side == Side::Buy) ? opposite.begin()
                                                   : std::prev(opposite.end());
        PriceLevel& level = best_it->second;

        while (to_uint(taker->remaining_quantity) > 0 && !level.empty()) {
            Order*   maker    = level.front();
            Quantity fill_qty = Quantity{
                std::min(to_uint(taker->remaining_quantity),
                         to_uint(maker->remaining_quantity))};
            execute_fill(maker, taker, fill_qty, taker->timestamp);

            if (to_uint(maker->remaining_quantity) == 0) {
                book_.cancel_order(maker->id);
            }
        }
    }

    // Market orders can't rest — reject whatever didn't fill.
    if (to_uint(taker->remaining_quantity) > 0) {
        if (handler_.on_rejected) {
            handler_.on_rejected({taker->id, RejectReason::MarketOrderNoLiquidity});
        }
    }
}

void MatchingEngine::modify(OrderId id, std::optional<Price> new_price,
                            std::optional<Quantity> new_qty, Timestamp ts) {
    Order* o = book_.find(id);
    if (o == nullptr) {
        if (handler_.on_rejected) handler_.on_rejected({id, RejectReason::UnknownId});
        return;
    }

    Price    target_price = new_price.value_or(o->price);
    Quantity target_qty   = new_qty.value_or(o->remaining_quantity);

    bool price_changed  = (target_price != o->price);
    bool qty_increased  = to_uint(target_qty) > to_uint(o->remaining_quantity);
    bool loses_priority = price_changed || qty_increased;

    if (!loses_priority) {
        // Quantity decrease or no-op: patch in place, FIFO position preserved.
        // book_.modify_order handles the level aggregate update.
        book_.modify_order(id, std::nullopt, target_qty, nullptr);
        return;
    }

    // Loses queue position: cancel the old slot, then re-add through the normal
    // matching path (submit) so crossing prices execute immediately.
    Quantity old_remaining = o->remaining_quantity;
    book_.cancel_order(id);  // unlinks o from the level; o pointer stays valid
    if (handler_.on_cancelled) handler_.on_cancelled({id, old_remaining});

    // Reuse the same Order object — book_.cancel_order zeroed prev/next already.
    o->price              = target_price;
    o->quantity           = target_qty;
    o->remaining_quantity = target_qty;
    o->timestamp          = ts;
    o->prev               = nullptr;
    o->next               = nullptr;
    submit(o);  // fires on_accepted if it rests, or on_trade events if it crosses
}

void MatchingEngine::insert_passive(Order* o) {
    book_.add_order(o);
}

bool MatchingEngine::reduce_passive(OrderId id, Quantity qty) {
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

bool MatchingEngine::delete_passive(OrderId id) {
    return book_.cancel_order(id);
}

void MatchingEngine::execute_fill(Order* maker, Order* taker, Quantity qty,
                                  Timestamp ts) {
    assert(to_uint(qty) > 0);
    assert(to_uint(qty) <= to_uint(maker->remaining_quantity));
    assert(to_uint(qty) <= to_uint(taker->remaining_quantity));

    // adjust_quantity updates both the maker's remaining_quantity and the level's
    // running total atomically. On a full fill this sets maker qty to zero, so the
    // subsequent cancel_order → remove() subtracts zero — correct, not a bug.
    Quantity new_maker_qty =
        Quantity{to_uint(maker->remaining_quantity) - to_uint(qty)};
    auto& side_map = (maker->side == Side::Buy) ? book_.bids() : book_.asks();
    auto  it       = side_map.find(maker->price);
    if (it != side_map.end()) {
        it->second.adjust_quantity(maker, new_maker_qty);
    }

    taker->remaining_quantity =
        Quantity{to_uint(taker->remaining_quantity) - to_uint(qty)};

    if (handler_.on_trade) {
        handler_.on_trade({maker->id, taker->id, maker->price, qty, ts});
    }
}

}  // namespace lob
