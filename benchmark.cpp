#include <benchmark/benchmark.h>
#include "BaselineOrderBook.hpp"
#include "LimitOrderBook.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>

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

static inline uint64_t rdtsc() {
    unsigned lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t cyclePercentile(std::vector<uint64_t>& samples, double p) {
    std::sort(samples.begin(), samples.end());
    size_t idx = std::min(samples.size() - 1,
                    (size_t)std::ceil(samples.size() * p) - 1);
    return samples[idx];
}

static double getCyclesPerNanosecond() {
    auto t0_clock = std::chrono::high_resolution_clock::now();
    uint64_t t0_tsc = rdtsc();

    // Sleep for 10ms to let TSC accumulate cycles
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto t1_clock = std::chrono::high_resolution_clock::now();
    uint64_t t1_tsc = rdtsc();

    double elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1_clock - t0_clock).count();
    uint64_t elapsed_cycles = t1_tsc - t0_tsc;

    return (double)elapsed_cycles / elapsed_ns;
}

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
    auto engine = std::make_unique<LimitOrderBook>();
    uint32_t startingPrice = 100000;
    uint64_t orderId = 1;

    // Pre-fill the engine with distinct price levels based on benchmark range
    for (int i = 0; i < state.range(0); i++) {
        engine->addOrder({orderId++, startingPrice - i, 100, Side::BUY});
    }

    uint32_t targetPrice = startingPrice - (state.range(0) / 2);
    Order deepOrder = {200000, targetPrice, 100, Side::BUY};

    std::vector<uint64_t> samples;
    samples.reserve(1000000); 
    
    for (auto _ : state) {
        uint64_t t0 = rdtsc();
        engine->addOrder(deepOrder);
        engine->cancelOrder(200000);
        uint64_t t1 = rdtsc();
        benchmark::ClobberMemory();
        samples.push_back(t1 - t0);
    }

    std::sort(samples.begin(), samples.end());
    
    auto get_percentile = [&](double p) -> double {
        if (samples.empty()) return 0.0;
        size_t idx = std::min(samples.size() - 1, (size_t)std::ceil(samples.size() * p) - 1);
        return (double)samples[idx];
    };

    static const double cycles_per_ns = getCyclesPerNanosecond();

    state.counters["p50_ns"] = get_percentile(0.50) / cycles_per_ns;
    state.counters["p90_ns"] = get_percentile(0.90) / cycles_per_ns;
    state.counters["p99_ns"] = get_percentile(0.99) / cycles_per_ns;
}
BENCHMARK(BM_EngineScaling)->RangeMultiplier(10)->Range(100, 100000)->Iterations(1000000)->Repetitions(20)->ReportAggregatesOnly(true);
static void BM_PingPong(benchmark::State& state) {
    auto engine = std::make_unique<LimitOrderBook>();

    for (auto _ : state) {
        engine->addOrder({1, 15000, 100, Side::BUY}); 
        engine->addOrder({2, 15000, 100, Side::SELL});
    }
}
BENCHMARK(BM_PingPong)->Repetitions(20)->ComputeStatistics("p50", p50)->ComputeStatistics("p90", p90)->ComputeStatistics("p99", p99)->ReportAggregatesOnly(true);

static void BM_LevelSweep(benchmark::State& state) {
    auto engine = std::make_unique<LimitOrderBook>();
    Order massiveSell = {999, 15000, 1000, Side::SELL};
    
    for (auto _ : state) {
        // Pause timer: Set up the 100 resting orders
        state.PauseTiming();
        for (int i = 1; i <= 100; ++i) {
            engine->addOrder({(uint64_t)i, 15000, 10, Side::BUY}); 
        }
        state.ResumeTiming();

        // Execute the sweep
        engine->addOrder(massiveSell);
    }
}
BENCHMARK(BM_LevelSweep)->Repetitions(20)->ComputeStatistics("p50", p50)->ComputeStatistics("p90", p90)->ComputeStatistics("p99", p99)->ReportAggregatesOnly(true);

int main(int argc, char** argv) {
    double cycles_per_ns = getCyclesPerNanosecond(); 

    std::cout << "[SYSTEM] Calibrated TSC frequency: " 
              << (cycles_per_ns * 1000) << " MHz\n";
    std::cout << "[SYSTEM] 1 nanosecond = " 
              << cycles_per_ns << " clock cycles\n";
    std::cout << "[SYSTEM] To convert benchmark cycles to time, divide cycles by " 
              << cycles_per_ns << "\n\n";

    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    return 0;
}