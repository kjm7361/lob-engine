// PriceLevel: FIFO queue of all orders at one price, O(1) append/remove via intrusive list.
#pragma once

#include "order.hpp"

namespace lob {

class PriceLevel {
   public:
    PriceLevel() = default;

    // Can't copy — intrusive pointers point into live Order objects.
    PriceLevel(const PriceLevel&) = delete;
    PriceLevel& operator=(const PriceLevel&) = delete;
    PriceLevel(PriceLevel&&) = default;
    PriceLevel& operator=(PriceLevel&&) = default;

    void append(Order* o) noexcept;  // new order goes to the back (time priority)
    void remove(Order* o) noexcept;  // O(1) because we rewire neighbours directly

    [[nodiscard]] bool empty() const noexcept { return head_ == nullptr; }
    [[nodiscard]] Order* front() noexcept { return head_; }
    [[nodiscard]] const Order* front() const noexcept { return head_; }
    [[nodiscard]] Quantity total_quantity() const noexcept { return total_quantity_; }
    [[nodiscard]] uint64_t order_count() const noexcept { return order_count_; }

    // Patch an order's quantity in-place without moving it in the queue.
    // Quantity decreases keep time priority; this is how exchanges implement that rule.
    void adjust_quantity(Order* o, Quantity new_qty) noexcept;

   private:
    Order*   head_{nullptr};
    Order*   tail_{nullptr};
    Quantity total_quantity_{Quantity{0}};
    uint64_t order_count_{0};
};

}  // namespace lob
