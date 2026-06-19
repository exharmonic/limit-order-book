#pragma once
#include <map>
#include "Order.hpp"
#include <list>


class LimitOrderBook {
    private:
        std::map<uint64_t, std::list<Order>, std::greater<uint64_t>> bids;
        std::map<uint64_t, std::list<Order>, std::less<uint64_t>> asks;
    public:
        void addOrder(Order order);
        void printBook();

};