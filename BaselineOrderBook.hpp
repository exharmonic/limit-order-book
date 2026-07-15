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
    
    size_t restingOrderCount() const {
        size_t total = 0;
        for (const auto& [price, level] : bids) total += level.size();
        for (const auto& [price, level] : asks) total += level.size();
        return total;
    }

    void addBid(uint32_t price, uint64_t orderId, uint64_t quantity) {
    auto it = asks.begin();
    while (it != asks.end() && quantity > 0 && it->first <= price) {
        auto& level = it->second;
        auto lit = level.begin();
        while (lit != level.end() && quantity > 0) {
            uint64_t fillQty = std::min(quantity, lit->quantity);
            quantity -= fillQty;
            lit->quantity -= fillQty;
            lit = (lit->quantity == 0) ? level.erase(lit) : std::next(lit);
        }
        it = level.empty() ? asks.erase(it) : std::next(it);
    }
    if (quantity > 0) bids[price].push_back({orderId, quantity});
}

void addAsk(uint32_t price, uint64_t orderId, uint64_t quantity) {
    auto it = bids.begin();
    while (it != bids.end() && quantity > 0 && it->first >= price) {
        auto& level = it->second;
        auto lit = level.begin();
        while (lit != level.end() && quantity > 0) {
            uint64_t fillQty = std::min(quantity, lit->quantity);
            quantity -= fillQty;
            lit->quantity -= fillQty;
            lit = (lit->quantity == 0) ? level.erase(lit) : std::next(lit);
        }
        it = level.empty() ? bids.erase(it) : std::next(it);
    }
    if (quantity > 0) asks[price].push_back({orderId, quantity});
}

    void cancelOrder(uint32_t price, uint64_t orderId, bool isBuy) {
        if (isBuy) {
            auto& orderList = bids[price];
            orderList.remove_if([orderId](const BaselineOrder& o) { return o.orderId == orderId; });
            if (orderList.empty()) bids.erase(price);
        } else {
            auto& orderList = asks[price];
            orderList.remove_if([orderId](const BaselineOrder& o) { return o.orderId == orderId; });
            if (orderList.empty()) asks.erase(price);
        }
    }
};