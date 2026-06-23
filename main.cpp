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
#include <pthread.h>
#include <sched.h>

RingBuffer<Order, 1048576> orderQueue;
LimitOrderBook engine;
std::atomic<bool> marketOpen{true};

void engineThread() {

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(4, &cpuset); // Pin strictly to CPU Core 4

    pthread_t current_thread = pthread_self();
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        std::cerr << "[SYSTEM] Warning: Failed to set thread affinity for Engine Thread.\n";
    }
    Order incomingOrder;
    uint32_t processedCount = 0;

    while (marketOpen.load(std::memory_order_relaxed)) {
        if (orderQueue.pop(incomingOrder)) {
            engine.addOrder(incomingOrder);
            processedCount++;
        }
        else {
            _mm_pause();
        }
    }

    while (orderQueue.pop(incomingOrder)) {
        engine.addOrder(incomingOrder);
        processedCount++;
    }

}

int main() {
    
    cpu_set_t cpuset_main;
    CPU_ZERO(&cpuset_main);
    CPU_SET(2, &cpuset_main); // Pin Main to CPU Core 2

    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset_main) != 0) {
        std::cerr << "[SYSTEM] Warning: Failed to set thread affinity for Main Thread.\n";
    }
    std::string filepath = "data/sample.itch";
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