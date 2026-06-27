#include <benchmark/benchmark.h>
#include "BaselineOrderBook.hpp"
#include "LimitOrderBook.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

auto p50 = [](const std::vector<double>& v) -> double {
    std::vector<double> copy = v;
    std::sort(copy.begin(), copy.end());
    size_t index = static_cast<size_t>(std::ceil(copy.size() * 0.50)) - 1;
    return copy[index];
};

auto p90 = [](const std::vector<double>& v) -> double {
    std::vector<double> copy = v;
    std::sort(copy.begin(), copy.end());
    size_t index = static_cast<size_t>(std::ceil(copy.size() * 0.90)) - 1;
    return copy[index];
};

auto p99 = [](const std::vector<double>& v) -> double {
    std::vector<double> copy = v;
    std::sort(copy.begin(), copy.end());
    size_t index = static_cast<size_t>(std::ceil(copy.size() * 0.99)) - 1;
    return copy[index];
};

static void BM_BaselineScaling(benchmark::State& state) {
    BaselineOrderBook book;
    uint32_t startingPrice = 99999;; 
    uint64_t orderId = 1;
    
    // Pre-fill the book with distinct price levels based on benchmark range
    for (int i = 0; i < state.range(0); i++) {
        book.addBid(startingPrice - i, orderId++, 100);
    }

    uint32_t targetPrice = startingPrice - (state.range(0) / 2);

    for (auto _ : state) {
        book.addBid(targetPrice, orderId++, 100);
    }
}
BENCHMARK(BM_BaselineScaling)->RangeMultiplier(10)->Range(100, 100000)->Repetitions(20)->ComputeStatistics("p50", p50)->ComputeStatistics("p90", p90)->ComputeStatistics("p99", p99)->ReportAggregatesOnly(true);

static void BM_EngineScaling(benchmark::State& state) {
    LimitOrderBook engine;
    uint32_t startingPrice = 99999;;
    uint64_t orderId = 1;

    // Pre-fill the engine with distinct price levels based on benchmark range
    for (int i = 0; i < state.range(0); i++) {
        engine.addOrder({orderId++, startingPrice - i, 100, Side::BUY});
    }

    uint32_t targetPrice = startingPrice - (state.range(0) / 2);
    Order deepOrder = {200000, targetPrice, 100, Side::BUY};

    for (auto _ : state) {
        engine.addOrder(deepOrder);
        engine.cancelOrder(200000); 
    }
}
BENCHMARK(BM_EngineScaling)->RangeMultiplier(10)->Range(100, 100000)->Repetitions(20)->ComputeStatistics("p50", p50)->ComputeStatistics("p90", p90)->ComputeStatistics("p99", p99)->ReportAggregatesOnly(true);

static void BM_PingPong(benchmark::State& state) {
    LimitOrderBook engine;

    for (auto _ : state) {
        engine.addOrder({1, 15000, 100, Side::BUY}); 
        engine.addOrder({2, 15000, 100, Side::SELL});
    }
}
BENCHMARK(BM_PingPong)->Repetitions(20)->ComputeStatistics("p50", p50)->ComputeStatistics("p90", p90)->ComputeStatistics("p99", p99)->ReportAggregatesOnly(true);

static void BM_LevelSweep(benchmark::State& state) {
    LimitOrderBook engine;
    Order massiveSell = {999, 15000, 1000, Side::SELL};
    
    for (auto _ : state) {
        // Pause timer: Set up the 100 resting orders
        state.PauseTiming();
        for (int i = 1; i <= 100; ++i) {
            engine.addOrder({(uint64_t)i, 15000, 10, Side::BUY}); 
        }
        state.ResumeTiming();

        // Execute the sweep
        engine.addOrder(massiveSell);
    }
}
BENCHMARK(BM_LevelSweep)->Repetitions(20)->ComputeStatistics("p50", p50)->ComputeStatistics("p90", p90)->ComputeStatistics("p99", p99)->ReportAggregatesOnly(true);

BENCHMARK_MAIN();