#pragma once
#include "Order.hpp"
#include "MemoryPool.hpp"
#include "RingBuffer.hpp"
#include <array>

constexpr size_t MAX_PRICE = 100001; 
constexpr size_t MAX_ORDERS = 1100001;
constexpr int BIT_WORDS = (MAX_PRICE / 64) + 1;
constexpr size_t FILL_QUEUE_CAPACITY = 1048576;

struct FillEvent {
    uint64_t restingOrderID;
    uint64_t aggressorOrderID;
    uint32_t price;
    uint32_t fillQuantity;
    Side     aggressorSide;
};

class LimitOrderBook {
    private:
        MemoryPool orderPool;
        uint32_t bestAsk = MAX_PRICE;
        uint32_t bestBid = 0;
        std::array<PriceLevel, MAX_PRICE> bids;
        std::array<PriceLevel, MAX_PRICE> asks;
        std::array<uint32_t, MAX_ORDERS> orderMap = {0};
        uint64_t bidWords[BIT_WORDS] = {0};
        uint64_t askWords[BIT_WORDS] = {0};

        RingBuffer<FillEvent, FILL_QUEUE_CAPACITY> fillQueue;
        uint64_t droppedFillCount = 0;

        uint32_t findNextBestBid(uint32_t currentBid);
        uint32_t findNextBestAsk(uint32_t currentAsk);

    public:
        LimitOrderBook();

        void addOrder(Order order);
        void addMarketOrder(Order order);
        void cancelOrder(uint32_t orderID);
        void printBook();
        size_t restingOrderCount();

        bool popFill(FillEvent& out) { return fillQueue.pop(out); }
        uint64_t getDroppedFillCount() const { return droppedFillCount; }

};