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
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

GoogleTest is fetched automatically via `FetchContent` — no manual install needed.

## Test

```bash
ctest --test-dir build --output-on-failure
# or run directly:
./build/tests/lob_tests
```

All 28 tests pass, including a seeded 10k-operation fuzz test that verifies per-level
`total_quantity` invariants after every operation.

## Example

```bash
./build/simple_session
```

Submits ~10 orders, triggers several matches, and prints a formatted book ladder plus trade log.

---

## Roadmap

**Phase 2 — Memory pool + benchmarking**
- Replace `new Order` with a slab/pool allocator; zero heap allocation in the matching loop.
- Google Benchmark suite: throughput (orders/sec), latency percentiles (p50/p99/p999).
- Benchmark `std::map` vs flat level array; replace if warranted.
- Template `MatchingEngine` on a handler concept (CRTP) to eliminate `std::function` overhead.

**Phase 3 — NASDAQ ITCH 5.0 replay**
- Parse ITCH binary feed from historical data files.
- Replay into the engine and emit statistics: fill rates, queue position at fill, queue depletion.

**Phase 4 — Streaming microstructure metrics → dashboard**
- Compute order flow imbalance (OFI), arrival rate λ, queue resilience.
- Stream metrics over WebSocket to a React dashboard with live book visualization.
