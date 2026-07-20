#include "lob/matching_engine_t.hpp"

#include <vector>

#include <gtest/gtest.h>

using namespace lob;

// ── Concrete test handler ─────────────────────────────────────────────────────

struct TestHandler {
    std::vector<TradeEvent>     trades;
    std::vector<OrderAccepted>  accepted;
    std::vector<OrderCancelled> cancelled;
    std::vector<OrderRejected>  rejected;

    void on_trade    (const TradeEvent&     e) { trades.push_back(e);    }
    void on_accepted (const OrderAccepted&  e) { accepted.push_back(e);  }
    void on_cancelled(const OrderCancelled& e) { cancelled.push_back(e); }
    void on_rejected (const OrderRejected&  e) { rejected.push_back(e);  }
};

static_assert(HandlerConcept<TestHandler>);

// ── Helpers ───────────────────────────────────────────────────────────────────

static Order make(uint64_t id, Side side, OrderType type,
                  int64_t price, uint64_t qty, uint64_t ts = 0) {
    Order o{};
    o.id                 = OrderId{id};
    o.side               = side;
    o.type               = type;
    o.price              = Price{price};
    o.quantity           = Quantity{qty};
    o.remaining_quantity = Quantity{qty};
    o.timestamp          = Timestamp{ts};
    return o;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// NullHandler compiles and runs without crash.
TEST(MatchingEngineT, NullHandlerCompiles) {
    MatchingEngineT<NullHandler> eng;
    Order bid = make(1, Side::Buy,  OrderType::Limit, 100, 10);
    Order ask = make(2, Side::Sell, OrderType::Limit, 101, 10);
    eng.submit(&bid);
    eng.submit(&ask);
    EXPECT_TRUE(eng.book().best_bid().has_value());
    EXPECT_TRUE(eng.book().best_ask().has_value());
}

// Limit order rests when no opposite side.
TEST(MatchingEngineT, LimitOrderRestsOnBook) {
    MatchingEngineT<TestHandler> eng;
    Order o = make(1, Side::Buy, OrderType::Limit, 100, 10);
    eng.submit(&o);

    ASSERT_EQ(eng.handler().accepted.size(),  1u);
    EXPECT_EQ(eng.handler().trades.size(),    0u);
    EXPECT_EQ(to_uint(eng.handler().accepted[0].quantity), 10u);
    EXPECT_TRUE(eng.book().best_bid().has_value());
}

// FIFO priority: oldest order at price level fills first.
TEST(MatchingEngineT, FifoPriorityAtSamePrice) {
    MatchingEngineT<TestHandler> eng;
    Order b1 = make(1, Side::Buy, OrderType::Limit, 100, 5);
    Order b2 = make(2, Side::Buy, OrderType::Limit, 100, 5);
    eng.submit(&b1);
    eng.submit(&b2);

    Order sell = make(3, Side::Sell, OrderType::Limit, 100, 5);
    eng.submit(&sell);

    ASSERT_EQ(eng.handler().trades.size(), 1u);
    EXPECT_EQ(to_uint(eng.handler().trades[0].maker_id), 1u);  // b1 fills, not b2
}

// Partial fill leaves remainder on book.
TEST(MatchingEngineT, PartialFillLeavesRemainder) {
    MatchingEngineT<TestHandler> eng;
    Order bid  = make(1, Side::Buy,  OrderType::Limit, 100, 10);
    Order ask  = make(2, Side::Sell, OrderType::Limit, 100, 6);
    eng.submit(&bid);
    eng.submit(&ask);

    ASSERT_EQ(eng.handler().trades.size(), 1u);
    EXPECT_EQ(to_uint(eng.handler().trades[0].quantity), 6u);

    Order* live = eng.book().find(OrderId{1});
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(to_uint(live->remaining_quantity), 4u);
}

// TradeEvent has correct fields (price = maker's price).
TEST(MatchingEngineT, TradeEventFields) {
    MatchingEngineT<TestHandler> eng;
    Order bid  = make(10, Side::Buy,  OrderType::Limit, 200, 3, 42);
    Order ask  = make(20, Side::Sell, OrderType::Limit, 200, 3, 99);
    eng.submit(&bid);
    eng.submit(&ask);

    ASSERT_EQ(eng.handler().trades.size(), 1u);
    auto& t = eng.handler().trades[0];
    EXPECT_EQ(to_uint(t.maker_id),  10u);
    EXPECT_EQ(to_uint(t.taker_id),  20u);
    EXPECT_EQ(to_int (t.price),     200);
    EXPECT_EQ(to_uint(t.quantity),  3u);
}

// Market order fires on_rejected when book is empty.
TEST(MatchingEngineT, MarketOrderEmptyBookRejected) {
    MatchingEngineT<TestHandler> eng;
    Order mkt = make(1, Side::Buy, OrderType::Market, 0, 5);
    eng.submit(&mkt);

    ASSERT_EQ(eng.handler().rejected.size(), 1u);
    EXPECT_EQ(eng.handler().rejected[0].reason, RejectReason::MarketOrderNoLiquidity);
}

// Cancel removes the order and fires on_cancelled.
TEST(MatchingEngineT, CancelRemovesOrder) {
    MatchingEngineT<TestHandler> eng;
    Order bid = make(7, Side::Buy, OrderType::Limit, 100, 10);
    eng.submit(&bid);
    eng.cancel(OrderId{7});

    ASSERT_EQ(eng.handler().cancelled.size(), 1u);
    EXPECT_EQ(to_uint(eng.handler().cancelled[0].remaining_quantity), 10u);
    EXPECT_EQ(eng.book().find(OrderId{7}), nullptr);
    EXPECT_FALSE(eng.book().best_bid().has_value());
}

// Cancel unknown ID fires on_rejected.
TEST(MatchingEngineT, CancelUnknownIdRejected) {
    MatchingEngineT<TestHandler> eng;
    eng.cancel(OrderId{99});
    ASSERT_EQ(eng.handler().rejected.size(), 1u);
    EXPECT_EQ(eng.handler().rejected[0].reason, RejectReason::UnknownId);
}

// Zero-quantity submit fires on_rejected.
TEST(MatchingEngineT, ZeroQuantityRejected) {
    MatchingEngineT<TestHandler> eng;
    Order o = make(1, Side::Buy, OrderType::Limit, 100, 0);
    eng.submit(&o);
    ASSERT_EQ(eng.handler().rejected.size(), 1u);
    EXPECT_EQ(eng.handler().rejected[0].reason, RejectReason::InvalidQuantity);
}

// Modify: qty-decrease keeps FIFO position.
TEST(MatchingEngineT, Modify_QtyDecrease_KeepsFifo) {
    MatchingEngineT<TestHandler> eng;
    Order o1 = make(1, Side::Buy, OrderType::Limit, 100, 10);
    Order o2 = make(2, Side::Buy, OrderType::Limit, 100, 10);
    eng.submit(&o1);
    eng.submit(&o2);

    // Reduce o1 in-place — it stays at the front.
    eng.modify(OrderId{1}, std::nullopt, Quantity{5}, Timestamp{1});
    EXPECT_EQ(eng.handler().cancelled.size(), 0u);  // no cancel for qty-decrease

    Order sell = make(3, Side::Sell, OrderType::Limit, 100, 5);
    eng.submit(&sell);

    ASSERT_EQ(eng.handler().trades.size(), 1u);
    EXPECT_EQ(to_uint(eng.handler().trades[0].maker_id), 1u);  // o1 still at front
}

// Modify: price change loses queue position; fires on_cancelled + re-enters matching.
TEST(MatchingEngineT, Modify_PriceChange_LosesPosition) {
    MatchingEngineT<TestHandler> eng;
    Order o1 = make(1, Side::Buy, OrderType::Limit,  100, 5);
    Order o2 = make(2, Side::Buy, OrderType::Limit,  100, 5);
    eng.submit(&o1);
    eng.submit(&o2);

    // Move o1 to price 99 — it goes to the back of that (new) level.
    eng.modify(OrderId{1}, Price{99}, std::nullopt, Timestamp{2});
    ASSERT_EQ(eng.handler().cancelled.size(), 1u);

    // Sell at 99 — should fill o2 first (at 100), then o1 (at 99, back of queue).
    // Actually only o2 is at 100; o1 is now at 99.
    // A sell at 99 will cross with the best bid (100) first — that's o2.
    Order sell = make(3, Side::Sell, OrderType::Limit, 99, 5);
    eng.submit(&sell);

    ASSERT_GE(eng.handler().trades.size(), 1u);
    EXPECT_EQ(to_uint(eng.handler().trades[0].maker_id), 2u);  // o2 at price 100
}

// Modify unknown ID fires on_rejected.
TEST(MatchingEngineT, Modify_UnknownId_Rejected) {
    MatchingEngineT<TestHandler> eng;
    eng.modify(OrderId{42}, Price{100}, std::nullopt, Timestamp{0});
    ASSERT_EQ(eng.handler().rejected.size(), 1u);
    EXPECT_EQ(eng.handler().rejected[0].reason, RejectReason::UnknownId);
}

// insert_passive / delete_passive bypass matching.
TEST(MatchingEngineT, InsertPassiveNoEvents) {
    MatchingEngineT<TestHandler> eng;
    Order bid  = make(1, Side::Buy,  OrderType::Limit, 100, 10);
    Order ask  = make(2, Side::Sell, OrderType::Limit,  90, 10);
    // Crossing orders inserted passively must NOT match.
    eng.insert_passive(&bid);
    eng.insert_passive(&ask);

    EXPECT_EQ(eng.handler().trades.size(),   0u);
    EXPECT_EQ(eng.handler().accepted.size(), 0u);
    EXPECT_TRUE(eng.book().best_bid().has_value());
    EXPECT_TRUE(eng.book().best_ask().has_value());

    bool removed = eng.delete_passive(OrderId{1});
    EXPECT_TRUE(removed);
    EXPECT_EQ(eng.handler().cancelled.size(), 0u);  // no callback
    EXPECT_FALSE(eng.book().best_bid().has_value());
}

// reduce_passive: partial reduction keeps order on book.
TEST(MatchingEngineT, ReducePassivePartial) {
    MatchingEngineT<TestHandler> eng;
    Order bid = make(5, Side::Buy, OrderType::Limit, 100, 20);
    eng.insert_passive(&bid);

    bool ok = eng.reduce_passive(OrderId{5}, Quantity{8});
    EXPECT_TRUE(ok);
    EXPECT_EQ(eng.handler().cancelled.size(), 0u);

    Order* live = eng.book().find(OrderId{5});
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(to_uint(live->remaining_quantity), 12u);
}

// HandlerConcept rejects a type missing a method (compile-time check via static_assert).
// We test the positive case (TestHandler satisfies it) and the NullHandler.
TEST(MatchingEngineT, ConceptSatisfied) {
    static_assert(HandlerConcept<TestHandler>);
    static_assert(HandlerConcept<NullHandler>);
    SUCCEED();
}
