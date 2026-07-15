// Performance benchmarks for the matching engine.
// No external framework needed — just std::chrono.
// Always run in Release mode or the numbers will be meaningless:
//   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
//   cmake --build build --parallel
//   ./build/benchmarks/lob_bench

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "lob/matching_engine.hpp"

using namespace lob;
using Clock    = std::chrono::high_resolution_clock;
using NsCount  = std::chrono::nanoseconds;

// Accumulate results into a volatile so the compiler can't prove the work
// is dead and silently delete the whole benchmark loop.
static volatile uint64_t g_sink{0};

// Pre-allocate all Order objects up front so we're not measuring malloc
// inside the hot loop. The engine never deletes orders — we own them.
struct Pool {
    std::vector<Order> store;
    size_t             next{0};

    explicit Pool(size_t capacity) : store(capacity) {}

    Order* take(uint64_t id, Side side, OrderType type, int64_t price,
                uint64_t qty, uint64_t ts = 0) noexcept {
        assert(next < store.size());
        Order& o             = store[next++];
        o.id                 = OrderId{id};
        o.side               = side;
        o.type               = type;
        o.price              = Price{price};
        o.quantity           = Quantity{qty};
        o.remaining_quantity = Quantity{qty};
        o.timestamp          = Timestamp{ts};
        o.prev               = nullptr;
        o.next               = nullptr;
        return &o;
    }

    void reset() noexcept { next = 0; }
};

// Bundles the engine with a trade counter. Kept in one struct so the lambda
// captures a stable pointer (moving this struct would dangle the capture).
struct EngineCtx {
    size_t        trades{0};
    MatchingEngine engine;

    EngineCtx() : engine(make_handler()) {}

    // Non-copyable; move only.
    EngineCtx(const EngineCtx&)            = delete;
    EngineCtx& operator=(const EngineCtx&) = delete;
    EngineCtx(EngineCtx&&)                 = delete;
    EngineCtx& operator=(EngineCtx&&)      = delete;

   private:
    EventHandler make_handler() {
        return {
            [this](const TradeEvent& e) {
                ++trades;
                g_sink += to_uint(e.quantity);
            },
            nullptr, nullptr, nullptr,
        };
    }
};

// Sort the latency samples and pick the value at position p (0.0–1.0).
static double percentile(std::vector<int64_t>& v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p * static_cast<double>(v.size() - 1));
    return static_cast<double>(v[idx]);
}

// ── Result ────────────────────────────────────────────────────────────────────

struct BenchResult {
    std::string scenario;
    size_t      ops{0};
    double      runtime_ms{0.0};
    double      ops_per_sec{0.0};
    double      avg_ns{0.0};
    double      p50_ns{0.0};
    double      p95_ns{0.0};
    double      p99_ns{0.0};
    size_t      trades{0};
    size_t      book_levels{0};
};

static BenchResult make_result(const std::string& name, size_t N,
                               std::vector<int64_t>& lats, size_t trades,
                               size_t levels) {
    double total_ns = static_cast<double>(
        std::accumulate(lats.begin(), lats.end(), int64_t{0}));
    double ms = total_ns / 1e6;

    BenchResult r;
    r.scenario    = name;
    r.ops         = N;
    r.runtime_ms  = ms;
    r.ops_per_sec = static_cast<double>(N) / (ms / 1e3);
    r.avg_ns      = total_ns / static_cast<double>(N);
    r.p50_ns      = percentile(lats, 0.50);
    r.p95_ns      = percentile(lats, 0.95);
    r.p99_ns      = percentile(lats, 0.99);
    r.trades      = trades;
    r.book_levels = levels;
    return r;
}

// ── Scenario helpers ──────────────────────────────────────────────────────────

// Count total resting levels across both sides.
static size_t book_levels(const OrderBook& b) {
    return b.bid_depth(1'000'000).size() + b.ask_depth(1'000'000).size();
}

// ── Scenario 1: AddOnly ───────────────────────────────────────────────────────
// Submit N non-crossing limit orders (alternating buys 50-99, sells 101-150).
// Tests pure insertion throughput into std::map levels.

static BenchResult bench_add_only(size_t N, uint32_t seed) {
    // Pre-generate order parameters.
    struct Spec { Side side; int64_t price; };
    std::vector<Spec> specs(N);
    {
        std::mt19937 rng(seed);
        for (size_t i = 0; i < N; ++i) {
            if (i % 2 == 0) {
                specs[i] = {Side::Buy,  50 + static_cast<int64_t>(rng() % 50)};
            } else {
                specs[i] = {Side::Sell, 101 + static_cast<int64_t>(rng() % 50)};
            }
        }
    }

    Pool pool(N);

    // Warmup (1 000 ops, result discarded).
    {
        Pool wp(1000);
        EngineCtx wctx;
        for (size_t i = 0; i < 1000 && i < N; ++i) {
            wctx.engine.submit(
                wp.take(i + 1, specs[i].side, OrderType::Limit,
                        specs[i].price, 10, i));
        }
        g_sink += static_cast<uint64_t>(wctx.trades);
    }

    // Measured run.
    EngineCtx ctx;
    std::vector<int64_t> lats;
    lats.reserve(N);

    for (size_t i = 0; i < N; ++i) {
        Order* o = pool.take(i + 1, specs[i].side, OrderType::Limit,
                             specs[i].price, 10, i);
        auto t0 = Clock::now();
        ctx.engine.submit(o);
        auto t1 = Clock::now();
        lats.push_back(
            std::chrono::duration_cast<NsCount>(t1 - t0).count());
    }

    size_t depth = book_levels(ctx.engine.book());
    g_sink += static_cast<uint64_t>(depth);
    return make_result("AddOnly", N, lats, ctx.trades, depth);
}

// ── Scenario 2: CrossSpread ───────────────────────────────────────────────────
// Pre-load N resting sell orders at price 101.
// Then time N aggressive buy limit orders at price 101, each crossing one sell.
// Tests the full match path including trade event dispatch.

static BenchResult bench_cross_spread(size_t N, uint32_t seed) {
    // We need 2*N order slots: N for resting sells + N for crossing buys.
    Pool pool(2 * N);

    // Setup: populate book with N resting sells (not timed).
    {
        // Use a separate ctx just for setup, then move into bench ctx.
        // Actually, we need the resting orders to still be in the book
        // when we time the crossing orders, so we use one ctx.
    }

    // Warmup.
    {
        Pool wp(2000);
        EngineCtx wctx;
        for (size_t i = 0; i < 500; ++i) {
            wctx.engine.submit(
                wp.take(i + 1, Side::Sell, OrderType::Limit, 101, 1, i));
        }
        for (size_t i = 0; i < 500; ++i) {
            wctx.engine.submit(
                wp.take(500 + i + 1, Side::Buy, OrderType::Limit, 101, 1, 500 + i));
        }
        g_sink += static_cast<uint64_t>(wctx.trades);
    }

    // Pre-populate: N resting sell orders at price 101, qty 1 each (not timed).
    EngineCtx ctx;
    for (size_t i = 0; i < N; ++i) {
        ctx.engine.submit(
            pool.take(i + 1, Side::Sell, OrderType::Limit, 101, 1, i));
    }
    ctx.trades = 0;  // reset counter — setup trades don't count

    // Timed: N crossing buy orders at price 101 (each fills one resting sell).
    std::vector<int64_t> lats;
    lats.reserve(N);
    std::mt19937 rng(seed);  // unused here but keeps signature consistent
    (void)rng;

    for (size_t i = 0; i < N; ++i) {
        Order* o = pool.take(N + i + 1, Side::Buy, OrderType::Limit, 101, 1, N + i);
        auto t0 = Clock::now();
        ctx.engine.submit(o);
        auto t1 = Clock::now();
        lats.push_back(
            std::chrono::duration_cast<NsCount>(t1 - t0).count());
    }

    size_t depth = book_levels(ctx.engine.book());
    g_sink += static_cast<uint64_t>(depth);
    return make_result("CrossSpread", N, lats, ctx.trades, depth);
}

// ── Scenario 3: CancelOnly ────────────────────────────────────────────────────
// Submit N resting limit orders (setup), then time N cancels.
// Tests cancel throughput: unordered_map lookup + intrusive list removal.

static BenchResult bench_cancel(size_t N, uint32_t seed) {
    struct Spec { Side side; int64_t price; };
    std::vector<Spec> specs(N);
    {
        std::mt19937 rng(seed);
        for (size_t i = 0; i < N; ++i) {
            if (i % 2 == 0) {
                specs[i] = {Side::Buy,  50 + static_cast<int64_t>(rng() % 50)};
            } else {
                specs[i] = {Side::Sell, 101 + static_cast<int64_t>(rng() % 50)};
            }
        }
    }

    Pool pool(N);

    // Warmup.
    {
        Pool wp(1000);
        EngineCtx wctx;
        for (size_t i = 0; i < 1000 && i < N; ++i) {
            wctx.engine.submit(
                wp.take(i + 1, specs[i].side, OrderType::Limit,
                        specs[i].price, 10, i));
        }
        for (size_t i = 0; i < 1000 && i < N; ++i) {
            wctx.engine.cancel(OrderId{i + 1});
        }
        g_sink += static_cast<uint64_t>(wctx.trades);
    }

    // Setup: submit N resting orders (not timed).
    EngineCtx ctx;
    for (size_t i = 0; i < N; ++i) {
        ctx.engine.submit(pool.take(i + 1, specs[i].side, OrderType::Limit,
                                   specs[i].price, 10, i));
    }

    // Timed: cancel all N.
    std::vector<int64_t> lats;
    lats.reserve(N);

    for (size_t i = 0; i < N; ++i) {
        auto t0 = Clock::now();
        ctx.engine.cancel(OrderId{i + 1});
        auto t1 = Clock::now();
        lats.push_back(
            std::chrono::duration_cast<NsCount>(t1 - t0).count());
    }

    size_t depth = book_levels(ctx.engine.book());
    g_sink += static_cast<uint64_t>(depth);
    return make_result("CancelOnly", N, lats, ctx.trades, depth);
}

// ── Scenario 4: Mixed (70% add, 20% cross, 10% cancel) ───────────────────────
// Tracks live resting order IDs to enable realistic cancels.
// Crossing orders are aggressive buys/sells against the opposite side.

static BenchResult bench_mixed(size_t N, uint32_t seed) {
    // Pool needs headroom: every op could be an add.
    Pool pool(N + 1024);

    // Pre-seed the book so crossing orders have something to match against.
    constexpr size_t PRESEED = 512;
    for (size_t i = 0; i < PRESEED && i < N / 2; ++i) {
        pool.take(i + 1, Side::Buy,  OrderType::Limit, 98,  10, i);
        pool.take(PRESEED + i + 1, Side::Sell, OrderType::Limit, 102, 10, i);
    }

    // Warmup.
    {
        Pool wp(2200);
        EngineCtx wctx;
        std::vector<uint64_t> live;
        live.reserve(2200);
        uint64_t wid = 1;
        std::mt19937 wrng(seed ^ 0xDEADBEEF);
        for (size_t i = 0; i < 1000; ++i) {
            int r = static_cast<int>(wrng() % 10);
            if (r < 7 || live.empty()) {
                Side    s = (wrng() % 2) ? Side::Buy : Side::Sell;
                int64_t p = (s == Side::Buy)
                                ? 50 + static_cast<int64_t>(wrng() % 40)
                                : 110 + static_cast<int64_t>(wrng() % 40);
                Order* o = wp.take(wid, s, OrderType::Limit, p, 5, i);
                wctx.engine.submit(o);
                if (wctx.engine.book().find(OrderId{wid}) != nullptr) {
                    live.push_back(wid);
                }
                ++wid;
            } else if (r < 9) {
                Side s = (wrng() % 2) ? Side::Buy : Side::Sell;
                int64_t p = (s == Side::Buy) ? 200 : 1;
                Order* o = wp.take(wid++, s, OrderType::Market, p, 1, i);
                wctx.engine.submit(o);
                // No O(N) scan — cancelled orders may be rejected, which is fine for warmup.
            } else {
                uint64_t cid = live.back();
                live.pop_back();
                wctx.engine.cancel(OrderId{cid});
            }
        }
        g_sink += static_cast<uint64_t>(wctx.trades);
    }

    // Measured run.
    Pool mpool(N + 1024);
    EngineCtx ctx;
    std::mt19937 rng(seed);
    std::vector<uint64_t> live_ids;
    live_ids.reserve(N);
    std::vector<int64_t> lats;
    lats.reserve(N);
    uint64_t next_id = 1;

    for (size_t i = 0; i < N; ++i) {
        int roll = static_cast<int>(rng() % 10);

        auto t0 = Clock::now();

        if (roll < 7 || live_ids.empty()) {
            // 70% add non-crossing limit.
            Side    s = (rng() % 2) ? Side::Buy : Side::Sell;
            int64_t p = (s == Side::Buy)
                            ? 50 + static_cast<int64_t>(rng() % 40)
                            : 110 + static_cast<int64_t>(rng() % 40);
            Order* o = mpool.take(next_id, s, OrderType::Limit, p, 5, i);
            ctx.engine.submit(o);
            if (ctx.engine.book().find(OrderId{next_id}) != nullptr) {
                live_ids.push_back(next_id);
            }
            ++next_id;
        } else if (roll < 9) {
            // 20% crossing (marketable) order.
            Side   s = (rng() % 2) ? Side::Buy : Side::Sell;
            Order* o = mpool.take(next_id, s, OrderType::Market, 0, 1, i);
            ctx.engine.submit(o);
            ++next_id;
            // Do NOT scan live_ids here — O(N) per op turns the loop O(N²).
            // Cancels may occasionally hit already-filled IDs and fire Rejected,
            // which is acceptable in a mixed-workload benchmark.
        } else {
            // 10% cancel.
            uint64_t cid = live_ids.back();
            live_ids.pop_back();
            ctx.engine.cancel(OrderId{cid});
        }

        auto t1 = Clock::now();
        lats.push_back(
            std::chrono::duration_cast<NsCount>(t1 - t0).count());
    }

    size_t depth = book_levels(ctx.engine.book());
    g_sink += static_cast<uint64_t>(depth);
    return make_result("Mixed", N, lats, ctx.trades, depth);
}

// ── Output ────────────────────────────────────────────────────────────────────

static void print_header() {
    std::cout
        << "\n"
        << std::left
        << std::setw(16) << "Scenario"
        << std::right
        << std::setw(10) << "Ops"
        << std::setw(14) << "Runtime(ms)"
        << std::setw(14) << "Orders/sec"
        << std::setw(11) << "Avg(ns)"
        << std::setw(10) << "p50(ns)"
        << std::setw(10) << "p95(ns)"
        << std::setw(10) << "p99(ns)"
        << std::setw(9)  << "Trades"
        << std::setw(9)  << "Levels"
        << "\n"
        << std::string(113, '-') << "\n";
}

static void print_row(const BenchResult& r) {
    std::cout
        << std::left  << std::setw(16) << r.scenario
        << std::right
        << std::setw(10) << r.ops
        << std::setw(14) << std::fixed << std::setprecision(2) << r.runtime_ms
        << std::setw(14) << std::fixed << std::setprecision(0) << r.ops_per_sec
        << std::setw(11) << std::fixed << std::setprecision(1) << r.avg_ns
        << std::setw(10) << std::fixed << std::setprecision(0) << r.p50_ns
        << std::setw(10) << std::fixed << std::setprecision(0) << r.p95_ns
        << std::setw(10) << std::fixed << std::setprecision(0) << r.p99_ns
        << std::setw(9)  << r.trades
        << std::setw(9)  << r.book_levels
        << "\n";
}

static void write_csv(const std::vector<BenchResult>& results,
                      const std::string& path) {
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "scenario,ops,runtime_ms,orders_per_sec,avg_ns,p50_ns,p95_ns,p99_ns,"
         "trades,book_levels\n";
    for (const auto& r : results) {
        f << r.scenario      << ","
          << r.ops           << ","
          << std::fixed << std::setprecision(3) << r.runtime_ms  << ","
          << std::fixed << std::setprecision(0) << r.ops_per_sec << ","
          << std::fixed << std::setprecision(1) << r.avg_ns      << ","
          << std::fixed << std::setprecision(0) << r.p50_ns      << ","
          << std::fixed << std::setprecision(0) << r.p95_ns      << ","
          << std::fixed << std::setprecision(0) << r.p99_ns      << ","
          << r.trades        << ","
          << r.book_levels   << "\n";
    }
    std::cout << "Results written to " << path << "\n";
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Allow skipping 1M run with --fast flag (useful in CI).
    bool fast = (argc > 1 && std::string(argv[1]) == "--fast");

    constexpr uint32_t SEED = 42;
    const std::vector<size_t> sizes = fast
        ? std::vector<size_t>{10'000, 100'000}
        : std::vector<size_t>{10'000, 100'000, 1'000'000};

    std::cout << "lob-engine benchmark  (seed=" << SEED << ")\n";
    if (fast) std::cout << "[--fast mode: skipping 1M run]\n";
    std::cout << "Run with -DCMAKE_BUILD_TYPE=Release for accurate numbers.\n";

    std::vector<BenchResult> all_results;
    print_header();

    for (size_t N : sizes) {
        // Print size separator.
        std::cout << "── N = " << N << " ──\n";

        auto r1 = bench_add_only(N, SEED);      print_row(r1); all_results.push_back(r1);
        auto r2 = bench_cross_spread(N, SEED);  print_row(r2); all_results.push_back(r2);
        auto r3 = bench_cancel(N, SEED);        print_row(r3); all_results.push_back(r3);
        auto r4 = bench_mixed(N, SEED);         print_row(r4); all_results.push_back(r4);
    }

    std::cout << "\n[sink=" << g_sink << "]\n";  // prevents dead-code elimination
    write_csv(all_results, "benchmark_results.csv");
    return 0;
}
