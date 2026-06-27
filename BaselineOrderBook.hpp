#pragma once
#include <map>
#include <list>
#include <cstdint>

struct BaselineOrder {
    uint64_t orderId;
    uint64_t quantity;
};

class BaselineOrderBook {
private:
    std::map<uint32_t, std::list<BaselineOrder>, std::greater<uint32_t>> bids;
    std::map<uint32_t, std::list<BaselineOrder>> asks;

public:
    void addBid(uint32_t price, uint64_t orderId, uint64_t quantity) {
        bids[price].push_back({orderId, quantity});
    }

    void addAsk(uint32_t price, uint64_t orderId, uint64_t quantity) {
        asks[price].push_back({orderId, quantity});
    }

    // Simulate an aggressive market sell sweeping through the resting bids
    void sweepBids(uint64_t totalQuantity) {
        auto it = bids.begin();
        while (it != bids.end() && totalQuantity > 0) {
            auto& orderList = it->second;
            auto orderIt = orderList.begin();
            
            while (orderIt != orderList.end() && totalQuantity > 0) {
                if (orderIt->quantity <= totalQuantity) {
                    totalQuantity -= orderIt->quantity;
                    orderIt = orderList.erase(orderIt);
                } else {
                    orderIt->quantity -= totalQuantity;
                    totalQuantity = 0;
                }
            }
            
            if (orderList.empty()) {
                it = bids.erase(it);
            } else {
                ++it;
            }
        }
    }
};