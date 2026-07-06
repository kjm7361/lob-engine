#include "lob/order_book.hpp"

#include <cassert>

namespace lob {

// ── PriceLevel ────────────────────────────────────────────────────────────────

void PriceLevel::append(Order* o) noexcept {
    o->prev = tail_;
    o->next = nullptr;
    if (tail_) {
        tail_->next = o;
    } else {
        head_ = o;
    }
    tail_ = o;
    total_quantity_ = Quantity{to_uint(total_quantity_) + to_uint(o->remaining_quantity)};
    ++order_count_;
}

void PriceLevel::remove(Order* o) noexcept {
    if (o->prev) {
        o->prev->next = o->next;
    } else {
        head_ = o->next;
    }
    if (o->next) {
        o->next->prev = o->prev;
    } else {
        tail_ = o->prev;
    }
    o->prev = nullptr;
    o->next = nullptr;
    total_quantity_ = Quantity{to_uint(total_quantity_) - to_uint(o->remaining_quantity)};
    --order_count_;
}

void PriceLevel::adjust_quantity(Order* o, Quantity new_qty) noexcept {
    uint64_t old_q = to_uint(o->remaining_quantity);
    uint64_t new_q = to_uint(new_qty);
    uint64_t agg   = to_uint(total_quantity_);
    // Unsigned-safe delta: avoid wrapping when new_q < old_q.
    if (new_q >= old_q) {
        total_quantity_ = Quantity{agg + (new_q - old_q)};
    } else {
        total_quantity_ = Quantity{agg - (old_q - new_q)};
    }
    o->remaining_quantity = new_qty;
}

// ── OrderBook internals ───────────────────────────────────────────────────────

void OrderBook::insert_into_side(Order* o) {
    auto& side_map = (o->side == Side::Buy) ? bids_ : asks_;
    side_map[o->price].append(o);
    index_[to_uint(o->id)] = o;
}

void OrderBook::remove_from_side(Order* o) {
    auto& side_map = (o->side == Side::Buy) ? bids_ : asks_;
    auto  it       = side_map.find(o->price);
    if (it != side_map.end()) {
        it->second.remove(o);
        if (it->second.empty()) {
            side_map.erase(it);
        }
    }
    index_.erase(to_uint(o->id));
}

// ── Public API ────────────────────────────────────────────────────────────────

void OrderBook::add_order(Order* o) { insert_into_side(o); }

bool OrderBook::cancel_order(OrderId id) {
    auto it = index_.find(to_uint(id));
    if (it == index_.end()) return false;
    remove_from_side(it->second);
    return true;
}

Order* OrderBook::modify_order(OrderId id, std::optional<Price> new_price,
                               std::optional<Quantity> new_qty,
                               Order*                  new_order) {
    auto it = index_.find(to_uint(id));
    if (it == index_.end()) return nullptr;

    Order* existing = it->second;

    Price    target_price = new_price.value_or(existing->price);
    Quantity target_qty   = new_qty.value_or(existing->remaining_quantity);

    bool price_changed  = (target_price != existing->price);
    bool qty_increased  = (to_uint(target_qty) > to_uint(existing->remaining_quantity));
    bool loses_priority = price_changed || qty_increased;

    if (loses_priority) {
        // Cancel + reinsert at tail.  Caller must supply new_order.
        assert(new_order != nullptr);
        *new_order                    = *existing;
        new_order->price              = target_price;
        new_order->quantity           = target_qty;
        new_order->remaining_quantity = target_qty;
        new_order->prev               = nullptr;
        new_order->next               = nullptr;
        remove_from_side(existing);
        insert_into_side(new_order);
        return new_order;
    } else {
        // Quantity decrease: patch in-place so position in queue is unchanged.
        auto& side_map = (existing->side == Side::Buy) ? bids_ : asks_;
        side_map.at(existing->price).adjust_quantity(existing, target_qty);
        return existing;
    }
}

// ── best bid / ask ────────────────────────────────────────────────────────────

std::optional<Price> OrderBook::best_bid() const noexcept {
    if (bids_.empty()) return std::nullopt;
    return bids_.rbegin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

// ── depth snapshots ───────────────────────────────────────────────────────────

std::vector<DepthLevel> OrderBook::bid_depth(size_t n) const {
    std::vector<DepthLevel> out;
    out.reserve(n);
    for (auto it = bids_.rbegin(); it != bids_.rend() && out.size() < n; ++it) {
        out.push_back(
            {it->first, it->second.total_quantity(), it->second.order_count()});
    }
    return out;
}

std::vector<DepthLevel> OrderBook::ask_depth(size_t n) const {
    std::vector<DepthLevel> out;
    out.reserve(n);
    for (auto it = asks_.begin(); it != asks_.end() && out.size() < n; ++it) {
        out.push_back(
            {it->first, it->second.total_quantity(), it->second.order_count()});
    }
    return out;
}

// ── lookup ────────────────────────────────────────────────────────────────────

Order* OrderBook::find(OrderId id) noexcept {
    auto it = index_.find(to_uint(id));
    return it == index_.end() ? nullptr : it->second;
}

const Order* OrderBook::find(OrderId id) const noexcept {
    auto it = index_.find(to_uint(id));
    return it == index_.end() ? nullptr : it->second;
}

}  // namespace lob
