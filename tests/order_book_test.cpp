// OrderBook unit tests: add, cancel, modify, depth snapshot, and find.
#include <gtest/gtest.h>

#include "lob/order_book.hpp"

using namespace lob;

static Order make_order(uint64_t id, Side side, int64_t price, uint64_t qty,
                        uint64_t ts = 0) {
    Order o{};
    o.id                = OrderId{id};
    o.side              = side;
    o.type              = OrderType::Limit;
    o.price             = Price{price};
    o.quantity          = Quantity{qty};
    o.remaining_quantity = Quantity{qty};
    o.timestamp         = Timestamp{ts};
    return o;
}

TEST(OrderBook, AddBidUpdatesbestBid) {
    OrderBook book;
    Order o = make_order(1, Side::Buy, 100, 10);
    book.add_order(&o);
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(to_int(*book.best_bid()), 100);
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBook, AddAskUpdatesBestAsk) {
    OrderBook book;
    Order o = make_order(1, Side::Sell, 101, 5);
    book.add_order(&o);
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(to_int(*book.best_ask()), 101);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(OrderBook, BestBidIsHighestPrice) {
    OrderBook book;
    Order o1 = make_order(1, Side::Buy, 99, 10);
    Order o2 = make_order(2, Side::Buy, 101, 5);
    Order o3 = make_order(3, Side::Buy, 100, 8);
    book.add_order(&o1);
    book.add_order(&o2);
    book.add_order(&o3);
    EXPECT_EQ(to_int(*book.best_bid()), 101);
}

TEST(OrderBook, BestAskIsLowestPrice) {
    OrderBook book;
    Order o1 = make_order(1, Side::Sell, 103, 10);
    Order o2 = make_order(2, Side::Sell, 101, 5);
    Order o3 = make_order(3, Side::Sell, 102, 8);
    book.add_order(&o1);
    book.add_order(&o2);
    book.add_order(&o3);
    EXPECT_EQ(to_int(*book.best_ask()), 101);
}

TEST(OrderBook, CancelRemovesOrder) {
    OrderBook book;
    Order o = make_order(42, Side::Buy, 100, 10);
    book.add_order(&o);
    EXPECT_TRUE(book.cancel_order(OrderId{42}));
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(OrderBook, CancelUnknownIdReturnsFalse) {
    OrderBook book;
    EXPECT_FALSE(book.cancel_order(OrderId{999}));
}

TEST(OrderBook, CancelUpdatesLevelAggregates) {
    OrderBook book;
    Order o1 = make_order(1, Side::Buy, 100, 10);
    Order o2 = make_order(2, Side::Buy, 100, 20);
    book.add_order(&o1);
    book.add_order(&o2);

    auto depth = book.bid_depth(5);
    ASSERT_EQ(depth.size(), 1u);
    EXPECT_EQ(to_uint(depth[0].total_quantity), 30u);
    EXPECT_EQ(depth[0].order_count, 2u);

    book.cancel_order(OrderId{1});
    depth = book.bid_depth(5);
    ASSERT_EQ(depth.size(), 1u);
    EXPECT_EQ(to_uint(depth[0].total_quantity), 20u);
    EXPECT_EQ(depth[0].order_count, 1u);
}

TEST(OrderBook, CancelLastOrderInLevelRemovesLevel) {
    OrderBook book;
    Order o = make_order(1, Side::Buy, 100, 10);
    book.add_order(&o);
    book.cancel_order(OrderId{1});
    EXPECT_TRUE(book.bid_depth(5).empty());
}

TEST(OrderBook, ModifyQtyDecreaseKeepsPriority) {
    // Two orders at same price; decrease first order's qty.
    // First order must still be at the front after the decrease.
    OrderBook book;
    Order o1 = make_order(1, Side::Buy, 100, 20);
    Order o2 = make_order(2, Side::Buy, 100, 10);
    book.add_order(&o1);
    book.add_order(&o2);

    Order* live = book.modify_order(OrderId{1}, std::nullopt, Quantity{5}, nullptr);
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(to_uint(live->remaining_quantity), 5u);

    auto depth = book.bid_depth(5);
    ASSERT_EQ(depth.size(), 1u);
    EXPECT_EQ(to_uint(depth[0].total_quantity), 15u);  // 5 + 10

    book.cancel_order(OrderId{1});
    depth = book.bid_depth(5);
    EXPECT_EQ(to_uint(depth[0].total_quantity), 10u);
}

TEST(OrderBook, ModifyPriceLoosesPriority) {
    OrderBook book;
    Order o1 = make_order(1, Side::Buy, 100, 10);
    Order o2 = make_order(2, Side::Buy, 100, 10);
    book.add_order(&o1);
    book.add_order(&o2);

    Order new_node{};
    // Move o1 to price 101 — loses priority.
    Order* live = book.modify_order(OrderId{1}, Price{101}, std::nullopt, &new_node);
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(to_int(live->price), 101);
    EXPECT_EQ(to_int(*book.best_bid()), 101);
}

TEST(OrderBook, ModifyUnknownIdReturnsNull) {
    OrderBook book;
    Order dummy{};
    EXPECT_EQ(book.modify_order(OrderId{999}, std::nullopt, std::nullopt, &dummy), nullptr);
}

TEST(OrderBook, DepthSnapshotOrdering) {
    OrderBook book;
    Order o1 = make_order(1, Side::Sell, 102, 5);
    Order o2 = make_order(2, Side::Sell, 101, 3);
    Order o3 = make_order(3, Side::Sell, 103, 7);
    book.add_order(&o1);
    book.add_order(&o2);
    book.add_order(&o3);

    auto depth = book.ask_depth(3);
    ASSERT_EQ(depth.size(), 3u);
    EXPECT_EQ(to_int(depth[0].price), 101);
    EXPECT_EQ(to_int(depth[1].price), 102);
    EXPECT_EQ(to_int(depth[2].price), 103);
}

TEST(OrderBook, DepthSnapshotLimitN) {
    OrderBook book;
    for (int i = 1; i <= 5; ++i) {
        Order* o = new Order(make_order(static_cast<uint64_t>(i), Side::Buy,
                                       100 + i, 10));
        book.add_order(o);
    }
    auto depth = book.bid_depth(3);
    EXPECT_EQ(depth.size(), 3u);

    for (int i = 1; i <= 5; ++i) {
        Order* o = book.find(OrderId{static_cast<uint64_t>(i)});
        if (o) { book.cancel_order(OrderId{static_cast<uint64_t>(i)}); delete o; }
    }
}

TEST(OrderBook, FindReturnsLiveOrder) {
    OrderBook book;
    Order o = make_order(7, Side::Sell, 105, 20);
    book.add_order(&o);
    EXPECT_NE(book.find(OrderId{7}), nullptr);
    EXPECT_EQ(book.find(OrderId{99}), nullptr);
}
