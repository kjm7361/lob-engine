# lob-engine

A C++20 limit order book and matching engine with price-time (FIFO) priority. Built for
correctness and clean architecture in Phase 1, with interfaces designed to support low-latency
optimizations in subsequent phases without breaking changes.

This is the kind of data structure that sits at the heart of every equities, futures, and
crypto exchange. Understanding it is foundational to microstructure research, HFT system design,
and building order routing infrastructure.

---

## Design decisions

### Integer ticks, no floating-point

`Price` is `int64_t` (ticks). There is no `double` anywhere on the matching path. Floating-point
arithmetic introduces non-determinism, rounding surprises, and slower operations on the hot path.
Integer tick math is exact, branch-predictor-friendly, and directly maps to how real exchanges
represent prices in their wire protocols (e.g. NASDAQ ITCH uses 4-decimal fixed-point integers).

### Intrusive doubly-linked lists

Each `Order` carries `prev`/`next` pointers directly (see `order.hpp`). This means:
- **No extra allocation per order** beyond the `Order` object itself.
- **O(1) removal given an `Order*`** — no linear search, no iterator invalidation. The matching
  engine calls `PriceLevel::remove(o)` directly after a full fill.
- **One fewer pointer indirection** compared to `std::list<Order>` where following a node chases
  an allocator node before reaching the order.

The tradeoff: `Order` objects cannot be freely moved in memory while on the book.

### `std::map<Price, PriceLevel>` for Phase 1

O(log N) add/remove/lookup where N is the number of distinct price levels. In practice N is
small (hundreds, not millions), so the constant factor matters more than the asymptotic. Phase 2
will benchmark this against a flat circular array of levels indexed by tick offset from a
reference price — O(1) with much better cache behaviour for tightly-clustered prices. The
`OrderBook` interface is identical either way; the swap is surgical.

### Operation complexity

| Operation | Complexity | Notes |
|---|---|---|
| `add_order` | O(log L) | L = live price levels |
| `cancel_order` | O(log L) | O(1) index lookup + O(log L) level erase |
| `modify_order` (qty decrease) | O(log L) | In-place, no list reorder |
| `modify_order` (qty increase / price change) | O(log L) | Cancel + reinsert |
| `best_bid` / `best_ask` | O(1) | `std::map::rbegin` / `begin` |
| `depth_snapshot(n)` | O(n) | Walk at most n levels |
| Match one level | O(F) | F = fills on that level |

### No exceptions on the hot path

Rejections fire via callback (`OrderRejected` event) rather than `throw`. This keeps the
matching loop exception-free and compatible with `noexcept` assertions in Phase 2.

---

## Build

**Prerequisites:** CMake ≥ 3.20, a C++20-capable compiler (Clang 13+, GCC 11+).

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release --parallel
```

GoogleTest is fetched automatically via `FetchContent` — no manual install needed.

## Test

```bash
ctest --test-dir build_release --output-on-failure
# or run directly:
./build_release/tests/lob_tests
```

All 28 tests pass, including a seeded 10k-operation fuzz test that verifies per-level
`total_quantity` invariants after every operation.

## Example

```bash
./build_release/simple_session
```

Submits ~10 orders, triggers several matches, and prints a formatted book ladder plus trade log.

---

## Verification

After a clean build, all three project executables should be present and tests should pass:

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
find build_release -type f -perm -111 | grep -v '_deps\|CMakeFiles'
ctest --test-dir build_release --output-on-failure
```

Expected executables:
- `build_release/simple_session`
- `build_release/tests/lob_tests`
- `build_release/benchmarks/lob_bench`

---

## Performance Benchmarking

Benchmarks use `std::chrono` only — no external framework required.

**Build and run (always use Release mode for valid numbers):**

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release --parallel
./build_release/benchmarks/lob_bench
```

Skip the 1M run (for quick CI checks):
```bash
./build_release/benchmarks/lob_bench --fast
```

**Scenarios:**

| Scenario | What it measures |
|---|---|
| `AddOnly` | Pure limit order insertion — alternating buys/sells at non-crossing prices |
| `CrossSpread` | Each op submits one crossing buy that matches a pre-resting sell — tests full match path |
| `CancelOnly` | Cancels N pre-submitted resting orders — tests `unordered_map` lookup + O(1) list removal |
| `Mixed` | 70% limit adds, 20% market/crossing, 10% cancels — realistic blended workload |

**Metrics reported:** total ops, runtime (ms), orders/sec, avg latency (ns), p50/p95/p99 (ns), trades generated, price levels remaining.

**Sample output (Apple M-series, Release mode):**

```
Scenario          Ops   Runtime(ms)   Orders/sec   Avg(ns)  p50(ns)  p95(ns)  p99(ns)  Trades  Levels
────────────────────────────────────────────────────────────────────────────────────────────────────────
── N = 1000000 ──
AddOnly       1000000        38.63     25887452      38.6       41       42       42        0     100
CrossSpread   1000000        24.27     41206970      24.3       41       42       42  1000000       0
CancelOnly    1000000        37.49     26672725      37.5       42      125      125        0       0
Mixed         1000000        51.53     19405938      51.5       42       84      167   199470      78
```

Results are also written to `benchmark_results.csv` in the working directory.

**Notes:**
- Numbers depend on your hardware, OS scheduler, and CPU state. Run several times and compare.
- Debug builds will show 5–10× higher latencies. Always benchmark in Release.
- The `std::function` event callbacks and `std::map` level lookup are the current hot-path costs — Phase 2 targets both.

---

## Backtesting Module

A minimal backtesting module for educational market-microstructure simulation and strategy
execution prototyping. It does **not** claim to be a production-grade backtesting framework,
provide live-trading connectivity, or guarantee profitable strategy outcomes.

### Two replay modes

**1. Order-book event replay** (`market_data_replay.hpp`)

Loads a CSV of raw order events and replays them in timestamp order into a fresh
`MatchingEngine`. Each row is one engine operation — a resting limit add, an
immediate market order, or a cancel. Useful for deterministic LOB reconstruction
from a historical or synthetic event stream.

CSV format (`examples/data/sample_events.csv`):
```
timestamp,event_type,side,price,quantity
1,add,sell,10005,50
5,market,buy,0,25
```
| field        | values                 | notes                              |
|---|---|---|
| `event_type` | `add` `market` `cancel`| `cancel` uses `price` as order_id  |
| `side`       | `buy` `sell`           | ignored for `cancel`               |
| `price`      | integer ticks          | 0 for market orders                |

**2. Strategy backtest** (`runner.hpp`, `strategy.hpp`)

Replays a sequence of bid/ask market ticks through a `Strategy` subclass.
Per tick the runner:
1. Places a synthetic resting SELL at the event's ask, filling any resting strategy
   buy limits that have now crossed the market.
2. Places a synthetic resting BUY at the event's bid, filling any resting strategy
   sell limits.
3. Calls `strategy.on_event()` — the strategy may submit new aggressive or passive orders.
4. Cancels the synthetic orders.
5. Marks the `Portfolio` at the current mid price.

### Strategy interface

Subclass `Strategy`, implement `on_event(const MarketEvent&)` and `on_fill(const Fill&)`,
and call `submit()` to place orders. A ready-made `MeanReversionStrategy` is provided in
`include/lob/backtest/mean_reversion_strategy.hpp` as an educational prototype.

### Portfolio and PnL tracking

`Portfolio` tracks cash, net position (signed units), realized PnL (FIFO), and
mark-to-market equity via an equity curve snapshot per tick. `PerformanceReport` computes:
- Total return %
- Sharpe ratio (annualised, assuming 1 event = 1 minute)
- Max drawdown %
- Trade count and average trade PnL

Only metrics that are actually calculated are reported.

### Build and run

```bash
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release
ctest --test-dir build_release --output-on-failure
```

Run the simple backtest example (loads `examples/data/sample_events.csv` from the repo root):
```bash
./build_release/simple_backtest
# or with a custom CSV:
./build_release/simple_backtest path/to/events.csv
```

Run the original strategy demo (500-tick synthetic feed, equity curve printout):
```bash
./build_release/backtest_demo
```

### Module layout

```
include/lob/backtest/
  market_data_event.hpp       — MarketDataEvent struct + EventType enum (add/market/cancel)
  market_data_replay.hpp      — load_market_data_csv + replay() + ReplayResult
  market_event.hpp            — MarketEvent struct (bid/ask tick) + synthetic_feed
  strategy.hpp                — Strategy ABC + Fill struct + SubmitFn
  mean_reversion_strategy.hpp — header-only educational MeanReversionStrategy
  portfolio.hpp               — Portfolio (cash, position, PnL, equity curve)
  runner.hpp                  — BacktestRunner (tick-by-tick strategy execution)
  performance.hpp             — PerformanceReport + compute_report

src/backtest/
  market_data_replay.cpp
  market_event.cpp
  portfolio.cpp
  performance.cpp
  runner.cpp

examples/
  simple_backtest.cpp         — Part 1: CSV replay  |  Part 2: strategy backtest
  backtest_demo.cpp           — strategy demo with equity curve output
  data/sample_events.csv      — 9-event sample order stream
```

---

## Roadmap

**Phase 2 — Memory pool + deeper profiling**
- Replace `new Order` with a slab/pool allocator; zero heap allocation in the matching loop.
- Baseline `std::chrono` benchmarks are in place (see above). Add Google Benchmark for more rigorous statistics and regression tracking.
- Benchmark `std::map` vs flat circular level array; replace if warranted.
- Template `MatchingEngine` on a handler concept (CRTP) to eliminate `std::function` overhead.

**Phase 3 — NASDAQ ITCH 5.0 replay**
- Parse ITCH binary feed from historical data files.
- Replay into the engine and emit statistics: fill rates, queue position at fill, queue depletion.

**Phase 4 — Streaming microstructure metrics → dashboard**
- Compute order flow imbalance (OFI), arrival rate λ, queue resilience.
- Stream metrics over WebSocket to a React dashboard with live book visualization.
