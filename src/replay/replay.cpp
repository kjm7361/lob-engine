#include "lob/replay/replay.hpp"

#include <string>

#include "lob/itch/parser.hpp"

namespace lob::replay {

// ── Constructor ───────────────────────────────────────────────────────────────

ReplayEngine::ReplayEngine(Config cfg) : cfg_(std::move(cfg)) {}

// ── Public interface ──────────────────────────────────────────────────────────

ReplayStats ReplayEngine::run(const std::string& path) {
    by_locate_.clear();
    by_symbol_.clear();
    ref_map_.clear();

    ReplayStats stats{};
    bool        stop = false;

    itch::ItchHandler h;

    // R — register symbol ↔ locate mapping (doesn't count toward message_limit)
    h.on_stock_directory = [&](const itch::MsgStockDirectory& m) {
        handle_stock_directory(m);
    };

    h.on_add_order = [&](const itch::MsgAddOrder& m) {
        if (stop) return;
        handle_add_order(m, stats);
        if (cfg_.message_limit > 0 && ++stats.messages_processed >= cfg_.message_limit)
            stop = true;
    };

    h.on_order_executed = [&](const itch::MsgOrderExecuted& m) {
        if (stop) return;
        handle_order_executed(m, stats);
        if (cfg_.message_limit > 0 && ++stats.messages_processed >= cfg_.message_limit)
            stop = true;
    };

    h.on_order_executed_with_price = [&](const itch::MsgOrderExecutedWithPrice& m) {
        if (stop) return;
        handle_order_executed_with_price(m, stats);
        if (cfg_.message_limit > 0 && ++stats.messages_processed >= cfg_.message_limit)
            stop = true;
    };

    h.on_order_cancel = [&](const itch::MsgOrderCancel& m) {
        if (stop) return;
        handle_order_cancel(m, stats);
        if (cfg_.message_limit > 0 && ++stats.messages_processed >= cfg_.message_limit)
            stop = true;
    };

    h.on_order_delete = [&](const itch::MsgOrderDelete& m) {
        if (stop) return;
        handle_order_delete(m, stats);
        if (cfg_.message_limit > 0 && ++stats.messages_processed >= cfg_.message_limit)
            stop = true;
    };

    h.on_order_replace = [&](const itch::MsgOrderReplace& m) {
        if (stop) return;
        handle_order_replace(m, stats);
        if (cfg_.message_limit > 0 && ++stats.messages_processed >= cfg_.message_limit)
            stop = true;
    };

    itch::parse_file(path, h);
    return stats;
}

std::vector<BookSnapshot> ReplayEngine::snapshots() const {
    std::vector<BookSnapshot> result;
    result.reserve(by_symbol_.size());
    for (const auto& [sym, state] : by_symbol_) {
        OrderBook& bk = state->engine.book();  // non-const: bids()/asks() are non-const
        BookSnapshot snap;
        snap.symbol   = sym;
        snap.best_bid = bk.best_bid();
        snap.best_ask = bk.best_ask();
        if (snap.best_bid)
            snap.bid_depth = to_uint(bk.bids().rbegin()->second.total_quantity());
        if (snap.best_ask)
            snap.ask_depth = to_uint(bk.asks().begin()->second.total_quantity());
        result.push_back(std::move(snap));
    }
    return result;
}

const OrderBook* ReplayEngine::book(const std::string& symbol) const {
    auto it = by_symbol_.find(symbol);
    return (it == by_symbol_.end()) ? nullptr : &it->second->engine.book();
}

// ── Private helpers ───────────────────────────────────────────────────────────

ReplayEngine::SymbolState* ReplayEngine::state_for(uint16_t locate) const noexcept {
    auto it = by_locate_.find(locate);
    return (it == by_locate_.end()) ? nullptr : it->second;
}

Order* ReplayEngine::alloc_order(SymbolState& s, uint64_t itch_ref,
                                  Side side, Quantity qty, Price price,
                                  Timestamp ts) {
    auto o                = std::make_unique<Order>();
    o->id                 = OrderId{itch_ref};
    o->side               = side;
    o->type               = OrderType::Limit;
    o->price              = price;
    o->quantity           = qty;
    o->remaining_quantity = qty;
    o->timestamp          = ts;
    Order* raw = o.get();
    s.pool.push_back(std::move(o));
    return raw;
}

bool ReplayEngine::reduce_and_erase(
        const RefKey& key,
        std::unordered_map<RefKey, Order*, RefKeyHash>::iterator it,
        Quantity exec_qty) {
    SymbolState* s = state_for(key.locate);
    if (s == nullptr) return false;
    s->engine.reduce_passive(it->second->id, exec_qty);
    bool gone = (s->engine.book().find(it->second->id) == nullptr);
    if (gone) ref_map_.erase(it);
    return gone;
}

// ── Message handlers ──────────────────────────────────────────────────────────

void ReplayEngine::handle_stock_directory(const itch::MsgStockDirectory& m) {
    std::string sym{itch::trim_stock(m.stock)};

    if (!cfg_.symbols.empty() && cfg_.symbols.find(sym) == cfg_.symbols.end())
        return;

    auto sit = by_symbol_.find(sym);
    if (sit == by_symbol_.end()) {
        auto state = std::make_unique<SymbolState>(sym);
        SymbolState* ptr = state.get();
        by_symbol_.emplace(sym, std::move(state));
        by_locate_.emplace(m.hdr.stock_locate, ptr);
    } else {
        // Re-registration — update (or add) the locate mapping.
        by_locate_[m.hdr.stock_locate] = sit->second.get();
    }
}

void ReplayEngine::handle_add_order(const itch::MsgAddOrder& m, ReplayStats& st) {
    SymbolState* s = state_for(m.hdr.stock_locate);
    if (s == nullptr) return;

    Side      side  = (m.side == 'B') ? Side::Buy : Side::Sell;
    Price     price = Price{static_cast<int64_t>(m.price)};
    Quantity  qty   = Quantity{m.shares};
    Timestamp ts    = Timestamp{m.hdr.timestamp_ns};

    Order* o = alloc_order(*s, m.ref, side, qty, price, ts);
    s->engine.insert_passive(o);
    ref_map_.emplace(RefKey{m.hdr.stock_locate, m.ref}, o);
    ++st.orders_added;
}

void ReplayEngine::handle_order_executed(const itch::MsgOrderExecuted& m,
                                          ReplayStats& st) {
    auto it = ref_map_.find({m.hdr.stock_locate, m.ref});
    if (it == ref_map_.end()) { ++st.unknown_refs; return; }

    reduce_and_erase({m.hdr.stock_locate, m.ref}, it, Quantity{m.executed_shares});
    ++st.orders_executed;
}

void ReplayEngine::handle_order_executed_with_price(
        const itch::MsgOrderExecutedWithPrice& m, ReplayStats& st) {
    auto it = ref_map_.find({m.hdr.stock_locate, m.ref});
    if (it == ref_map_.end()) { ++st.unknown_refs; return; }

    reduce_and_erase({m.hdr.stock_locate, m.ref}, it, Quantity{m.executed_shares});
    ++st.orders_executed;
}

void ReplayEngine::handle_order_cancel(const itch::MsgOrderCancel& m,
                                        ReplayStats& st) {
    auto it = ref_map_.find({m.hdr.stock_locate, m.ref});
    if (it == ref_map_.end()) { ++st.unknown_refs; return; }

    reduce_and_erase({m.hdr.stock_locate, m.ref}, it,
                     Quantity{m.cancelled_shares});
    ++st.orders_cancelled;
}

void ReplayEngine::handle_order_delete(const itch::MsgOrderDelete& m,
                                        ReplayStats& st) {
    auto it = ref_map_.find({m.hdr.stock_locate, m.ref});
    if (it == ref_map_.end()) { ++st.unknown_refs; return; }

    SymbolState* s = state_for(m.hdr.stock_locate);
    if (s != nullptr) s->engine.delete_passive(it->second->id);
    ref_map_.erase(it);
    ++st.orders_deleted;
}

void ReplayEngine::handle_order_replace(const itch::MsgOrderReplace& m,
                                         ReplayStats& st) {
    RefKey orig_key{m.hdr.stock_locate, m.orig_ref};
    auto   it = ref_map_.find(orig_key);
    if (it == ref_map_.end()) { ++st.unknown_refs; return; }

    SymbolState* s = state_for(m.hdr.stock_locate);
    if (s == nullptr) return;

    Side orig_side = it->second->side;

    // Remove the original order from the book.
    s->engine.delete_passive(it->second->id);
    ref_map_.erase(it);

    // Insert the replacement order (same side, new ref, new qty/price).
    Price     new_price = Price{static_cast<int64_t>(m.price)};
    Quantity  new_qty   = Quantity{m.shares};
    Timestamp ts        = Timestamp{m.hdr.timestamp_ns};

    Order* o = alloc_order(*s, m.new_ref, orig_side, new_qty, new_price, ts);
    s->engine.insert_passive(o);
    ref_map_.emplace(RefKey{m.hdr.stock_locate, m.new_ref}, o);
    ++st.orders_replaced;
}

}  // namespace lob::replay
