#include <benchmark/benchmark.h>
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

static void BM_PingPong(benchmark::State& state) {
    LimitOrderBook engine;

    for (auto _ : state) {
        engine.addOrder({1, 15000, 100, Side::BUY}); 
        engine.addOrder({2, 15000, 100, Side::SELL});
    }
}
BENCHMARK(BM_PingPong)->Repetitions(20)->ComputeStatistics("p50", p50)->ComputeStatistics("p90", p90)->ComputeStatistics("p99", p99)->ReportAggregatesOnly(true);

class DeepBookFixture : public benchmark::Fixture {
public:
    LimitOrderBook engine;
    void SetUp(const benchmark::State& state) override {
        // Pre-fill the book with 10,000 resting buy orders before the timer starts
        for (int i = 1; i <= 10000; ++i) {
            engine.addOrder({(uint64_t)i, 14000, 100, Side::BUY});
        }
    }
};

BENCHMARK_F(DeepBookFixture, BM_DeepInsert)(benchmark::State& state) {
    Order deepOrder = {99999, 13000, 100, Side::BUY};
    for (auto _ : state) {
        // Add an order far from the spread, then immediately cancel it to preserve memory
        engine.addOrder(deepOrder);
        engine.cancelOrder(99999);
    }
}
BENCHMARK_REGISTER_F(DeepBookFixture, BM_DeepInsert)->Repetitions(20)->ComputeStatistics("p50", p50)->ComputeStatistics("p90", p90)->ComputeStatistics("p99", p99)->ReportAggregatesOnly(true);

static void BM_LevelSweep(benchmark::State& state) {
    LimitOrderBook engine;
    Order massiveSell = {999, 15000, 1000, Side::SELL};
    
    for (auto _ : state) {
        // Pause timer: Set up the 100 resting orders
        state.PauseTiming();
        for (int i = 1; i <= 100; ++i) {
            engine.addOrder({(uint64_t)i, 15000, 10, Side::BUY}); // Total resting volume: 1000
        }
        state.ResumeTiming();

        // Execute the sweep
        engine.addOrder(massiveSell);
    }
}
BENCHMARK(BM_LevelSweep)->Repetitions(20)->ComputeStatistics("p50", p50)->ComputeStatistics("p90", p90)->ComputeStatistics("p99", p99)->ReportAggregatesOnly(true);

BENCHMARK_MAIN();