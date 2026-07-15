// Order: POD struct for a single resting order, with intrusive doubly-linked list pointers.
#pragma once

#include "types.hpp"

namespace lob {

// prev/next live inside Order rather than in a separate list node so we can
// remove any order from its price level in O(1) without a search, and with
// one fewer heap allocation per order. Orders can't move in memory while on the book.
struct Order {
    OrderId   id;
    Side      side;
    OrderType type;
    Price     price;
    Quantity  quantity;            // original submitted quantity
    Quantity  remaining_quantity;  // counts down as fills happen
    Timestamp timestamp;           // arrival time in nanoseconds

    Order* prev{nullptr};
    Order* next{nullptr};
};

}  // namespace lob
