#pragma once

#include "order.hpp"

namespace lob {

// All orders at a single price, kept in FIFO order via an intrusive
// doubly-linked list.  append() and remove() are both O(1).
class PriceLevel {
   public:
    PriceLevel() = default;

    // Non-copyable: the intrusive pointers point into live Order objects.
    PriceLevel(const PriceLevel&) = delete;
    PriceLevel& operator=(const PriceLevel&) = delete;
    PriceLevel(PriceLevel&&) = default;
    PriceLevel& operator=(PriceLevel&&) = default;

    // Append order to the tail of the FIFO queue.
    void append(Order* o) noexcept;

    // Remove an arbitrary order.  O(1) because the list is doubly-linked.
    void remove(Order* o) noexcept;

    [[nodiscard]] bool empty() const noexcept { return head_ == nullptr; }
    [[nodiscard]] Order* front() noexcept { return head_; }
    [[nodiscard]] const Order* front() const noexcept { return head_; }

    [[nodiscard]] Quantity total_quantity() const noexcept {
        return total_quantity_;
    }
    [[nodiscard]] uint64_t order_count() const noexcept { return order_count_; }

    // Update an order's remaining_quantity in-place (no position change —
    // FIFO priority is preserved).  Used for quantity-decrease modifications.
    void adjust_quantity(Order* o, Quantity new_qty) noexcept;

   private:
    Order*   head_{nullptr};
    Order*   tail_{nullptr};
    Quantity total_quantity_{Quantity{0}};
    uint64_t order_count_{0};
};

}  // namespace lob
