#include "lob/replay/replay.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

namespace {

// ── Synthetic ITCH file builder (duplicated from itch/parser_test.cpp) ────────

class TempItchFile {
   public:
    explicit TempItchFile(const std::string& tag)
        : path_(std::filesystem::temp_directory_path() /
                ("replay_test_" + tag + ".bin")) {}

    ~TempItchFile() { std::filesystem::remove(path_); }

    TempItchFile(const TempItchFile&) = delete;
    TempItchFile& operator=(const TempItchFile&) = delete;

    void add_message(const std::vector<uint8_t>& body) {
        auto n = static_cast<uint16_t>(body.size());
        stream_.push_back(static_cast<uint8_t>(n >> 8));
        stream_.push_back(static_cast<uint8_t>(n & 0xffu));
        stream_.insert(stream_.end(), body.begin(), body.end());
    }

    void write() const {
        std::ofstream f(path_, std::ios::binary);
        f.write(reinterpret_cast<const char*>(stream_.data()),
                static_cast<std::streamsize>(stream_.size()));
    }

    [[nodiscard]] std::string path() const { return path_.string(); }

   private:
    std::filesystem::path path_;
    std::vector<uint8_t>  stream_;
};

// ── Wire-encoding helpers ─────────────────────────────────────────────────────

static void push_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x & 0xffu));
}

static void push_u32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xffu));
    v.push_back(static_cast<uint8_t>(x & 0xffu));
}

static void push_u48(std::vector<uint8_t>& v, uint64_t x) {
    v.push_back(static_cast<uint8_t>((x >> 40) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 32) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xffu));
    v.push_back(static_cast<uint8_t>(x & 0xffu));
}

static void push_u64(std::vector<uint8_t>& v, uint64_t x) {
    v.push_back(static_cast<uint8_t>((x >> 56) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 48) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 40) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 32) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xffu));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xffu));
    v.push_back(static_cast<uint8_t>(x & 0xffu));
}

static void push_bytes(std::vector<uint8_t>& v, const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i)
        v.push_back(static_cast<uint8_t>(s[i]));
}

// ── Message builders ──────────────────────────────────────────────────────────

static constexpr uint64_t kTs = 1000000ULL;  // 1 ms in nanoseconds

static std::vector<uint8_t> make_R(uint16_t locate, const char* stock8) {
    std::vector<uint8_t> b;
    b.push_back('R');
    push_u16(b, locate);
    push_u16(b, 0);          // tracking
    push_u48(b, kTs);
    push_bytes(b, stock8, 8);
    b.push_back('Q');        // market_category
    b.push_back('N');        // financial_status
    push_u32(b, 100u);      // round_lot_size
    b.push_back('N');        // round_lots_only
    b.push_back('C');        // issue_classification
    b.push_back('S'); b.push_back('1');  // issue_sub_type
    b.push_back('P');        // authenticity
    b.push_back('N');        // short_sale_threshold
    b.push_back(' ');        // ipo_flag
    b.push_back('1');        // luld_tier
    b.push_back('N');        // etp_flag
    push_u32(b, 0u);        // etp_leverage_factor
    b.push_back('N');        // inverse_indicator
    return b;                // 39 bytes
}

static std::vector<uint8_t> make_A(uint16_t locate, uint64_t ref,
                                    char side, uint32_t shares,
                                    const char* stock8, uint32_t price) {
    std::vector<uint8_t> b;
    b.push_back('A');
    push_u16(b, locate);
    push_u16(b, 0);
    push_u48(b, kTs);
    push_u64(b, ref);
    b.push_back(static_cast<uint8_t>(side));
    push_u32(b, shares);
    push_bytes(b, stock8, 8);
    push_u32(b, price);
    return b;  // 36 bytes
}

static std::vector<uint8_t> make_E(uint16_t locate, uint64_t ref,
                                    uint32_t exec_shares) {
    std::vector<uint8_t> b;
    b.push_back('E');
    push_u16(b, locate);
    push_u16(b, 0);
    push_u48(b, kTs);
    push_u64(b, ref);
    push_u32(b, exec_shares);
    push_u64(b, 9999ULL);  // match_number
    return b;  // 31 bytes
}

static std::vector<uint8_t> make_X(uint16_t locate, uint64_t ref,
                                    uint32_t cancelled_shares) {
    std::vector<uint8_t> b;
    b.push_back('X');
    push_u16(b, locate);
    push_u16(b, 0);
    push_u48(b, kTs);
    push_u64(b, ref);
    push_u32(b, cancelled_shares);
    return b;  // 23 bytes
}

static std::vector<uint8_t> make_D(uint16_t locate, uint64_t ref) {
    std::vector<uint8_t> b;
    b.push_back('D');
    push_u16(b, locate);
    push_u16(b, 0);
    push_u48(b, kTs);
    push_u64(b, ref);
    return b;  // 19 bytes
}

static std::vector<uint8_t> make_U(uint16_t locate, uint64_t orig_ref,
                                    uint64_t new_ref, uint32_t shares,
                                    uint32_t price) {
    std::vector<uint8_t> b;
    b.push_back('U');
    push_u16(b, locate);
    push_u16(b, 0);
    push_u48(b, kTs);
    push_u64(b, orig_ref);
    push_u64(b, new_ref);
    push_u32(b, shares);
    push_u32(b, price);
    return b;  // 35 bytes
}

}  // namespace

using namespace lob;
using namespace lob::replay;

// ── Basic add + delete leaves book empty ─────────────────────────────────────

TEST(ReplayEngine, AddThenDeleteLeavesEmptyBook) {
    TempItchFile f("add_del");
    f.add_message(make_R(1, "AAPL    "));
    f.add_message(make_A(1, 42, 'B', 100, "AAPL    ", 1500000));
    f.add_message(make_D(1, 42));
    f.write();

    ReplayEngine eng;
    auto stats = eng.run(f.path());

    EXPECT_EQ(stats.orders_added,   1u);
    EXPECT_EQ(stats.orders_deleted, 1u);
    EXPECT_EQ(stats.unknown_refs,   0u);

    const OrderBook* bk = eng.book("AAPL");
    ASSERT_NE(bk, nullptr);
    EXPECT_FALSE(bk->best_bid().has_value());
    EXPECT_FALSE(bk->best_ask().has_value());
}

// ── Full execution removes the order ─────────────────────────────────────────

TEST(ReplayEngine, FullExecutionRemovesOrder) {
    TempItchFile f("full_exec");
    f.add_message(make_R(1, "MSFT    "));
    f.add_message(make_A(1, 10, 'S', 200, "MSFT    ", 3000000));
    f.add_message(make_E(1, 10, 200));  // full fill
    f.write();

    ReplayEngine eng;
    auto stats = eng.run(f.path());

    EXPECT_EQ(stats.orders_added,    1u);
    EXPECT_EQ(stats.orders_executed, 1u);

    const OrderBook* bk = eng.book("MSFT");
    ASSERT_NE(bk, nullptr);
    EXPECT_FALSE(bk->best_ask().has_value());
}

// ── Partial execution reduces remaining qty ───────────────────────────────────

TEST(ReplayEngine, PartialExecutionReducesQty) {
    TempItchFile f("partial_exec");
    f.add_message(make_R(1, "GOOG    "));
    f.add_message(make_A(1, 7, 'B', 500, "GOOG    ", 2800000));
    f.add_message(make_E(1, 7, 100));  // partial fill
    f.write();

    ReplayEngine eng;
    auto stats = eng.run(f.path());

    EXPECT_EQ(stats.orders_executed, 1u);

    const OrderBook* bk = eng.book("GOOG");
    ASSERT_NE(bk, nullptr);
    ASSERT_TRUE(bk->best_bid().has_value());
    EXPECT_EQ(to_int(*bk->best_bid()), 2800000);

    // Remaining should be 400.
    const Order* o = bk->find(OrderId{7});
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(to_uint(o->remaining_quantity), 400u);
}

// ── Partial cancel reduces remaining qty ─────────────────────────────────────

TEST(ReplayEngine, PartialCancelReducesQty) {
    TempItchFile f("partial_cancel");
    f.add_message(make_R(2, "IBM     "));
    f.add_message(make_A(2, 99, 'S', 300, "IBM     ", 1400000));
    f.add_message(make_X(2, 99, 100));  // cancel 100 of 300
    f.write();

    ReplayEngine eng;
    eng.run(f.path());

    const OrderBook* bk = eng.book("IBM");
    ASSERT_NE(bk, nullptr);
    ASSERT_TRUE(bk->best_ask().has_value());

    const Order* o = bk->find(OrderId{99});
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(to_uint(o->remaining_quantity), 200u);
}

// ── Full cancel via X removes the order ──────────────────────────────────────

TEST(ReplayEngine, FullCancelViaXRemovesOrder) {
    TempItchFile f("full_cancel");
    f.add_message(make_R(3, "NVDA    "));
    f.add_message(make_A(3, 55, 'B', 100, "NVDA    ", 7000000));
    f.add_message(make_X(3, 55, 100));  // cancel all
    f.write();

    ReplayEngine eng;
    auto stats = eng.run(f.path());

    EXPECT_EQ(stats.orders_cancelled, 1u);

    const OrderBook* bk = eng.book("NVDA");
    ASSERT_NE(bk, nullptr);
    EXPECT_FALSE(bk->best_bid().has_value());
}

// ── Order Replace: old ref gone, new ref on book ──────────────────────────────

TEST(ReplayEngine, OrderReplaceUpdatesBook) {
    TempItchFile f("replace");
    f.add_message(make_R(4, "AMZN    "));
    f.add_message(make_A(4, 200, 'B', 100, "AMZN    ", 1800000));
    f.add_message(make_U(4, 200, 201, 150, 1801000));  // replace with new qty+price
    f.write();

    ReplayEngine eng;
    auto stats = eng.run(f.path());

    EXPECT_EQ(stats.orders_replaced, 1u);

    const OrderBook* bk = eng.book("AMZN");
    ASSERT_NE(bk, nullptr);

    // Old ref should be gone.
    EXPECT_EQ(bk->find(OrderId{200}), nullptr);

    // New ref should be on the book.
    const Order* o = bk->find(OrderId{201});
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(to_uint(o->remaining_quantity), 150u);
    EXPECT_EQ(to_int(o->price), 1801000);
}

// ── Unknown ref counts are tracked ───────────────────────────────────────────

TEST(ReplayEngine, UnknownRefsCounted) {
    TempItchFile f("unknown_refs");
    f.add_message(make_R(1, "SPY     "));
    // Execute a ref that was never added.
    f.add_message(make_E(1, 9999, 100));
    // Delete a ref that was never added.
    f.add_message(make_D(1, 8888));
    f.write();

    ReplayEngine eng;
    auto stats = eng.run(f.path());

    EXPECT_EQ(stats.unknown_refs, 2u);
}

// ── Symbol filter: only tracked symbols get a book ───────────────────────────

TEST(ReplayEngine, SymbolFilterIgnoresOthers) {
    TempItchFile f("sym_filter");
    f.add_message(make_R(1, "AAPL    "));
    f.add_message(make_R(2, "GOOG    "));
    f.add_message(make_A(1, 1, 'B', 100, "AAPL    ", 1500000));
    f.add_message(make_A(2, 2, 'B', 200, "GOOG    ", 1400000));
    f.write();

    ReplayEngine eng(Config{.symbols = {"AAPL"}});
    auto stats = eng.run(f.path());

    // Only AAPL was tracked; GOOG add was silently ignored.
    EXPECT_EQ(stats.orders_added, 1u);
    EXPECT_NE(eng.book("AAPL"), nullptr);
    EXPECT_EQ(eng.book("GOOG"), nullptr);
}

// ── Multiple price levels: best_bid/ask and depth are correct ─────────────────

TEST(ReplayEngine, MultiplePriceLevelsCorrectSnapshot) {
    TempItchFile f("multi_level");
    f.add_message(make_R(1, "TSLA    "));
    // Bids at 100 and 99 (100 is best bid).
    f.add_message(make_A(1, 1, 'B', 300, "TSLA    ", 1000000));  // bid@100
    f.add_message(make_A(1, 2, 'B', 200, "TSLA    ", 990000));   // bid@99
    // Asks at 101 and 102 (101 is best ask).
    f.add_message(make_A(1, 3, 'S', 150, "TSLA    ", 1010000));  // ask@101
    f.add_message(make_A(1, 4, 'S', 100, "TSLA    ", 1020000));  // ask@102
    f.write();

    ReplayEngine eng;
    eng.run(f.path());

    const OrderBook* bk = eng.book("TSLA");
    ASSERT_NE(bk, nullptr);

    ASSERT_TRUE(bk->best_bid().has_value());
    ASSERT_TRUE(bk->best_ask().has_value());
    EXPECT_EQ(to_int(*bk->best_bid()), 1000000);  // best bid = 100
    EXPECT_EQ(to_int(*bk->best_ask()), 1010000);  // best ask = 101

    auto snaps = eng.snapshots();
    ASSERT_EQ(snaps.size(), 1u);
    EXPECT_EQ(snaps[0].bid_depth, 300u);
    EXPECT_EQ(snaps[0].ask_depth, 150u);
}

// ── Book depth consistency: qty never exceeds original after partial fills ─────

TEST(ReplayEngine, QtyNeverExceedsOriginal) {
    TempItchFile f("qty_check");
    f.add_message(make_R(1, "META    "));
    f.add_message(make_A(1, 10, 'S', 1000, "META    ", 3500000));
    f.add_message(make_E(1, 10, 400));  // fill 400
    f.add_message(make_E(1, 10, 300));  // fill 300 more
    f.write();

    ReplayEngine eng;
    eng.run(f.path());

    const OrderBook* bk = eng.book("META");
    ASSERT_NE(bk, nullptr);

    const Order* o = bk->find(OrderId{10});
    ASSERT_NE(o, nullptr);
    EXPECT_LE(to_uint(o->remaining_quantity), 1000u);
    EXPECT_EQ(to_uint(o->remaining_quantity), 300u);
}

// ── book() returns nullptr for unknown symbol ─────────────────────────────────

TEST(ReplayEngine, BookNullForUnknownSymbol) {
    TempItchFile f("unknown_sym");
    f.add_message(make_R(1, "AAPL    "));
    f.write();

    ReplayEngine eng;
    eng.run(f.path());

    // R message registers the symbol, so AAPL book is non-null.
    EXPECT_NE(eng.book("AAPL"), nullptr);
    // A symbol never seen in any message is null.
    EXPECT_EQ(eng.book("XYZ"), nullptr);
}

// ── Replay is idempotent (reset between runs) ─────────────────────────────────

TEST(ReplayEngine, SecondRunResetsState) {
    TempItchFile f("reset");
    f.add_message(make_R(1, "NFLX    "));
    f.add_message(make_A(1, 1, 'B', 100, "NFLX    ", 6000000));
    f.write();

    ReplayEngine eng;
    eng.run(f.path());
    eng.run(f.path());  // second run — state must be clean

    const OrderBook* bk = eng.book("NFLX");
    ASSERT_NE(bk, nullptr);

    // If state was not reset, we'd see 2 copies of the order (duplicate ids).
    // The book should contain exactly one order.
    auto depth = eng.snapshots();
    ASSERT_EQ(depth.size(), 1u);
    EXPECT_EQ(depth[0].bid_depth, 100u);
}
