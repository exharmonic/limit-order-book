#include "LimitOrderBook.hpp"
#include <iostream>
#include <chrono>

int main() {
    LimitOrderBook engine;
    
    // Silence std::cout during the seeding phase so I/O doesn't bottleneck the setup
    std::cout.setstate(std::ios_base::failbit); 

    for (uint32_t i = 1; i <= 5000; ++i) {
        engine.addOrder({i, 15000, 100, Side::BUY});
        engine.addOrder({i + 5000, 15100, 100, Side::SELL});
    }

    // Turn std::cout back on for the benchmark
    std::cout.clear();

    std::cout << "Engine Seeded with 10,000 orders.\n";
    std::cout << "Firing aggressive market taker...\n\n";

    Order aggressiveOrder = {10001, 15100, 250, Side::BUY}; 

    auto start = std::chrono::high_resolution_clock::now();

    engine.addOrder(aggressiveOrder);

    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "TRADE EXECUTION TIME: " << duration << " nanoseconds.\n";

    return 0;
}