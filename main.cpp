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
#include "PCAPITCHParser.hpp"

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
                if (incomingOrder.price == 0) {
                    engine.addMarketOrder(incomingOrder);
                } else {
            engine.addOrder(incomingOrder);
                }
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

void loggerThread(std::atomic<bool>& loggingActive) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(6, &cpuset);

    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        std::cerr << "[SYSTEM] Warning: Failed to set thread affinity for Logger Thread.\n";
    }

    std::ofstream fillLog("fills.csv", std::ios::out | std::ios::trunc);
    if (!fillLog.is_open()) {
        std::cerr << "[SYSTEM] Warning: Failed to open fills.csv for trade logging.\n";
        return;
    }
    fillLog << "restingOrderID,aggressorOrderID,price,fillQuantity,aggressorSide\n";

    FillEvent fe;
    uint64_t loggedCount = 0;

    auto drainAvailable = [&]() {
        while (engine.popFill(fe)) {
            fillLog << fe.restingOrderID << ',' << fe.aggressorOrderID << ','
                     << fe.price << ',' << fe.fillQuantity << ','
                     << (fe.aggressorSide == Side::BUY ? "B" : "S") << '\n';
            ++loggedCount;
        }
    };

    while (loggingActive.load(std::memory_order_acquire)) {
        drainAvailable();
        _mm_pause();
    }

    drainAvailable();

    fillLog.flush();
    std::cout << "[LOGGER] Logged " << loggedCount << " fills to fills.csv. Dropped fills (queue full): "
              << engine.getDroppedFillCount() << "\n";
}

int main(int argc, char* argv[]) {
    
    cpu_set_t cpuset_main;
    CPU_ZERO(&cpuset_main);
    CPU_SET(2, &cpuset_main); // Pin Main to CPU Core 2

    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset_main) != 0) {
        std::cerr << "[SYSTEM] Warning: Failed to set thread affinity for Main Thread.\n";
    }
    std::string filepath = "../data/sample.pcap"; // Default relative to build/ dir
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

    // Launching the fill-logging consumer thread (drains engine.fillQueue)
    std::atomic<bool> loggingActive{true};
    std::thread logger(loggerThread, std::ref(loggingActive));
    
    std::cout << "[MAIN] Executing a burst of 1,000,000 orders into the queue...\n";

    auto start = std::chrono::high_resolution_clock::now();
    if (view.ends_with(".csv")) {
        CSVParser::parseAndPush(filepath.c_str(), orderQueue);
    } 
    else if (view.ends_with(".itch")) {
        ITCHParser::parseAndPush(filepath.c_str(), orderQueue);
    } 
    else if (view.ends_with(".pcap")) {
        PCAPITCHParser::parseAndPush(filepath.c_str(), orderQueue);
    }
    else {
        std::cerr << "[SYSTEM] Unsupported file format.\n";
        marketOpen.store(false, std::memory_order_release);
        consumer.join();
        loggingActive.store(false, std::memory_order_release);
        logger.join();
        return 1;
    }
    marketOpen.store(false, std::memory_order_release);
    consumer.join();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    #ifdef DEBUG
    std::cout << "[ENGINE] Resting orders remaining: " << engine.restingOrderCount() << " / 1,000,000\n";
    #endif

    loggingActive.store(false, std::memory_order_release);
    logger.join();
    
    std::cout << "[MAIN] Ingestion burst completed in " << duration << " ms.\n";


    std::cout << "[MAIN] Execution verified. Core closed successfully.\n";
    
    return 0;
}