#pragma once
#include "Order.hpp"
#include "MemoryPool.hpp"
#include <array>

constexpr size_t MAX_PRICE = 100000; 
constexpr size_t MAX_ORDERS = 1000000;

class LimitOrderBook {
    private:
        MemoryPool orderPool;
        uint32_t bestAsk = MAX_PRICE;
        uint32_t bestBid = 0;
        std::array<PriceLevel, MAX_PRICE> bids;
        std::array<PriceLevel, MAX_PRICE> asks;

    public:
        LimitOrderBook() : orderPool(MAX_ORDERS) {};

        void addOrder(Order order);
        void printBook();

};