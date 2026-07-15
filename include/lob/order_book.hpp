// OrderBook: two-sided price ladder (bids/asks) with O(1) best-price lookup and O(log L) add/cancel.
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

// std::map gives sorted iteration and O(1) best bid/ask via rbegin()/begin().
// In practice there are only a few hundred live price levels so the O(log L)
// cost is negligible. Phase 2 will benchmark this against a flat level array.
class OrderBook {
   public:
    // Caller owns the Order's memory; the book just stores the pointer.
    void add_order(Order* o);

    // Returns false if the id doesn't exist.
    bool cancel_order(OrderId id);

    // Modify price or quantity of a resting order.
    // Exchange priority rules: quantity decrease keeps queue position (in-place);
    // quantity increase or price change loses it (cancel + reinsert to back).
    // Pass new_order when priority is lost — the old node is unlinked and this one inserted.
    // Returns nullptr if not found, otherwise the live Order* now on the book.
    Order* modify_order(OrderId id, std::optional<Price> new_price,
                        std::optional<Quantity> new_qty, Order* new_order);

    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;

    [[nodiscard]] std::vector<DepthLevel> bid_depth(size_t n) const;
    [[nodiscard]] std::vector<DepthLevel> ask_depth(size_t n) const;

    // O(1) id lookup used by cancel and modify.
    [[nodiscard]] Order* find(OrderId id) noexcept;
    [[nodiscard]] const Order* find(OrderId id) const noexcept;

    // Direct access so the matching engine can walk levels without going through the public API.
    [[nodiscard]] std::map<Price, PriceLevel>& bids() noexcept { return bids_; }
    [[nodiscard]] std::map<Price, PriceLevel>& asks() noexcept { return asks_; }

   private:
    // bids ascending → rbegin() is best bid. asks ascending → begin() is best ask.
    std::map<Price, PriceLevel> bids_;
    std::map<Price, PriceLevel> asks_;

    std::unordered_map<uint64_t, Order*> index_;  // id → Order* for O(1) cancel/modify

    void insert_into_side(Order* o);
    void remove_from_side(Order* o);
    void erase_level_if_empty(Side side, Price price);
};

}  // namespace lob
