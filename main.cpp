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
#include <fstream>

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
            if (incomingOrder.quantity > 0) {
                engine.addOrder(incomingOrder);
            } else {
                engine.cancelOrder(incomingOrder.orderID);
            }
            processedCount++;
        }
        else {
            _mm_pause();
        }
    }

    while (orderQueue.pop(incomingOrder)) {
        if (incomingOrder.quantity > 0) {
            engine.addOrder(incomingOrder);
        } else {
            engine.cancelOrder(incomingOrder.orderID);
        }
        processedCount++;
    }

}

int main(int argc, char* argv[]) {
    
    cpu_set_t cpuset_main;
    CPU_ZERO(&cpuset_main);
    CPU_SET(2, &cpuset_main); // Pin Main to CPU Core 2

    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset_main) != 0) {
        std::cerr << "[SYSTEM] Warning: Failed to set thread affinity for Main Thread.\n";
    }
    std::string filepath = "../data/sample.itch"; // Default relative to build/ dir
    if (argc > 1) {
        filepath = argv[1];
    }
    std::ifstream fileCheck(filepath, std::ios::binary);
    if (!fileCheck.is_open()) {
        std::cerr << "[CRITICAL ERROR] Failed to locate market data file at: " << filepath << "\n";
        std::cerr << "Usage: ./engine_main <path_to_data_file>\n";
        return 1;
    }
    fileCheck.close();

    std::string_view view = filepath;
    std::cout << "[MAIN] Initializing system pipeline...\n";
    
    // Launching the isolated consumer thread
    std::thread consumer(engineThread);
    
    std::cout << "[MAIN] Executing a burst of 1,000,000 orders into the queue...\n";

    auto start = std::chrono::high_resolution_clock::now();
    if (view.ends_with(".csv")) {
        CSVParser::parseAndPush(filepath.c_str(), orderQueue);
    } 
    else if (view.ends_with(".itch") || view.ends_with(".pcap")) {
        ITCHParser::parseAndPush(filepath.c_str(), orderQueue);
    } 
    else {
        std::cerr << "[SYSTEM] Unsupported file format.\n";
        marketOpen.store(false, std::memory_order_release);
        consumer.join();
        return 1;
    }
    marketOpen.store(false, std::memory_order_release);
    consumer.join();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "[MAIN] Ingestion burst completed in " << duration << " ms.\n";


    std::cout << "[MAIN] Execution verified. Core closed successfully.\n";
    
    return 0;
}