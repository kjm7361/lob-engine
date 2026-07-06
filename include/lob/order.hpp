#pragma once

#include "types.hpp"

namespace lob {

// Intrusive doubly-linked list pointers are embedded directly in Order rather
// than managed by std::list or a separate node allocator.  This means:
//   1. No extra heap allocation per order beyond the Order object itself.
//   2. O(1) removal from a PriceLevel given only an Order* — no search needed.
//   3. Cache-friendly: following prev/next doesn't chase an extra pointer layer.
// In Phase 2 we'll back Orders from a pool allocator so the object allocation
// is also O(1) with no heap fragmentation.
struct Order {
    OrderId   id;
    Side      side;
    OrderType type;
    Price     price;
    Quantity  quantity;            // original submitted quantity
    Quantity  remaining_quantity;  // decremented by fills
    Timestamp timestamp;           // submission time in nanoseconds

    // Intrusive list linkage within a PriceLevel.
    Order* prev{nullptr};
    Order* next{nullptr};
};

}  // namespace lob
