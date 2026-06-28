# NanoMatch  Limit Order Book Engine

A limit order book written in C++20, built to be as fast as possible. It reads NASDAQ ITCH 5.0 binary feeds or CSV files, matches orders by price-time priority, and processes them on a dedicated thread pinned to its own CPU core.

---

## How it works

A parser thread reads the feed file and pushes orders into a ring buffer. A separate engine thread drains that buffer and runs the matching logic. The two threads never share a lock  they communicate only through atomic reads and writes on the queue.

```
Parser Thread (Core 2)  ── [ring buffer] ──>  Engine Thread (Core 4)
  reads file                                  matches orders
  mmap, zero-copy                             price-time priority
```

Both threads are pinned to physical cores that don't share an L2 cache, and Core 0 is avoided because the OS routes hardware interrupts there.

---

## Why it's fast

**No `std::map`.** The bid and ask sides are just flat arrays indexed by price  `bids[price]` gets you a price level in a single array lookup, regardless of how many other price levels exist. A `std::map` takes O(log N) time per lookup and gets measurably slower as the book fills up. The benchmarks below prove this out.

**Bitsets for best bid/ask tracking.** When a price level empties, the engine needs to find the next best price. Instead of scanning the array, it uses a compact array of 64-bit integers as a bitset, then uses a single CPU instruction (`__builtin_clzll` / `__builtin_ctzll`) to find the next set bit. The scan starts from the current best price, so it rarely looks at more than one word.

**Memory pool, pre-wired upfront.** All order nodes come from a slab of memory allocated at startup with `mmap(MAP_POPULATE)`. `MAP_POPULATE` tells the kernel to wire all the physical pages before any orders arrive, so there are no page faults at runtime. `MADV_HUGEPAGE` then groups that memory into 2 MB pages to reduce TLB pressure during large sweeps.

**32-byte node alignment.** Each `OrderNode` is exactly 32 bytes, so two fit neatly in a 64-byte L1 cache line. Traversing a price level's order queue reads two nodes per cache line fetch.

**Zero-copy file parsing.** Both parsers use `mmap` to map the file into virtual memory directly. There's no `fstream`, no intermediate buffer  the kernel's page cache is the read buffer. ITCH binary messages are decoded by casting a raw pointer straight to a packed struct, plus a byte-swap for big-endian fields.

---

## Benchmark results

Ran on a 24-thread machine (12 physical cores, 30 MB L3). Each fixture runs 20 times; p50/p90/p99 are reported.

### Scaling: Engine vs. std::map baseline

The key result. The engine's latency stays flat at ~9 ns across 100 to 100,000 price levels. The `std::map` baseline climbs from ~10 ns to ~22 ns over the same range.

| Book depth | Baseline p50 | Baseline p99 | Engine p50 | Engine p99 |
|---|---|---|---|---|
| 100 levels | 10.3 ns | 27.8 ns | 9.13 ns | 9.49 ns |
| 1,000 levels | 12.0 ns | 26.4 ns | 9.17 ns | 9.43 ns |
| 10,000 levels | 13.4 ns | 16.6 ns | 9.15 ns | 9.34 ns |
| 100,000 levels | 21.5 ns | 29.4 ns | 9.14 ns | 9.61 ns |

### Other fixtures

**Ping-Pong**  a BUY order lands and immediately matches a resting SELL. This is the full lifecycle: insert, match, remove.

| p50 | p90 | p99 |
|---|---|---|
| 17.0 ns | 17.5 ns | 17.6 ns |

**Level Sweep**  one aggressive SELL sweeps through 100 resting BUY orders at the same price. 100 nodes removed in a single `addOrder` call.

| p50 | p90 | p99 |
|---|---|---|
| 384 ns | 390 ns | 391 ns |

That's about 3.8 ns per node swept, with almost no variance (1.2% CV)  the tight stddev shows the pool and cache alignment doing their job.

---

## File structure

```
├── LimitOrderBook.hpp/cpp   matching engine
├── Order.hpp                Order, OrderNode, PriceLevel structs
├── MemoryPool.hpp           mmap slab allocator
├── RingBuffer.hpp           lock-free SPSC queue
├── CSVParser.hpp            zero-copy CSV parser
├── ITCHParser.hpp           zero-copy ITCH 5.0 binary parser
├── BaselineOrderBook.hpp    std::map reference implementation (benchmarking only)
├── benchmark.cpp            Google Benchmark suite
├── main.cpp                 entry point, thread setup, timing
├── CMakeLists.txt           build config (-O3 -march=native -flto)
└── scripts/
    ├── csv_generator.py     generates 1M synthetic orders as CSV
    └── itch_generator.py    generates 1M synthetic orders as ITCH binary (~38 MB)
```

---

## Build & run

Requires CMake ≥ 3.14, a C++20 compiler, and Linux (uses `mmap`, `pthread_setaffinity_np`, `_mm_pause`).

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Google Benchmark is fetched automatically at configure time.

Generate test data:

```bash
python3 scripts/itch_generator.py   # creates data/sample.itch
python3 scripts/csv_generator.py    # creates data/orders.csv
```

Run the ingestion engine (processes `data/sample.itch` by default):

```bash
./engine_main
```

Run benchmarks:

```bash
./engine_bench
```

To switch between CSV and ITCH, change the `filepath` variable in `main.cpp`.

---

## Supported feed formats

**CSV**  one order per line: `orderID,price,quantity,side` (0 = buy, 1 = sell).

**NASDAQ ITCH 5.0**  binary. Handles message types `A` (add order), `F` (add order with attribution), and `D` (delete order). Everything else is skipped.

---

## Limitations

- Prices must be integers below 100,000. No decimal scaling; orders at or above the ceiling are dropped.
- Order IDs must be below 1,100,001 (the size of the order map array).
- Linux only  `mmap`, `pthread_setaffinity_np`, and `__builtin_*` intrinsics are used throughout.
- The ring buffer is single-producer, single-consumer only.
- No persistence. Everything lives in memory; a crash loses the book state.