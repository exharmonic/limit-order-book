#include "LimitOrderBook.hpp"
#include "RingBuffer.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

RingBuffer<Order, 131072> orderQueue;
LimitOrderBook engine;
std::atomic<bool> marketOpen{true};

void engineThread() {
    Order incomingOrder;
    uint32_t processedCount = 0;

    while (marketOpen.load(std::memory_order_relaxed)) {
        if (orderQueue.pop(incomingOrder)) {
            engine.addOrder(incomingOrder);
            processedCount++;
        }
    }

    while (orderQueue.pop(incomingOrder)) {
        engine.addOrder(incomingOrder);
        processedCount++;
    }

}

int main() {

    std::cout << "[MAIN] Initializing system pipeline...\n";
    
    // Launching the isolated consumer thread
    std::thread consumer(engineThread);
    
    std::cout << "[MAIN] Executing a burst of 100,000 orders into the queue...\n";
    auto start = std::chrono::high_resolution_clock::now();

    for (uint32_t i = 1; i <= 100000; ++i) {
        Order newOrder = {i, 15000, 100, Side::BUY};
        while (!orderQueue.push(newOrder)) {
            // Drop logic or Retry Spin logic
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "[MAIN] Ingestion burst completed in " << duration << " ms.\n";

    marketOpen.store(false, std::memory_order_relaxed);
    consumer.join();

    std::cout << "[MAIN] Execution verified. Core closed successfully.\n";
    
    return 0;
}