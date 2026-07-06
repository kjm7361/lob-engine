#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "lob/matching_engine.hpp"

using namespace lob;

// ── Formatting helpers ────────────────────────────────────────────────────────

static std::string side_str(Side s) {
    return s == Side::Buy ? "BUY " : "SELL";
}

static void print_book(const OrderBook& book, int levels = 5) {
    auto bids = book.bid_depth(static_cast<size_t>(levels));
    auto asks = book.ask_depth(static_cast<size_t>(levels));

    std::cout << "\n╔═══════════════════════════════════════╗\n";
    std::cout << "║            ORDER BOOK LADDER          ║\n";
    std::cout << "╠══════════════╦══════╦═════════════════╣\n";
    std::cout << "║    PRICE     ║  QTY ║ SIDE            ║\n";
    std::cout << "╠══════════════╬══════╬═════════════════╣\n";

    // Print asks in reverse (highest ask first for visual ladder).
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        std::cout << "║  " << std::setw(10) << to_int(it->price)
                  << "  ║" << std::setw(5) << to_uint(it->total_quantity)
                  << " ║ ASK             ║\n";
    }
    std::cout << "╠══════════════╬══════╬═════════════════╣\n";
    for (const auto& lvl : bids) {
        std::cout << "║  " << std::setw(10) << to_int(lvl.price)
                  << "  ║" << std::setw(5) << to_uint(lvl.total_quantity)
                  << " ║ BID             ║\n";
    }
    std::cout << "╚══════════════╩══════╩═════════════════╝\n";

    auto bb = book.best_bid();
    auto ba = book.best_ask();
    if (bb && ba) {
        int64_t spread = to_int(*ba) - to_int(*bb);
        std::cout << "  Spread: " << spread << " tick(s)\n";
    } else {
        std::cout << "  (one side empty)\n";
    }
}

// ── Main demo ─────────────────────────────────────────────────────────────────

int main() {
    std::vector<std::unique_ptr<Order>> pool;
    std::vector<TradeEvent>             trade_log;
    int trade_num = 0;

    auto alloc = [&](uint64_t id, Side side, OrderType type, int64_t price,
                     uint64_t qty, uint64_t ts) -> Order* {
        auto o               = std::make_unique<Order>();
        o->id                = OrderId{id};
        o->side              = side;
        o->type              = type;
        o->price             = Price{price};
        o->quantity          = Quantity{qty};
        o->remaining_quantity = Quantity{qty};
        o->timestamp         = Timestamp{ts};
        pool.push_back(std::move(o));
        return pool.back().get();
    };

    EventHandler handler{
        [&](const TradeEvent& e) {
            ++trade_num;
            std::cout << "  [TRADE #" << trade_num << "]  maker=" << to_uint(e.maker_id)
                      << "  taker=" << to_uint(e.taker_id)
                      << "  price=" << to_int(e.price)
                      << "  qty=" << to_uint(e.quantity) << "\n";
            trade_log.push_back(e);
        },
        [](const OrderAccepted& e) {
            std::cout << "  [ACCEPTED] id=" << to_uint(e.id)
                      << "  " << side_str(e.side)
                      << "  price=" << to_int(e.price)
                      << "  qty=" << to_uint(e.quantity) << "\n";
        },
        [](const OrderCancelled& e) {
            std::cout << "  [CANCELLED] id=" << to_uint(e.id)
                      << "  remaining=" << to_uint(e.remaining_quantity) << "\n";
        },
        [](const OrderRejected& e) {
            std::cout << "  [REJECTED] id=" << to_uint(e.id) << "\n";
        },
    };

    MatchingEngine me(std::move(handler));

    std::cout << "\n=== Phase 1: Build a resting book ===\n";
    me.submit(alloc(1, Side::Buy,  OrderType::Limit, 99,  100, 1));
    me.submit(alloc(2, Side::Buy,  OrderType::Limit, 99,   50, 2));
    me.submit(alloc(3, Side::Buy,  OrderType::Limit, 98,  200, 3));
    me.submit(alloc(4, Side::Sell, OrderType::Limit, 101, 80,  4));
    me.submit(alloc(5, Side::Sell, OrderType::Limit, 102, 120, 5));
    me.submit(alloc(6, Side::Sell, OrderType::Limit, 101, 40,  6));
    print_book(me.book());

    std::cout << "\n=== Phase 2: Aggressive limit crosses spread ===\n";
    // Buy at 101 — matches against asks at 101 (FIFO: order 4 first, then 6).
    me.submit(alloc(7, Side::Buy, OrderType::Limit, 101, 90, 7));
    print_book(me.book());

    std::cout << "\n=== Phase 3: Market order sweeps remaining asks ===\n";
    me.submit(alloc(8, Side::Buy, OrderType::Market, 0, 200, 8));
    print_book(me.book());

    std::cout << "\n=== Phase 4: Cancel a resting bid ===\n";
    me.cancel(OrderId{3});
    print_book(me.book());

    std::cout << "\n=== Trade log (" << trade_log.size() << " trades) ===\n";
    for (size_t i = 0; i < trade_log.size(); ++i) {
        const auto& t = trade_log[i];
        std::cout << "  " << (i + 1) << ". maker=" << to_uint(t.maker_id)
                  << " taker=" << to_uint(t.taker_id)
                  << " @" << to_int(t.price)
                  << " x" << to_uint(t.quantity) << "\n";
    }

    return 0;
}
