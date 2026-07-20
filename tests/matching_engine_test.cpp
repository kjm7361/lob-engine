// MatchingEngine unit tests: FIFO priority, partial fills, market sweeps, cancel, modify, fuzz.
#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

#include "lob/matching_engine.hpp"

using namespace lob;

struct TestHandler {
    std::vector<TradeEvent>     trades;
    std::vector<OrderAccepted>  accepted;
    std::vector<OrderCancelled> cancelled;
    std::vector<OrderRejected>  rejected;

    EventHandler make() {
        return {
            [this](const TradeEvent& e)     { trades.push_back(e); },
            [this](const OrderAccepted& e)  { accepted.push_back(e); },
            [this](const OrderCancelled& e) { cancelled.push_back(e); },
            [this](const OrderRejected& e)  { rejected.push_back(e); },
        };
    }
};

// Order pool for tests — keeps objects alive for the duration of each test.
struct Pool {
    std::vector<std::unique_ptr<Order>> store;

    Order* make(uint64_t id, Side side, OrderType type, int64_t price,
                uint64_t qty, uint64_t ts = 0) {
        auto o               = std::make_unique<Order>();
        o->id                = OrderId{id};
        o->side              = side;
        o->type              = type;
        o->price             = Price{price};
        o->quantity          = Quantity{qty};
        o->remaining_quantity = Quantity{qty};
        o->timestamp         = Timestamp{ts};
        store.push_back(std::move(o));
        return store.back().get();
    }

    Order* limit(uint64_t id, Side side, int64_t price, uint64_t qty,
                 uint64_t ts = 0) {
        return make(id, side, OrderType::Limit, price, qty, ts);
    }

    Order* market(uint64_t id, Side side, uint64_t qty, uint64_t ts = 0) {
        return make(id, side, OrderType::Market, 0, qty, ts);
    }
};

TEST(MatchingEngine, LimitOrderRestsWhenNoOpposideSide) {
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Buy, 100, 10));

    EXPECT_EQ(h.trades.size(), 0u);
    EXPECT_EQ(h.accepted.size(), 1u);
    EXPECT_EQ(to_int(*me.book().best_bid()), 100);
    EXPECT_FALSE(me.book().best_ask().has_value());
}

TEST(MatchingEngine, FifoPriorityAtSamePrice) {
    // Two resting bids at 100 (order 1 then 2).
    // A sell limit at 100 fills order 1 first.
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Buy, 100, 10, /*ts=*/1));
    me.submit(p.limit(2, Side::Buy, 100, 10, /*ts=*/2));

    // Sell 5 at 100 — should fill from order 1 only.
    me.submit(p.limit(3, Side::Sell, 100, 5, /*ts=*/3));

    ASSERT_EQ(h.trades.size(), 1u);
    EXPECT_EQ(to_uint(h.trades[0].maker_id), 1u);
    EXPECT_EQ(to_uint(h.trades[0].taker_id), 3u);
    EXPECT_EQ(to_uint(h.trades[0].quantity), 5u);

    // Order 1 should still have 5 remaining; order 2 untouched.
    EXPECT_NE(me.book().find(OrderId{1}), nullptr);
    EXPECT_EQ(to_uint(me.book().find(OrderId{1})->remaining_quantity), 5u);
    EXPECT_EQ(to_uint(me.book().find(OrderId{2})->remaining_quantity), 10u);
}

TEST(MatchingEngine, PricePriorityAcrossLevels) {
    // Resting bids at 102 (better) and 100 (worse).
    // Incoming sell at 100 fills the 102 level first.
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Buy, 102, 5));
    me.submit(p.limit(2, Side::Buy, 100, 5));

    me.submit(p.limit(3, Side::Sell, 100, 8));

    // First fill: maker 1 at 102 (5 shares).
    // Second fill: maker 2 at 100 (3 shares).
    ASSERT_EQ(h.trades.size(), 2u);
    EXPECT_EQ(to_uint(h.trades[0].maker_id), 1u);
    EXPECT_EQ(to_int(h.trades[0].price), 102);
    EXPECT_EQ(to_uint(h.trades[0].quantity), 5u);
    EXPECT_EQ(to_uint(h.trades[1].maker_id), 2u);
    EXPECT_EQ(to_int(h.trades[1].price), 100);
    EXPECT_EQ(to_uint(h.trades[1].quantity), 3u);
}

TEST(MatchingEngine, PartialFillLeavesRemainderOnBook) {
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Sell, 101, 20));
    me.submit(p.limit(2, Side::Buy,  101, 7));

    ASSERT_EQ(h.trades.size(), 1u);
    EXPECT_EQ(to_uint(h.trades[0].quantity), 7u);

    // Order 1 should still be resting with 13 remaining.
    ASSERT_NE(me.book().find(OrderId{1}), nullptr);
    EXPECT_EQ(to_uint(me.book().find(OrderId{1})->remaining_quantity), 13u);

    auto depth = me.book().ask_depth(5);
    ASSERT_EQ(depth.size(), 1u);
    EXPECT_EQ(to_uint(depth[0].total_quantity), 13u);
}

TEST(MatchingEngine, MarketOrderSweepsMultipleLevels) {
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Sell, 101, 5));
    me.submit(p.limit(2, Side::Sell, 102, 5));
    me.submit(p.limit(3, Side::Sell, 103, 5));

    me.submit(p.market(4, Side::Buy, 12));

    EXPECT_EQ(h.trades.size(), 3u);
    EXPECT_EQ(to_uint(h.trades[0].quantity), 5u);
    EXPECT_EQ(to_uint(h.trades[1].quantity), 5u);
    EXPECT_EQ(to_uint(h.trades[2].quantity), 2u);
    EXPECT_EQ(me.book().find(OrderId{1}), nullptr);
    EXPECT_EQ(me.book().find(OrderId{2}), nullptr);
    ASSERT_NE(me.book().find(OrderId{3}), nullptr);
    EXPECT_EQ(to_uint(me.book().find(OrderId{3})->remaining_quantity), 3u);
}

TEST(MatchingEngine, MarketOrderEmptyBookRejected) {
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.market(1, Side::Buy, 10));

    EXPECT_EQ(h.trades.size(), 0u);
    ASSERT_EQ(h.rejected.size(), 1u);
    EXPECT_EQ(to_uint(h.rejected[0].id), 1u);
    EXPECT_EQ(h.rejected[0].reason, RejectReason::MarketOrderNoLiquidity);
    // Must not rest on book.
    EXPECT_FALSE(me.book().best_bid().has_value());
}

TEST(MatchingEngine, CancelRemovesOrderAndUpdatesAggregates) {
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Buy, 100, 10));
    me.submit(p.limit(2, Side::Buy, 100, 20));

    me.cancel(OrderId{1});

    ASSERT_EQ(h.cancelled.size(), 1u);
    EXPECT_EQ(to_uint(h.cancelled[0].id), 1u);
    EXPECT_EQ(to_uint(h.cancelled[0].remaining_quantity), 10u);

    auto depth = me.book().bid_depth(5);
    ASSERT_EQ(depth.size(), 1u);
    EXPECT_EQ(to_uint(depth[0].total_quantity), 20u);
}

TEST(MatchingEngine, CancelUnknownIdFiresRejected) {
    TestHandler h;
    MatchingEngine me(h.make());

    me.cancel(OrderId{42});

    ASSERT_EQ(h.rejected.size(), 1u);
    EXPECT_EQ(h.rejected[0].reason, RejectReason::UnknownId);
}

TEST(MatchingEngine, ModifyQtyDecreaseKeepsFifoPriority) {
    // order 1 then order 2 at same price.  Decrease order 1's qty.
    // Incoming taker should still fill order 1 first.
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    Order* o1 = p.limit(1, Side::Buy, 100, 20);
    Order* o2 = p.limit(2, Side::Buy, 100, 10);
    me.submit(o1);
    me.submit(o2);

    me.book().modify_order(OrderId{1}, std::nullopt, Quantity{5}, nullptr);

    me.submit(p.limit(3, Side::Sell, 100, 5));

    ASSERT_EQ(h.trades.size(), 1u);
    EXPECT_EQ(to_uint(h.trades[0].maker_id), 1u);
    EXPECT_EQ(to_uint(h.trades[0].quantity), 5u);
}

TEST(MatchingEngine, ModifyQtyIncreaseLoesesPriority) {
    // order 1 then order 2.  Increase order 1 qty → it loses priority.
    // Incoming taker should fill order 2 first.
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Buy, 100, 10));
    me.submit(p.limit(2, Side::Buy, 100, 10));

    Order new_node{};
    me.book().modify_order(OrderId{1}, std::nullopt, Quantity{15}, &new_node);

    me.submit(p.limit(3, Side::Sell, 100, 10));

    ASSERT_EQ(h.trades.size(), 1u);
    EXPECT_EQ(to_uint(h.trades[0].maker_id), 2u);
}

TEST(MatchingEngine, CrossingLimitRestsRemainder) {
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Sell, 101, 5));
    // Buy 15 at 101 — fills all 5 from order 1, rests 10 on bid side.
    me.submit(p.limit(2, Side::Buy, 101, 15));

    ASSERT_EQ(h.trades.size(), 1u);
    EXPECT_EQ(to_uint(h.trades[0].quantity), 5u);

    EXPECT_TRUE(me.book().best_bid().has_value());
    EXPECT_EQ(to_int(*me.book().best_bid()), 101);
    EXPECT_EQ(to_uint(me.book().find(OrderId{2})->remaining_quantity), 10u);
}

TEST(MatchingEngine, TradeEventFields) {
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(10, Side::Buy, 105, 8, /*ts=*/1000));
    me.submit(p.limit(20, Side::Sell, 105, 3, /*ts=*/2000));

    ASSERT_EQ(h.trades.size(), 1u);
    const auto& t = h.trades[0];
    EXPECT_EQ(to_uint(t.maker_id), 10u);
    EXPECT_EQ(to_uint(t.taker_id), 20u);
    EXPECT_EQ(to_int(t.price), 105);   // maker's price
    EXPECT_EQ(to_uint(t.quantity), 3u);
    EXPECT_EQ(to_uint(t.timestamp), 2000u);
}

TEST(MatchingEngine, ZeroQuantityRejected) {
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Buy, 100, 0));

    ASSERT_EQ(h.rejected.size(), 1u);
    EXPECT_EQ(h.rejected[0].reason, RejectReason::InvalidQuantity);
    EXPECT_EQ(h.trades.size(), 0u);
    EXPECT_FALSE(me.book().best_bid().has_value());
}

// Performs 10k random operations and verifies after each that:
//   per-level total_quantity == sum of remaining_quantity of all resting orders
// against a naive reference map.
TEST(MatchingEngine, FuzzAggregateInvariants) {
    std::mt19937 rng(42);
    auto randi = [&](int lo, int hi) -> int {
        return std::uniform_int_distribution<int>(lo, hi)(rng);
    };

    struct Ref {
        int64_t  price;
        Side     side;
        uint64_t remaining_qty;
    };

    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    uint64_t next_id = 1;
    std::vector<uint64_t> live_ids;

    auto check_invariants = [&]() {
        // Build reference: for each live order in index, sum qty per (side,price).
        std::map<std::pair<int, int64_t>, uint64_t> ref;  // (side, price) -> sum_qty
        for (uint64_t id : live_ids) {
            const Order* o = me.book().find(OrderId{id});
            if (!o) continue;
            auto key = std::make_pair(static_cast<int>(o->side), to_int(o->price));
            ref[key] += to_uint(o->remaining_quantity);
        }
        // Compare against book depth.
        for (auto& [key, expected_qty] : ref) {
            int side_int = key.first;
            Price price  = Price{key.second};
            auto& side_map =
                (side_int == static_cast<int>(Side::Buy)) ? me.book().bids()
                                                           : me.book().asks();
            auto it = side_map.find(price);
            if (it == side_map.end()) {
                EXPECT_EQ(expected_qty, 0u) << "Level missing but ref has qty";
            } else {
                EXPECT_EQ(to_uint(it->second.total_quantity()), expected_qty)
                    << "Aggregate mismatch at price=" << key.second;
            }
        }
    };

    for (int i = 0; i < 10000; ++i) {
        int op = randi(0, 2);

        if (op == 0 || live_ids.empty()) {
            Side     side  = randi(0, 1) ? Side::Buy : Side::Sell;
            int64_t  price = randi(95, 105);
            uint64_t qty   = static_cast<uint64_t>(randi(1, 20));
            uint64_t id    = next_id++;

            Order* o = p.limit(id, side, price, qty, static_cast<uint64_t>(i));
            me.submit(o);

            // Only track if it rested (check book).
            if (me.book().find(OrderId{id}) != nullptr) {
                live_ids.push_back(id);
            }
        } else if (op == 1) {
            size_t   idx = static_cast<size_t>(randi(0, static_cast<int>(live_ids.size()) - 1));
            uint64_t id  = live_ids[idx];
            if (me.book().find(OrderId{id}) != nullptr) {
                me.cancel(OrderId{id});
            }
            live_ids.erase(live_ids.begin() + static_cast<ptrdiff_t>(idx));
        } else {
            Side     side = randi(0, 1) ? Side::Buy : Side::Sell;
            uint64_t qty  = static_cast<uint64_t>(randi(1, 5));
            me.submit(p.market(next_id++, side, qty, static_cast<uint64_t>(i)));

            // Refresh live_ids: remove any fully-filled orders.
            live_ids.erase(
                std::remove_if(live_ids.begin(), live_ids.end(),
                               [&](uint64_t id) {
                                   return me.book().find(OrderId{id}) == nullptr;
                               }),
                live_ids.end());
        }

        check_invariants();
    }
}

// ── MatchingEngine::modify() tests ───────────────────────────────────────────

TEST(MatchingEngineModify, QtyDecrease_KeepsFifoPosition) {
    // o1 is at the front of the 100-bid level; decrease its qty.
    // An incoming sell should still fill o1 first, not o2.
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Buy, 100, 20, 1));
    me.submit(p.limit(2, Side::Buy, 100, 10, 2));

    me.modify(OrderId{1}, std::nullopt, Quantity{5}, Timestamp{3});

    // Sell 5 at 100 — must fill from order 1 (still at front despite qty reduction).
    me.submit(p.limit(3, Side::Sell, 100, 5, 4));

    ASSERT_EQ(h.trades.size(), 1u);
    EXPECT_EQ(to_uint(h.trades[0].maker_id), 1u);
    EXPECT_EQ(to_uint(h.trades[0].quantity), 5u);

    // Order 1 is now fully consumed; order 2 remains untouched.
    EXPECT_EQ(me.book().find(OrderId{1}), nullptr);
    ASSERT_NE(me.book().find(OrderId{2}), nullptr);
    EXPECT_EQ(to_uint(me.book().find(OrderId{2})->remaining_quantity), 10u);
}

TEST(MatchingEngineModify, PriceChange_LosesPosition) {
    // o1 arrives first at 100, o2 second.
    // Move o1 to price 99 — it goes to the back of the 99 level behind o2.
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Buy, 100, 10, 1));
    me.submit(p.limit(2, Side::Buy,  99, 10, 2));

    // on_cancelled fires for o1, then on_accepted fires for o1 at 99.
    me.modify(OrderId{1}, Price{99}, std::nullopt, Timestamp{3});

    ASSERT_EQ(h.cancelled.size(), 1u);
    EXPECT_EQ(to_uint(h.cancelled[0].id), 1u);

    // Sell 10 at 99 — should fill o2 first (it was earlier in the 99-level queue).
    me.submit(p.limit(3, Side::Sell, 99, 10, 4));

    ASSERT_GE(h.trades.size(), 1u);
    EXPECT_EQ(to_uint(h.trades[0].maker_id), 2u);

    // o1 should still be resting at 99 (unfilled this round).
    ASSERT_NE(me.book().find(OrderId{1}), nullptr);
    EXPECT_EQ(to_int(me.book().find(OrderId{1})->price), 99);
}

TEST(MatchingEngineModify, QtyIncrease_LosesPosition) {
    // o1 arrives before o2 at price 100.  Increasing o1's qty loses its spot.
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Buy, 100, 5,  1));
    me.submit(p.limit(2, Side::Buy, 100, 10, 2));

    me.modify(OrderId{1}, std::nullopt, Quantity{15}, Timestamp{3});

    // Sell 10 — should fill o2 (now at the front) entirely.
    me.submit(p.limit(3, Side::Sell, 100, 10, 4));

    ASSERT_GE(h.trades.size(), 1u);
    EXPECT_EQ(to_uint(h.trades[0].maker_id), 2u);
    EXPECT_EQ(to_uint(h.trades[0].quantity), 10u);

    // o1 is still resting with 15 shares (re-added at back, not yet filled).
    ASSERT_NE(me.book().find(OrderId{1}), nullptr);
    EXPECT_EQ(to_uint(me.book().find(OrderId{1})->remaining_quantity), 15u);
}

TEST(MatchingEngineModify, ModifyIntoOpposite_Executes) {
    // Resting ask at 100, resting bid at 98.
    // Move the bid to 101 — it now crosses the ask and should execute immediately.
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Sell, 100, 10, 1));  // resting ask
    me.submit(p.limit(2, Side::Buy,   98, 10, 2));  // resting bid

    me.modify(OrderId{2}, Price{101}, std::nullopt, Timestamp{3});

    // on_cancelled fires for o2 at 98, then it re-submits and crosses o1.
    ASSERT_EQ(h.cancelled.size(), 1u);
    EXPECT_EQ(to_uint(h.cancelled[0].id), 2u);

    ASSERT_EQ(h.trades.size(), 1u);
    EXPECT_EQ(to_uint(h.trades[0].maker_id), 1u);
    EXPECT_EQ(to_uint(h.trades[0].taker_id), 2u);
    EXPECT_EQ(to_int(h.trades[0].price), 100);   // maker's price
    EXPECT_EQ(to_uint(h.trades[0].quantity), 10u);

    // Both orders fully consumed.
    EXPECT_EQ(me.book().find(OrderId{1}), nullptr);
    EXPECT_EQ(me.book().find(OrderId{2}), nullptr);
}

TEST(MatchingEngineModify, UnknownId_Rejected) {
    TestHandler h;
    MatchingEngine me(h.make());

    me.modify(OrderId{999}, Price{100}, std::nullopt, Timestamp{0});

    ASSERT_EQ(h.rejected.size(), 1u);
    EXPECT_EQ(to_uint(h.rejected[0].id), 999u);
    EXPECT_EQ(h.rejected[0].reason, RejectReason::UnknownId);
}

TEST(MatchingEngineModify, PartialFillThenQtyDecrease_CorrectState) {
    // Resting sell 20 at 101.  Partial fill of 7 (remaining=13).
    // Then decrease qty to 6.  Then a buy of 6 completes the order.
    TestHandler h;
    MatchingEngine me(h.make());
    Pool p;

    me.submit(p.limit(1, Side::Sell, 101, 20, 1));
    me.submit(p.limit(2, Side::Buy,  101,  7, 2));  // partial fill, 7 executed

    ASSERT_EQ(h.trades.size(), 1u);
    ASSERT_NE(me.book().find(OrderId{1}), nullptr);
    EXPECT_EQ(to_uint(me.book().find(OrderId{1})->remaining_quantity), 13u);

    // Decrease qty to 6 — order keeps FIFO position, no re-queue needed.
    me.modify(OrderId{1}, std::nullopt, Quantity{6}, Timestamp{3});
    ASSERT_NE(me.book().find(OrderId{1}), nullptr);
    EXPECT_EQ(to_uint(me.book().find(OrderId{1})->remaining_quantity), 6u);

    auto depth = me.book().ask_depth(5);
    ASSERT_EQ(depth.size(), 1u);
    EXPECT_EQ(to_uint(depth[0].total_quantity), 6u);

    // Buy 6 — fills and removes order 1.
    me.submit(p.limit(3, Side::Buy, 101, 6, 4));
    ASSERT_EQ(h.trades.size(), 2u);
    EXPECT_EQ(to_uint(h.trades[1].quantity), 6u);
    EXPECT_EQ(me.book().find(OrderId{1}), nullptr);
}
