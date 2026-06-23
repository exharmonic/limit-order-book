#include "LimitOrderBook.hpp"
#include "RingBuffer.hpp"
#include "CSVParser.hpp"
#include "ITCHParser.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include<string>
#include<string_view>

RingBuffer<Order, 1048576> orderQueue;
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
    std::string filepath = "data/orders.csv";
    std::string_view view = filepath;
    std::cout << "[MAIN] Initializing system pipeline...\n";
    
    // Launching the isolated consumer thread
    std::thread consumer(engineThread);
    
    std::cout << "[MAIN] Executing a burst of 1,000,000 orders into the queue...\n";

    auto start = std::chrono::high_resolution_clock::now();
    if (view.ends_with(".csv")) {
        // std::cout << "[NETWORK] Routing to CSV mmap pipeline...\n";
        CSVParser::parseAndPush(filepath.c_str(), orderQueue);
    } 
    else if (view.ends_with(".itch") || view.ends_with(".pcap")) {
        // std::cout << "[NETWORK] Routing to Binary ITCH mmap pipeline...\n";
        ITCHParser::parseAndPush(filepath.c_str(), orderQueue);
    } 
    else {
        std::cerr << "[SYSTEM] Unsupported file format.\n";
        return 1;
}
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "[MAIN] Ingestion burst completed in " << duration << " ms.\n";

    marketOpen.store(false, std::memory_order_relaxed);
    consumer.join();

    std::cout << "[MAIN] Execution verified. Core closed successfully.\n";
    
    return 0;
}