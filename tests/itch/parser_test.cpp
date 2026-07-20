#include "lob/itch/parser.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

namespace {

// ── Synthetic ITCH file builder ───────────────────────────────────────────────

// Builds a raw ITCH 5.0 byte stream in memory and writes it to a temp file.
// Each call to add_message() wraps a body vector with the 2-byte length prefix.
class TempItchFile {
   public:
    explicit TempItchFile(const std::string& tag)
        : path_(std::filesystem::temp_directory_path() /
                ("itch_test_" + tag + ".bin")) {}

    ~TempItchFile() { std::filesystem::remove(path_); }

    TempItchFile(const TempItchFile&) = delete;
    TempItchFile& operator=(const TempItchFile&) = delete;

    // Append a framed message body to the stream.
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

// ── Helper builders ───────────────────────────────────────────────────────────

// Common sentinel values reused across tests.
static constexpr uint16_t kLocate   = 42;
static constexpr uint16_t kTracking = 7;
static constexpr uint64_t kTs       = 0x123456789ABCULL;  // 6 bytes, all non-trivial

// Write a big-endian uint16 into v.
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

// Emit standard 11-byte header block (type + locate + tracking + 6-byte ts).
static void push_header(std::vector<uint8_t>& v, char type) {
    v.push_back(static_cast<uint8_t>(type));
    push_u16(v, kLocate);
    push_u16(v, kTracking);
    push_u48(v, kTs);
}

}  // namespace

using namespace lob::itch;

// ── System Event ─────────────────────────────────────────────────────────────

TEST(ItchParser, SystemEvent) {
    TempItchFile f("system_event");
    std::vector<uint8_t> body;
    push_header(body, 'S');
    body.push_back('O');  // event_code
    f.add_message(body);
    f.write();

    MsgSystemEvent result{};
    ItchHandler h;
    h.on_system_event = [&](const MsgSystemEvent& m) { result = m; };
    auto stats = parse_file(f.path(), h);

    EXPECT_EQ(stats.messages_parsed, 1u);
    EXPECT_EQ(stats.messages_skipped, 0u);
    EXPECT_EQ(result.hdr.stock_locate,    kLocate);
    EXPECT_EQ(result.hdr.tracking_number, kTracking);
    EXPECT_EQ(result.hdr.timestamp_ns,    kTs);
    EXPECT_EQ(result.event_code, 'O');
}

// ── Stock Directory ───────────────────────────────────────────────────────────

TEST(ItchParser, StockDirectory) {
    TempItchFile f("stock_dir");
    std::vector<uint8_t> body;
    push_header(body, 'R');
    push_bytes(body, "AAPL    ", 8);  // stock (right-padded)
    body.push_back('Q');              // market_category
    body.push_back('N');              // financial_status
    push_u32(body, 100u);            // round_lot_size
    body.push_back('N');              // round_lots_only
    body.push_back('C');              // issue_classification
    body.push_back('S'); body.push_back('1');  // issue_sub_type
    body.push_back('P');              // authenticity
    body.push_back('N');              // short_sale_threshold
    body.push_back(' ');              // ipo_flag
    body.push_back('1');              // luld_tier
    body.push_back('N');              // etp_flag
    push_u32(body, 0u);              // etp_leverage_factor
    body.push_back('N');              // inverse_indicator
    ASSERT_EQ(body.size(), 39u);
    f.add_message(body);
    f.write();

    MsgStockDirectory result{};
    ItchHandler h;
    h.on_stock_directory = [&](const MsgStockDirectory& m) { result = m; };
    auto stats = parse_file(f.path(), h);

    EXPECT_EQ(stats.messages_parsed, 1u);
    EXPECT_EQ(trim_stock(result.stock), "AAPL");
    EXPECT_EQ(result.hdr.stock_locate, kLocate);
    EXPECT_EQ(result.market_category, 'Q');
    EXPECT_EQ(result.round_lot_size, 100u);
    EXPECT_EQ(result.issue_sub_type[0], 'S');
    EXPECT_EQ(result.issue_sub_type[1], '1');
}

// ── Add Order (no MPID) ───────────────────────────────────────────────────────

TEST(ItchParser, AddOrderNoMpid) {
    TempItchFile f("add_order_a");
    std::vector<uint8_t> body;
    push_header(body, 'A');
    push_u64(body, 0xCAFEBABEDEADBEEFULL);  // ref
    body.push_back('B');                      // side = Buy
    push_u32(body, 500u);                    // shares
    push_bytes(body, "MSFT    ", 8);         // stock
    push_u32(body, 3012345u);               // price (= $301.2345)
    ASSERT_EQ(body.size(), 36u);
    f.add_message(body);
    f.write();

    MsgAddOrder result{};
    ItchHandler h;
    h.on_add_order = [&](const MsgAddOrder& m) { result = m; };
    auto stats = parse_file(f.path(), h);

    EXPECT_EQ(stats.messages_parsed, 1u);
    EXPECT_EQ(result.ref,    0xCAFEBABEDEADBEEFULL);
    EXPECT_EQ(result.side,   'B');
    EXPECT_EQ(result.shares, 500u);
    EXPECT_EQ(trim_stock(result.stock), "MSFT");
    EXPECT_EQ(result.price,  3012345u);
    EXPECT_FALSE(result.has_mpid);
}

// ── Add Order with MPID ───────────────────────────────────────────────────────

TEST(ItchParser, AddOrderWithMpid) {
    TempItchFile f("add_order_f");
    std::vector<uint8_t> body;
    push_header(body, 'F');
    push_u64(body, 0x1122334455667788ULL);  // ref
    body.push_back('S');                      // side = Sell
    push_u32(body, 200u);                    // shares
    push_bytes(body, "GOOG    ", 8);         // stock
    push_u32(body, 28000000u);              // price
    push_bytes(body, "MLCO", 4);            // mpid
    ASSERT_EQ(body.size(), 40u);
    f.add_message(body);
    f.write();

    MsgAddOrder result{};
    ItchHandler h;
    h.on_add_order = [&](const MsgAddOrder& m) { result = m; };
    parse_file(f.path(), h);

    EXPECT_EQ(result.ref,    0x1122334455667788ULL);
    EXPECT_EQ(result.side,   'S');
    EXPECT_EQ(result.shares, 200u);
    EXPECT_EQ(result.price,  28000000u);
    EXPECT_TRUE(result.has_mpid);
    EXPECT_EQ(trim_stock(result.mpid), "MLCO");
}

// ── Order Executed ────────────────────────────────────────────────────────────

TEST(ItchParser, OrderExecuted) {
    TempItchFile f("order_exec_e");
    std::vector<uint8_t> body;
    push_header(body, 'E');
    push_u64(body, 0xAABBCCDDEEFF0011ULL);  // ref
    push_u32(body, 300u);                    // executed_shares
    push_u64(body, 0x0102030405060708ULL);   // match_number
    ASSERT_EQ(body.size(), 31u);
    f.add_message(body);
    f.write();

    MsgOrderExecuted result{};
    ItchHandler h;
    h.on_order_executed = [&](const MsgOrderExecuted& m) { result = m; };
    auto stats = parse_file(f.path(), h);

    EXPECT_EQ(stats.messages_parsed, 1u);
    EXPECT_EQ(result.ref,             0xAABBCCDDEEFF0011ULL);
    EXPECT_EQ(result.executed_shares, 300u);
    EXPECT_EQ(result.match_number,    0x0102030405060708ULL);
}

// ── Order Executed with Price ─────────────────────────────────────────────────

TEST(ItchParser, OrderExecutedWithPrice) {
    TempItchFile f("order_exec_c");
    std::vector<uint8_t> body;
    push_header(body, 'C');
    push_u64(body, 0x0001020304050607ULL);  // ref
    push_u32(body, 100u);                   // executed_shares
    push_u64(body, 0x1111222233334444ULL);  // match_number
    body.push_back('Y');                     // printable
    push_u32(body, 5000000u);               // exec_price
    ASSERT_EQ(body.size(), 36u);
    f.add_message(body);
    f.write();

    MsgOrderExecutedWithPrice result{};
    ItchHandler h;
    h.on_order_executed_with_price = [&](const MsgOrderExecutedWithPrice& m) { result = m; };
    auto stats = parse_file(f.path(), h);

    EXPECT_EQ(stats.messages_parsed, 1u);
    EXPECT_EQ(result.ref,             0x0001020304050607ULL);
    EXPECT_EQ(result.executed_shares, 100u);
    EXPECT_EQ(result.match_number,    0x1111222233334444ULL);
    EXPECT_EQ(result.printable,       'Y');
    EXPECT_EQ(result.exec_price,      5000000u);
}

// ── Order Cancel ──────────────────────────────────────────────────────────────

TEST(ItchParser, OrderCancel) {
    TempItchFile f("order_cancel_x");
    std::vector<uint8_t> body;
    push_header(body, 'X');
    push_u64(body, 0xDEADBEEF12345678ULL);  // ref
    push_u32(body, 50u);                     // cancelled_shares
    ASSERT_EQ(body.size(), 23u);
    f.add_message(body);
    f.write();

    MsgOrderCancel result{};
    ItchHandler h;
    h.on_order_cancel = [&](const MsgOrderCancel& m) { result = m; };
    auto stats = parse_file(f.path(), h);

    EXPECT_EQ(stats.messages_parsed, 1u);
    EXPECT_EQ(result.ref,               0xDEADBEEF12345678ULL);
    EXPECT_EQ(result.cancelled_shares,  50u);
}

// ── Order Delete ──────────────────────────────────────────────────────────────

TEST(ItchParser, OrderDelete) {
    TempItchFile f("order_delete_d");
    std::vector<uint8_t> body;
    push_header(body, 'D');
    push_u64(body, 0xFEDCBA9876543210ULL);  // ref
    ASSERT_EQ(body.size(), 19u);
    f.add_message(body);
    f.write();

    MsgOrderDelete result{};
    ItchHandler h;
    h.on_order_delete = [&](const MsgOrderDelete& m) { result = m; };
    auto stats = parse_file(f.path(), h);

    EXPECT_EQ(stats.messages_parsed, 1u);
    EXPECT_EQ(result.ref, 0xFEDCBA9876543210ULL);
    EXPECT_EQ(result.hdr.stock_locate, kLocate);
}

// ── Order Replace ─────────────────────────────────────────────────────────────

TEST(ItchParser, OrderReplace) {
    TempItchFile f("order_replace_u");
    std::vector<uint8_t> body;
    push_header(body, 'U');
    push_u64(body, 0xAAAAAAAABBBBBBBBULL);  // orig_ref
    push_u64(body, 0xCCCCCCCCDDDDDDDDULL);  // new_ref
    push_u32(body, 1000u);                   // shares
    push_u32(body, 9999999u);               // price
    ASSERT_EQ(body.size(), 35u);
    f.add_message(body);
    f.write();

    MsgOrderReplace result{};
    ItchHandler h;
    h.on_order_replace = [&](const MsgOrderReplace& m) { result = m; };
    auto stats = parse_file(f.path(), h);

    EXPECT_EQ(stats.messages_parsed, 1u);
    EXPECT_EQ(result.orig_ref, 0xAAAAAAAABBBBBBBBULL);
    EXPECT_EQ(result.new_ref,  0xCCCCCCCCDDDDDDDDULL);
    EXPECT_EQ(result.shares,   1000u);
    EXPECT_EQ(result.price,    9999999u);
}

// ── Trade Message ─────────────────────────────────────────────────────────────

TEST(ItchParser, TradeMessage) {
    TempItchFile f("trade_p");
    std::vector<uint8_t> body;
    push_header(body, 'P');
    push_u64(body, 0u);                      // ref (may be 0 for cross)
    body.push_back('B');                      // side
    push_u32(body, 2500u);                   // shares
    push_bytes(body, "NVDA    ", 8);         // stock
    push_u32(body, 7654321u);               // price
    push_u64(body, 0x9988776655443322ULL);   // match_number
    ASSERT_EQ(body.size(), 44u);
    f.add_message(body);
    f.write();

    MsgTrade result{};
    ItchHandler h;
    h.on_trade = [&](const MsgTrade& m) { result = m; };
    auto stats = parse_file(f.path(), h);

    EXPECT_EQ(stats.messages_parsed, 1u);
    EXPECT_EQ(result.ref,          0u);
    EXPECT_EQ(result.side,         'B');
    EXPECT_EQ(result.shares,       2500u);
    EXPECT_EQ(trim_stock(result.stock), "NVDA");
    EXPECT_EQ(result.price,        7654321u);
    EXPECT_EQ(result.match_number, 0x9988776655443322ULL);
}

// ── 48-bit timestamp: verify all 6 wire bytes contribute to the value ─────────

TEST(ItchParser, Timestamp48BitAllBytesDecoded) {
    // Use a timestamp that has non-zero bits spread across all 6 bytes.
    // 0xA0B1C2D3E4F5 has a non-zero high byte (0xA0) which would be lost
    // if only the lower 4 or 5 bytes were read.
    const uint64_t ts = 0xA0B1C2D3E4F5ULL;

    TempItchFile f("ts48");
    std::vector<uint8_t> body;
    body.push_back(static_cast<uint8_t>('S'));  // type
    push_u16(body, kLocate);
    push_u16(body, kTracking);
    push_u48(body, ts);                          // 6-byte timestamp
    body.push_back('S');                          // event_code
    ASSERT_EQ(body.size(), 12u);
    f.add_message(body);
    f.write();

    uint64_t got_ts = 0;
    ItchHandler h;
    h.on_system_event = [&](const MsgSystemEvent& m) { got_ts = m.hdr.timestamp_ns; };
    parse_file(f.path(), h);

    EXPECT_EQ(got_ts, ts);
}

// ── Big-endian u32: byte order must be big-endian, not little-endian ──────────

TEST(ItchParser, BigEndianU32Price) {
    // price bytes on the wire: 0x01 0x23 0x45 0x67
    // Big-endian value = 0x01234567 = 19088743
    // Little-endian interpretation would be 0x67452301 = 1732584193
    const uint32_t expected_price = 0x01234567u;

    TempItchFile f("be_u32");
    std::vector<uint8_t> body;
    push_header(body, 'A');
    push_u64(body, 1u);         // ref
    body.push_back('B');         // side
    push_u32(body, 1u);         // shares
    push_bytes(body, "TEST    ", 8);
    push_u32(body, expected_price);
    ASSERT_EQ(body.size(), 36u);
    f.add_message(body);
    f.write();

    uint32_t got_price = 0;
    ItchHandler h;
    h.on_add_order = [&](const MsgAddOrder& m) { got_price = m.price; };
    parse_file(f.path(), h);

    EXPECT_EQ(got_price, expected_price);
}

// ── Unknown message type increments skipped count ─────────────────────────────

TEST(ItchParser, UnknownMessageTypeSkipped) {
    TempItchFile f("unknown");
    std::vector<uint8_t> body;
    body.push_back('Z');  // not a real ITCH type
    push_u16(body, kLocate);
    push_u16(body, kTracking);
    push_u48(body, kTs);
    body.push_back(0x00);  // padding
    f.add_message(body);
    f.write();

    ItchHandler h;  // no callbacks set
    auto stats = parse_file(f.path(), h);

    EXPECT_EQ(stats.messages_parsed,  0u);
    EXPECT_EQ(stats.messages_skipped, 1u);
}

// ── Multiple messages in sequence are all decoded ─────────────────────────────

TEST(ItchParser, MultipleMessagesDecoded) {
    TempItchFile f("multi");

    // Message 1: System Event
    {
        std::vector<uint8_t> b;
        push_header(b, 'S');
        b.push_back('O');
        f.add_message(b);
    }
    // Message 2: Add Order
    {
        std::vector<uint8_t> b;
        push_header(b, 'A');
        push_u64(b, 42u);
        b.push_back('B');
        push_u32(b, 10u);
        push_bytes(b, "IBM     ", 8);
        push_u32(b, 1500000u);
        f.add_message(b);
    }
    // Message 3: Order Delete
    {
        std::vector<uint8_t> b;
        push_header(b, 'D');
        push_u64(b, 42u);
        f.add_message(b);
    }
    f.write();

    int system_count = 0, add_count = 0, delete_count = 0;
    ItchHandler h;
    h.on_system_event = [&](const MsgSystemEvent&) { ++system_count; };
    h.on_add_order    = [&](const MsgAddOrder&)    { ++add_count;    };
    h.on_order_delete = [&](const MsgOrderDelete&) { ++delete_count; };
    auto stats = parse_file(f.path(), h);

    EXPECT_EQ(stats.messages_parsed, 3u);
    EXPECT_EQ(system_count, 1);
    EXPECT_EQ(add_count,    1);
    EXPECT_EQ(delete_count, 1);
}

// ── trim_stock helper ─────────────────────────────────────────────────────────

TEST(ItchParser, TrimStockRemovesTrailingSpaces) {
    std::array<char, 8> raw{};
    std::memcpy(raw.data(), "AAPL    ", 8);
    EXPECT_EQ(trim_stock(raw), "AAPL");
}

TEST(ItchParser, TrimStockFullWidth) {
    std::array<char, 8> raw{};
    std::memcpy(raw.data(), "ABCDEFGH", 8);
    EXPECT_EQ(trim_stock(raw), "ABCDEFGH");
}

TEST(ItchParser, TrimStockAllSpaces) {
    std::array<char, 8> raw{};
    raw.fill(' ');
    EXPECT_EQ(trim_stock(raw), "");
}

// ── NullCallbacksDoNotCrash ────────────────────────────────────────────────────
// Ensure setting no callbacks and parsing a file doesn't crash.

TEST(ItchParser, NullCallbacksDoNotCrash) {
    TempItchFile f("null_cb");
    std::vector<uint8_t> body;
    push_header(body, 'A');
    push_u64(body, 1u);
    body.push_back('B');
    push_u32(body, 1u);
    push_bytes(body, "SPY     ", 8);
    push_u32(body, 5000000u);
    f.add_message(body);
    f.write();

    ItchHandler h;  // all callbacks null
    EXPECT_NO_THROW(parse_file(f.path(), h));
}
