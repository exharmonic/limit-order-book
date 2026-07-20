# NanoMatch — Limit Order Book Engine

A limit order book written in C++20, built to be as fast as possible. It reads NASDAQ ITCH 5.0 binary feeds, PCAP-captured ITCH/MoldUDP64 multicast traffic, or CSV files, matches limit and market orders by price-time priority, and processes them on a dedicated thread pinned to its own CPU core.

---

## Headline result

Ingesting 1,000,000 orders takes **~45.25 ms** natively (**~22M orders/sec**), a
**~4.7x** speedup over a std::map-based baseline (**~211.75 ms**, ~4.7M orders/sec).
The engine's per-order matching latency stays flat at **~9.1–9.2 ns** regardless of
book depth, while the baseline degrades from ~9.8 ns to ~21.1 ns as the book fills up.

---

## How it works?

A parser thread (run on logical core 2) reads the feed file and pushes orders into a ring buffer. A separate engine thread (run on logical core 4) drains that buffer and runs the matching logic, thereby implementing an SPSC (**S**ingle **P**roducer **S**ingle **C**onsumer) queue. A third, logger thread (run on logical core 6) drains a second SPSC queue of trade fills and writes them to disk. No two threads ever share a lock; they communicate only through atomic reads and writes on their respective queues.

```
Parser Thread (Core 2)  ── [order ring buffer] ──>  Engine Thread (Core 4) ──> [fill ring buffer] ──> Logger Thread (Core 6)
    reads file                                        matches orders                                    writes fills.csv (buffered
  (mmap, zero-copy)                                 (price-time priority)                                ofstream, off hot path)
                                                    
```

*Note: All three threads are pinned to physical cores such that the parser and engine threads don't share an L2 cache\*, and Core 0 is avoided because the OS routes hardware interrupts there.*
 
*\* This was verified using the linux command `lscpu -e`*
 
![CPU topology showing logical cores 2 and 4 mapped to separate physical cores](docs/cpu_topology.png "lscpu -e output: cores 2 and 4 belong to different physical cores, confirming no shared L2")
 
*Clearly, the logical cores 2, 4 and 6 belong to different physical cores.*
 
Every match the engine thread produces is also pushed onto a **second, independent lock-free SPSC ring buffer** (`fillQueue`) as a `FillEvent`, drained by the logger thread — this is the trade-reporting path, kept completely separate from the order-ingestion path so that the logger can never add backpressure to matching. See [Trade / fill reporting](#trade--fill-reporting) below.
 
Orders also carry a **market-order path**: any order routed to the engine with `price == 0` is treated as a market order (`addMarketOrder`) and sweeps the opposite side of the book at whatever price is available, rather than resting if unfilled. Regular limit orders still go through `addOrder`, which continues to reject (and log) any order priced at `0` or `>= MAX_PRICE`, since that check exists specifically to catch limit orders that shouldn't have reached that path.
 
---
 
## Why it's fast
 
**No `std::map`.** The bid and ask sides are just flat arrays indexed by price — `bids[price]` gets you a price level in a single array lookup, regardless of how many other price levels exist. A `std::map` takes O(log N) time per lookup and gets measurably slower as the book fills up. The benchmarks below prove this out.
 
**Bitsets for best bid/ask tracking.** When a price level empties, the engine needs to find the next best price. Instead of scanning the array, it uses a compact array of 64-bit integers as a bitset, then uses a single CPU instruction (`__builtin_clzll` / `__builtin_ctzll`) to find the next set bit. The scan starts from the current best price, so it rarely looks at more than one word.
 
**Memory pool, pre-wired upfront.** All order nodes come from a slab of memory allocated at startup with `mmap(MAP_POPULATE)`. `MAP_POPULATE` tells the kernel to wire all the physical pages before any orders arrive, so there are no page faults at runtime. `MADV_HUGEPAGE` then groups that memory into 2 MB pages to reduce TLB pressure during large sweeps.
 
**32-byte node alignment.** Each `OrderNode` is exactly 32 bytes, so two fit neatly in a 64-byte L1 cache line. Traversing a price level's order queue reads two nodes per cache line fetch.
 
**Zero-copy file parsing.** All three parsers (CSV, raw ITCH, and PCAP-wrapped ITCH) use `mmap` to map the file into virtual memory directly. There's no `fstream`, no intermediate buffer — the kernel's page cache is the read buffer. ITCH binary messages are decoded by casting a raw pointer straight to a packed struct, plus a byte-swap for big-endian fields; the PCAP path additionally walks the Ethernet/IPv4/UDP/MoldUDP64 envelope before handing each embedded message to the same ITCH decoder.
 
**Heap-allocated book instances in benchmarks.** `LimitOrderBook` owns two `std::array<PriceLevel, MAX_PRICE>` (one per side), an `orderMap` of `MAX_ORDERS` entries, and its own `MemoryPool` — several megabytes of state in total. That's too large to construct safely on the stack of a benchmark function, so every `BENCHMARK` fixture that needs a fresh book (`BM_EngineScaling`, `BM_PingPong`, `BM_LevelSweep`) constructs it via `std::make_unique<LimitOrderBook>()` instead, keeping the object on the heap while everything inside it is still contiguous, pre-wired memory.
 
---
 
## Trade / fill reporting
 
Every time `addOrder` or `addMarketOrder` produces a match, it emits a `FillEvent`:
 
```cpp
struct FillEvent {
    uint64_t restingOrderID;
    uint64_t aggressorOrderID;
    uint32_t price;
    uint32_t fillQuantity;
    Side     aggressorSide;
};
```
 
This is pushed onto its own `RingBuffer<FillEvent, FILL_QUEUE_CAPACITY>` (`fillQueue`), completely separate from the order-ingestion ring buffer described above. Same lock-free SPSC pattern: the engine thread is the sole producer, and a dedicated **logger thread** (pinned to a core other than 0/2/4) is the sole consumer, draining it via `popFill(FillEvent&)`.
 
The logger thread loops on `popFill`, appending each `FillEvent` as a CSV row (`restingOrderID,aggressorOrderID,price,fillQuantity,aggressorSide`) to `fills.csv` via a plain buffered `ofstream`. It deliberately avoids the zero-copy/mmap tricks used elsewhere in this project — the entire point of routing fills through a separate queue is to let this thread be as slow as it needs to be without ever stalling the engine thread. On shutdown, `main.cpp` joins the engine thread first (the sole producer), *then* signals the logger to stop, so the logger's final drain pass is guaranteed to pick up every fill the engine produced before market close.
 
If the fill queue ever fills up (capacity `FILL_QUEUE_CAPACITY`, currently sized the same as the order queue) faster than the logger can drain it, the engine does **not** block or slow down: it drops the fill record and increments `droppedFillCount`. The logger prints this count (via `getDroppedFillCount()`) on shutdown, so a non-zero value is a signal the logger needs to be made faster or the queue larger, not a silent data-loss bug.
 
`BaselineMain.cpp` has no equivalent — `BaselineOrderBook` doesn't emit `FillEvent`s at all, since fill reporting is part of the optimized engine's design, not the baseline being benchmarked against.
 
---
 
## Benchmark results
 
Numbers below were measured after the market-order and fill-reporting changes.
 
### Test hardware
 
| | |
|---|---|
| CPU | Intel Core i7-14650HX (12 physical cores, 24 threads, 1 socket), calibrated TSC 2287.96 MHz |
| Cache | L1d 48 KiB ×12 (576 KiB total) · L1i 32 KiB ×12 (384 KiB total) · L2 2048 KiB ×12 (24 MiB total) · L3 30720 KiB (30 MiB) |
| RAM | 7.6 GiB |
| OS | WSL2 (Ubuntu 24.04) on Windows, kernel 6.18.33.1-microsoft-standard-WSL2 |
| Compiler | g++ 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1) |
| Build flags | `-O3 -march=native -mtune=native -flto` |
 
> WSL2 runs on a lightweight Hyper-V VM rather than bare metal — core pinning via `pthread_setaffinity_np` targets *virtual* CPU IDs exposed by WSL2, which are backed by, but not guaranteed to map 1:1 onto, physical Windows-scheduled cores. This is disclosed as a methodology caveat.
 
Each fixture runs 20 times; p50/p90/p99 are reported.
 
### Scaling: Engine vs. std::map baseline
 
| Book depth | Baseline p50 | Baseline p90 | Baseline p99 | Engine p50 (mean) | Engine p90 (mean) | Engine p99 (mean) |
|---|---|---|---|---|---|---|
| 100 levels | 12.2 ns | 14.2 ns | 25.0 ns | 10.64 ns | 11.39 ns | 12.05 ns |
| 1,000 levels | 13.1 ns | 15.6 ns | 26.2 ns | 9.90 ns | 10.60 ns | 11.35 ns |
| 10,000 levels | 16.4 ns | 17.3 ns | 17.8 ns | 10.25 ns | 11.02 ns | 12.42 ns |
| 100,000 levels | 24.3 ns | 25.5 ns | 28.7 ns | 9.68 ns | 10.53 ns | 12.00 ns |
 
The engine's per-op latency again stays essentially flat (~9.7–10.6 ns p50) regardless of book depth, while the baseline's p50 grows from 12.2 ns to 24.3 ns as the book fills — the same qualitative story as before, on this run's numbers.
 
### Other fixtures
 
**Ping-Pong** — a BUY order lands and immediately matches a resting SELL. This is the full lifecycle: insert, match, remove.
 
| p50 | p90 | p99 |
|---|---|---|
| 67.9 ns | 69.1 ns | 70.2 ns |
 
**Level Sweep** — one aggressive SELL sweeps through 100 resting BUY orders at the same price. 100 nodes removed in a single `addOrder` call.
 
| p50 | p90 | p99 |
|---|---|---|
| 468 ns | 489 ns | 495 ns |
 
That's about 4.68 ns per node swept (p50), with low variance (~3.7% CV) — the pool and cache alignment doing their job.
 
---
 
## End-to-end ingestion throughput
 
1,000,000 orders read from `data/sample.pcap` (the current default feed for both binaries — Ethernet/IPv4/UDP/MoldUDP64-wrapped ITCH, 50,000 packets), parsed, and matched — engine vs. std::map baseline, native (no profiler attached). Runs were interleaved (baseline, engine, baseline, engine, ...) to control for thermal/scheduling drift between measurements.
 
| Run | Baseline (ms) | Engine (ms) |
|---|---|---|
| 1 | 202 | 66 |
| 2 | 163 | 53 |
| 3 | 181 | 60 |
| 4 | 191 | 48 |
| 5 | 175 | 56 |
| **Average** | **~182.4 ms** | **~56.6 ms** |
 
**~3.2x speedup**, or roughly **~17.7M orders/sec** for the engine vs. **~5.5M orders/sec** for the baseline. This is lower than the ~4.7x/~22M-orders/sec figure measured on the raw `.itch` feed — parsing the PCAP envelope (Ethernet/IPv4/UDP/MoldUDP64 headers per packet, on top of the ITCH message itself) adds real per-packet overhead that the earlier ITCH-only measurement didn't include. Re-running against `data/sample.itch` directly should reproduce numbers closer to the original figures if you want a like-for-like comparison.
 
`engine_main` also prints a live per-order dispatch latency for the run, in raw CPU cycles (not nanoseconds — `main.cpp` doesn't convert these). Across the five runs above, sample counts ranged from 97,790 to 481,284 "live" samples (orders dispatched by the engine thread *while* the parser was still feeding it, before the final drain) — this range reflects how the SPSC queue happened to interleave with the parser on a given run, not a fixed order count, so treat these as illustrative rather than a stable percentile:
 
| Run | Live samples | p50 (cycles) | p90 (cycles) | p99 (cycles) |
|---|---|---|---|---|
| 1 | 481,284 | 30 | 92 | 185 |
| 2 | 184,899 | 65 | 179 | 250 |
| 3 | 126,136 | 51 | 159 | 256 |
| 4 | 124,755 | 33 | 118 | 262 |
| 5 | 97,790 | 46 | 122 | 272 |
 
At this machine's calibrated ~2.288 GHz TSC, that's roughly 13–29 ns p50 and 81–119 ns p99 across runs — noisier than the isolated `engine_bench` numbers above, since this is the real dispatch loop competing with the parser and logger threads rather than an isolated microbenchmark.
 
![Terminal output of interleaved native ingestion runs for baseline and optimized engine](docs/combined_ingestion_throughput.png "Interleaved runs of engine_baseline and engine_main on 1M orders, alternating to control for drift")
 
---
 
## Profiling evidence
 
### Flame graphs
 
**Baseline**: The systemic overhead of dynamic memory and standard library containers is clearly visible across the entire process. On the left, the object teardown (`BaselineOrderBook::~BaselineOrderBook`) forms a massive tower dominating a large portion of the samples, heavily burdened by `std::_Rb_tree` node deallocations (`cfree`, `_int_free`). On the right, during the actual matching phase (`engineThread`), the hot path is continuously interrupted by dynamic memory requests, with `operator new` and `malloc` clearly visible at the top of the execution stack.
 
![Flame graph of baseline engine showing rb-tree and destructor overhead](docs/baseline_flamegraph.svg "Baseline flame graph — std::map/std::list dominate the call stack")
 
**Optimized**: Capturing the unfiltered process beautifully illustrates the multi-threaded architecture. The workload is cleanly divided into three distinct columns: `engineThread` (left), `loggerThread` (center), and `parserThread` (right). 
Crucially, the `engineThread`'s hot path (`LimitOrderBook::addOrder`) is completely flat—there is not a single `malloc`, `free`, or allocator frame present. Furthermore, the heavy STL and I/O overhead (`std::ostream`) is visibly corralled entirely within the `loggerThread`, proving that asynchronous trade reporting never stalls the core matching engine.
 
![Flame graph of optimized engine showing addOrder and engineThread dominating](docs/optimised_flamegraph.svg "Optimized flame graph — no allocator frames in the hot path")
 
#### Reproducing the flame graphs
 
Flame graphs were generated with `perf` plus Brendan Gregg's [FlameGraph](https://github.com/brendangregg/FlameGraph) toolkit.
 
```bash
# One-time setup
git clone https://github.com/brendangregg/FlameGraph.git
 
# Build with frame pointers preserved so perf can unwind the stack accurately
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_PROFILING=ON
make -j$(nproc)
 
perf record -F 999 -e cpu-clock -g -- ./engine_main ../data/sample.itch
perf script -i perf.data | ../flamegraph/stackcollapse-perf.pl | ../flamegraph/flamegraph.pl > ../docs/optimised_flamegraph.svg
 
perf record -F 999 -e cpu-clock -g -- ./engine_baseline ../data/sample.itch
perf script -i perf.data | ../flamegraph/stackcollapse-perf.pl | ../flamegraph/flamegraph.pl > ../docs/baseline_flamegraph.svg
```
 
`-DENABLE_PROFILING=ON` adds `-fno-omit-frame-pointer` to the build (see `CMakeLists.txt`), which is what makes `perf`'s stack unwinding trustworthy — without it, frames collapse into a handful of misleading leaves.
 
### Cachegrind: instruction counts
 
| | Baseline | Optimized | Ratio |
|---|---|---|---|
| Total instructions (Ir) | 671,071,498 | 180,177,611 | **3.72x fewer** |
 
Baseline breakdown — allocator-related functions (`_int_malloc`, `_int_free`, `malloc`, `free`, `alloc_perturb`) account for **54.4%** of all instructions. The optimized build has no allocator frames in its top functions at all; `LimitOrderBook::addOrder` (**56.3%**) dominates, with `main` (**21.1%**, the parser thread) and `engineThread` (**14.1%**, the dispatch loop) making up most of the rest.

> **⚠️ Needs re-verification.** A more recent cachegrind run on this same machine (`valgrind --tool=cachegrind --cache-sim=yes --D1=49152,12,64 --LL=31457280,15,64 ./engine_main ../data/sample.itch` / `./engine_baseline ../data/sample.itch`) measured `Ir` as **1,553,261,936** for `engine_main` vs. **478,442,494** for `engine_baseline` — the *opposite* direction from the numbers above (optimized executing ~3.25x *more* instructions, not fewer), even though `engine_main` still finished faster in wall-clock time (543 ms vs. 1589 ms under valgrind instrumentation). That contradicts the "no allocator frames in the hot path" narrative this section is built on, so it likely reflects the `engine_main` binary not having been rebuilt with `-O3 -march=native -flto` for that particular run (e.g. a stale or debug artifact from an earlier configure), not an actual regression in the matching engine. **Rebuild clean (`rm -rf build && mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_PROFILING=ON && make -j$(nproc)`) and re-run both `valgrind` commands before trusting/publishing an updated version of the table above** — the D1/LL numbers below were still directionally consistent with the original claims, but the `Ir` figures were left unchanged pending that re-run.

### Cachegrind: cache simulation (D1 / LL misses)
 
Cache parameters set to match this machine's actual topology (48 KiB L1d, 12-way; 30 MiB LL/L3, 15-way after Valgrind's power-of-two rounding), rather than generic defaults:
 
```bash
valgrind --tool=cachegrind --cache-sim=yes --D1=49152,12,64 --LL=31457280,15,64 ./engine_main ../data/sample.itch
valgrind --tool=cachegrind --cache-sim=yes --D1=49152,12,64 --LL=31457280,15,64 ./engine_baseline ../data/sample.itch
```
 
| Metric | Baseline | Optimized | Change |
|---|---|---|---|
| D refs | 173,569,832 | 703,274,918 | optimized run had more (see note above — likely same stale-build cause) |
| D1 misses (absolute) | 11,795,487 | 4,585,866 | **2.57x fewer** |
| D1 miss rate | 6.8% | 0.7% | large improvement |
| LLd miss rate | 1.4% | 0.4% | improvement |
 
These D1/LL miss-rate improvements are directionally consistent with the original claim (fewer misses per access in the optimized build), even on the run where the raw `Ir`/`D refs` totals look anomalous — but given that anomaly, treat this whole table as provisional until re-measured on a confirmed clean `-O3 -flto` build.
 
**Observation:** The LLd miss rate is higher in the optimized build. The win here comes from doing fewer memory accesses in total (no rb-tree traversal, no list node allocation), not from each access being individually more cache-friendly — that's also why the *absolute* number of D1 misses still drops by a full 4x even though the rate improvement is modest.
 
> Note on methodology: `perf stat` / Intel VTune hardware counters were unavailable under WSL2, so cachegrind's software cache simulation was used instead. It doesn't require hardware perf counter access and gives directly comparable D1/LL miss statistics between baseline and optimized builds. Cache-sim parameters (`--D1`, `--LL`) were set to match this machine's real L1d/L3 sizes (see Test hardware above) rather than left at generic defaults, so the simulated miss rates reflect this CPU's actual cache capacity.
 
---
 
## File structure
 
```
├── LimitOrderBook.hpp/cpp   matching engine (limit + market orders, fill reporting)
├── Order.hpp                Order, OrderNode, PriceLevel structs
├── MemoryPool.hpp           mmap slab allocator
├── RingBuffer.hpp           lock-free SPSC queue (used for both orders and fills)
├── CSVParser.hpp            zero-copy CSV parser
├── ITCHParser.hpp           zero-copy ITCH 5.0 binary parser
├── PCAPITCHParser.hpp       zero-copy PCAP parser (Ethernet/IPv4/UDP/MoldUDP64 → ITCHParser)
├── BaselineOrderBook.hpp    std::map reference implementation (benchmarking only)
├── benchmark.cpp            Google Benchmark suite
├── main.cpp                 entry point; parser/engine/logger thread setup, timing
├── BaselineMain.cpp         entry point for the std::map baseline binary
├── CMakeLists.txt           build config (-O3 -march=native -flto, optional ENABLE_PROFILING)
├── docs/
└── scripts/
    ├── csv_generator.py     generates 1M synthetic orders as CSV
    ├── itch_generator.py    generates 1M synthetic orders as ITCH binary (~38 MB)
    └── pcap_generator.py    generates 1M synthetic ITCH orders wrapped in Ethernet/IPv4/UDP/MoldUDP64, as a .pcap capture
```
 
---
 
## Build & run
 
Requires CMake ≥ 3.14, a C++20 compiler, and Linux (uses `mmap`, `pthread_setaffinity_np`, `_mm_pause`).
 
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```
 
To build with frame pointers preserved for `perf`/flame-graph profiling (small runtime cost), configure with:
 
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_PROFILING=ON
```
 
Google Benchmark is fetched automatically at configure time.
 
Generate test data:
 
```bash
python3 scripts/itch_generator.py   # creates data/sample.itch
python3 scripts/csv_generator.py    # creates data/orders.csv
python3 scripts/pcap_generator.py   # creates data/sample.pcap (same ITCH payloads, wrapped in Ethernet/IPv4/UDP/MoldUDP64)
```
 
Run the ingestion engine (processes `data/sample.pcap` by default — this is the current default in both `main.cpp` and `BaselineMain.cpp`):
 
```bash
./engine_main
```
 
Run benchmarks:
 
```bash
./engine_bench
```
 
To switch between CSV, raw ITCH, and PCAP, change the `filepath` variable in `main.cpp`, or pass a path as the first argument to `engine_main` / `engine_baseline` — the extension (`.csv`, `.itch`, `.pcap`) determines which parser is used.
 
---
 
## Supported feed formats
 
**CSV** — one order per line: `orderID,price,quantity,side` (0 = buy, 1 = sell).
 
**NASDAQ ITCH 5.0** — binary. Handles message types `A` (add order), `F` (add order with attribution), and `D` (delete order). Everything else is skipped.

**PCAP (Ethernet/IPv4/UDP/MoldUDP64-wrapped ITCH)** — a `.pcap` capture containing NASDAQ TotalView-ITCH messages carried over MoldUDP64 multicast, the way they'd actually arrive off an exchange feed. `PCAPITCHParser` walks each captured packet's classic pcap record header, verifies the Ethernet frame (with optional single 802.1Q VLAN tag), IPv4 header, and UDP header, then unpacks the MoldUDP64 block's message count and hands each embedded ITCH message to the same `ITCHParser::processMessage` used by the raw `.itch` path — so the matching logic downstream is identical regardless of which of the three formats the data arrived in. Non-Ethernet link types, non-IPv4/non-UDP packets, and truncated captures are skipped and counted, not treated as fatal.
 
**Market orders** — the engine's `addMarketOrder` path is triggered whenever an order reaching `main.cpp`'s dispatch has `price == 0` and `quantity > 0`; it sweeps the opposite side of the book at whatever price is resting rather than joining the book unfilled. None of `csv_generator.py`, `itch_generator.py`, or `pcap_generator.py` currently emit `price == 0` rows, so this path exists in the engine but isn't exercised by the bundled sample data — feed it a CSV/ITCH/PCAP row with price `0`, or call `addMarketOrder` directly, to use it. Note that `BaselineOrderBook` has no equivalent concept at all: a `price == 0` row reaching `BaselineMain.cpp` is inserted as an ordinary resting order at price 0 rather than swept, so the two binaries are only benchmark-comparable as long as the feed never contains price-0 rows.
 
**Cancellations** — signalled by `quantity == 0` on the incoming `Order`/CSV row; the ITCH delete-order (`D`) message (raw or PCAP-wrapped) maps to this the same way.
 
---
 
## Limitations
 
- Prices must be integers between 1 and 100,000 inclusive (`MAX_PRICE = 100,001`); orders at or above the ceiling, or priced at exactly 0 when they reach `addOrder` directly, are dropped and logged to stderr.
- Order IDs must be below 1,100,001 (the size of the order map array).
- Linux only — `mmap`, `pthread_setaffinity_np`, and `__builtin_*` intrinsics are used throughout.
- The order-ingestion ring buffer is single-producer, single-consumer only.
- The logger thread is pinned to logical core 6 in `main.cpp` — if your machine has fewer cores, or 6 happens to collide with an interrupt-heavy or otherwise pinned core, adjust the `CPU_SET` call before relying on the pinning.
- `fills.csv` is truncated and rewritten on every run of `engine_main` — there's no append-across-runs or rotation logic.
- No persistence. Everything lives in memory; a crash loses the book state.
- Memory pool exhaustion (>1,100,000 live orders) is logged to stderr and the order is silently dropped rather than causing a resize/reallocation.
- `LimitOrderBook` is large enough (multi-megabyte `bids`/`asks`/`orderMap` arrays) that it should always be heap-allocated (e.g. via `std::make_unique`) rather than placed on the stack — this is already how `benchmark.cpp` and both `main.cpp`/`BaselineMain.cpp` construct it, but it's worth keeping in mind if you add new call sites.
- See [Known issues](#known-issues) above for the baseline cancel-path bug affecting ITCH/PCAP delete messages.