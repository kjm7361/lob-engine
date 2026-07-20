#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lob/itch/messages.hpp"
#include "lob/matching_engine.hpp"

namespace lob::replay {

struct Config {
    // Only reconstruct books for these ticker symbols.
    // Empty set means "all symbols".
    std::unordered_set<std::string> symbols;

    // Stop processing after this many non-directory messages (0 = no limit).
    uint64_t message_limit{0};
};

struct ReplayStats {
    uint64_t messages_processed{0};
    uint64_t orders_added{0};
    uint64_t orders_executed{0};
    uint64_t orders_cancelled{0};
    uint64_t orders_deleted{0};
    uint64_t orders_replaced{0};
    uint64_t unknown_refs{0};  // E/C/X/D/U for refs not in the map
};

struct BookSnapshot {
    std::string          symbol;
    std::optional<Price> best_bid;
    std::optional<Price> best_ask;
    uint64_t             bid_depth{0};  // total qty at best bid level
    uint64_t             ask_depth{0};  // total qty at best ask level
};

// Reconstructs one MatchingEngine per symbol from an ITCH 5.0 file.
// All historical orders are inserted passively — no re-matching.
class ReplayEngine {
   public:
    explicit ReplayEngine(Config cfg = {});

    // Parse path (plain or gzip) and replay all messages.
    ReplayStats run(const std::string& path);

    // Snapshot the current book state for every tracked symbol.
    std::vector<BookSnapshot> snapshots() const;

    // Access the raw OrderBook for one symbol (nullptr if not tracked).
    [[nodiscard]] const OrderBook* book(const std::string& symbol) const;

   private:
    Config cfg_;

    // ── Per-symbol book state ─────────────────────────────────────────────────

    struct SymbolState {
        std::string    symbol;
        MatchingEngine engine;
        std::vector<std::unique_ptr<Order>> pool;  // owns all Order objects

        explicit SymbolState(std::string sym)
            : symbol(std::move(sym)), engine(EventHandler{}) {}
    };

    // Keyed by the stock_locate code assigned in R messages.
    std::unordered_map<uint16_t, SymbolState*>                     by_locate_;
    // Owns the SymbolState; keyed by ticker string.
    std::unordered_map<std::string, std::unique_ptr<SymbolState>>  by_symbol_;

    // ── ITCH ref → Order mapping ──────────────────────────────────────────────
    // ITCH ref numbers are unique only within a stock_locate, so we key on both.

    struct RefKey {
        uint16_t locate;
        uint64_t ref;
        bool operator==(const RefKey& o) const noexcept {
            return locate == o.locate && ref == o.ref;
        }
    };
    struct RefKeyHash {
        size_t operator()(const RefKey& k) const noexcept {
            return std::hash<uint64_t>{}(
                (static_cast<uint64_t>(k.locate) << 48) ^ k.ref);
        }
    };
    std::unordered_map<RefKey, Order*, RefKeyHash> ref_map_;

    // ── Helpers ───────────────────────────────────────────────────────────────

    SymbolState* state_for(uint16_t locate) const noexcept;
    Order* alloc_order(SymbolState& s, uint64_t itch_ref, Side side,
                       Quantity qty, Price price, Timestamp ts);

    // ── Message handlers ──────────────────────────────────────────────────────

    void handle_stock_directory(const itch::MsgStockDirectory& m);
    void handle_add_order(const itch::MsgAddOrder& m, ReplayStats& st);
    void handle_order_executed(const itch::MsgOrderExecuted& m, ReplayStats& st);
    void handle_order_executed_with_price(
        const itch::MsgOrderExecutedWithPrice& m, ReplayStats& st);
    void handle_order_cancel(const itch::MsgOrderCancel& m, ReplayStats& st);
    void handle_order_delete(const itch::MsgOrderDelete& m, ReplayStats& st);
    void handle_order_replace(const itch::MsgOrderReplace& m, ReplayStats& st);

    // Reduce an order's qty by exec_qty; erase from ref_map if fully consumed.
    // Returns true if the order was removed from the book.
    bool reduce_and_erase(const RefKey& key,
                          std::unordered_map<RefKey, Order*, RefKeyHash>::iterator it,
                          Quantity exec_qty);
};

}  // namespace lob::replay
