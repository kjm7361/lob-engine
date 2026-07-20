// Order: POD struct for a single resting order, with intrusive doubly-linked list pointers.
#pragma once

#include <cstddef>
#include "types.hpp"

namespace lob {

// prev/next live inside Order rather than in a separate list node so we can
// remove any order from its price level in O(1) without a search, and with
// one fewer heap allocation per order. Orders can't move in memory while on the book.
//
// alignas(64): pin each Order to one 64-byte cache line so a read of any field
// never straddles a line boundary, and sequential pool access stays in one stream.
struct alignas(64) Order {
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

static_assert(sizeof(Order) <= 64,
              "Order grew past one cache line — review field layout");
static_assert(alignof(Order) == 64,
              "Order must be 64-byte aligned for cache-line pinning");

}  // namespace lob
