#pragma once

#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "price_level.hpp"
#include "types.hpp"

namespace lob {

struct DepthLevel {
    Price    price;
    Quantity total_quantity;
    uint64_t order_count;
};

// std::map<Price, PriceLevel> gives us O(log N) add/remove and O(1) best
// bid/ask via rbegin()/begin().  Phase 2 will benchmark this against a flat
// array indexed by tick offset; the interface is identical so the swap is
// surgical.
class OrderBook {
   public:
    // Add a resting limit order.  The caller owns the Order object's lifetime.
    void add_order(Order* o);

    // Remove an order from the book.  Returns false if id not found.
    bool cancel_order(OrderId id);

    // Modify price or quantity.
    //   - Quantity decrease  → keeps time priority (in-place update).
    //   - Quantity increase  → loses time priority (cancel + reinsert).
    //   - Price change       → always loses time priority (cancel + reinsert).
    // Returns nullptr if id not found; returns o (possibly reallocated) on
    // success.  When priority is lost, the old Order* is removed and a new
    // one must be provided via new_order.
    //
    // Convention: if modify requires a reinsert (priority lost), the caller
    // passes a pre-allocated new_order.  If modify is in-place, new_order is
    // unused and may be nullptr.  Return value: the Order* now live on the book.
    Order* modify_order(OrderId id, std::optional<Price> new_price,
                        std::optional<Quantity> new_qty, Order* new_order);

    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;

    // Returns up to n levels from best price outward.
    [[nodiscard]] std::vector<DepthLevel> bid_depth(size_t n) const;
    [[nodiscard]] std::vector<DepthLevel> ask_depth(size_t n) const;

    // Look up live order by id (returns nullptr if not found).
    [[nodiscard]] Order* find(OrderId id) noexcept;
    [[nodiscard]] const Order* find(OrderId id) const noexcept;

    // Access to price-level maps for the matching engine.
    [[nodiscard]] std::map<Price, PriceLevel>& bids() noexcept { return bids_; }
    [[nodiscard]] std::map<Price, PriceLevel>& asks() noexcept { return asks_; }

   private:
    // bids: descending price (best bid = rbegin)
    // asks: ascending price  (best ask = begin)
    std::map<Price, PriceLevel> bids_;
    std::map<Price, PriceLevel> asks_;

    // O(1) lookup by id for cancel/modify.
    std::unordered_map<uint64_t, Order*> index_;

    void insert_into_side(Order* o);
    void remove_from_side(Order* o);
    void erase_level_if_empty(Side side, Price price);
};

}  // namespace lob
