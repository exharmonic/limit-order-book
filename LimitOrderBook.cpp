#include "LimitOrderBook.hpp" 
#include <iostream> 

void LimitOrderBook::addOrder(Order order) {
    if (order.side == Side::BUY) {
        while (order.quantity>0 && bestAsk<=order.price) {
            PriceLevel& level = asks[bestAsk];
            uint32_t currIdx = level.headOrderIdx;
            while (currIdx!=0 && order.quantity>0) {
                OrderNode& restingNode = orderPool.get(currIdx);
                Order& restingOrder = restingNode.order;

                uint32_t fillQuantity = std::min(order.quantity, restingOrder.quantity);
                
                order.quantity -= fillQuantity;
                restingOrder.quantity -= fillQuantity;
                level.volume -= fillQuantity;

                std::cout<<"[EXECUTION] "<<fillQuantity<<" shares matched at ₹"<<bestAsk<<" (RestingID: "<<restingOrder.orderID<<")"<<" | Incoming ID: "<<order.orderID<<"\n";

                uint32_t nextIdx = restingNode.nextOrderIdx;

                if (restingOrder.quantity == 0) {
                    level.headOrderIdx = nextIdx;
                    if (nextIdx != 0) {
                        orderPool.get(nextIdx).prevOrderIdx = 0;
                    }
                    else {
                        level.tailOrderIdx = 0;
                    }
                    orderPool.deallocate(currIdx);
                }
                currIdx = nextIdx;
            }
            if (level.headOrderIdx == 0) {
                bestAsk++;
            }
        }

        if (order.quantity>0) {
            uint32_t newOrderIdx = orderPool.allocate();
            if (newOrderIdx == 0) {
                std::cerr<<"CRITICAL ERROR: Order Pool Exhausted!\n";
            }

            OrderNode& newNode = orderPool.get(newOrderIdx);
            newNode.order = order;
            newNode.nextOrderIdx = 0;

            PriceLevel& level = bids[order.price];

            if (level.headOrderIdx == 0) {
                level.headOrderIdx = newOrderIdx;
                level.tailOrderIdx = newOrderIdx;
                newNode.prevOrderIdx = 0;
            }
            else {
                OrderNode& oldTail = orderPool.get(level.tailOrderIdx);
                oldTail.nextOrderIdx = newOrderIdx;
                newNode.prevOrderIdx = level.tailOrderIdx;
                level.tailOrderIdx = newOrderIdx;
            }

            level.volume+=order.quantity;

            if (order.price>bestBid) {
                bestBid = order.price;
            }

            std::cout<<"[ADDED] BUY Order "<<order.orderID<<" resting "<<order.quantity<<" shares at ₹"<<order.price<<"\n";
        }
    }
    else {
        while (order.quantity>0 && bestBid>=order.price) {
            PriceLevel& level = bids[bestBid];
            uint32_t currIdx = level.headOrderIdx;
            while (currIdx!=0 && order.quantity>0) {
                OrderNode& restingNode = orderPool.get(currIdx);
                Order& restingOrder = restingNode.order;

                uint32_t fillQuantity = std::min(order.quantity, restingOrder.quantity);
                
                order.quantity -= fillQuantity;
                restingOrder.quantity -= fillQuantity;
                level.volume -= fillQuantity;

                std::cout<<"[EXECUTION] "<<fillQuantity<<" shares matched at ₹"<<bestBid<<" (RestingID: "<<restingOrder.orderID<<")"<<" | Incoming ID: "<<order.orderID<<"\n";

                uint32_t nextIdx = restingNode.nextOrderIdx;

                if (restingOrder.quantity == 0) {
                    level.headOrderIdx = nextIdx;
                    if (nextIdx != 0) {
                        orderPool.get(nextIdx).prevOrderIdx = 0;
                    }
                    else {
                        level.tailOrderIdx = 0;
                    }
                    orderPool.deallocate(currIdx);
                }
                currIdx = nextIdx;
            }
            if (level.headOrderIdx == 0) {
                if (bestBid>0) bestBid--;
                else break;
            }
        }

        if (order.quantity>0) {
            uint32_t newOrderIdx = orderPool.allocate();
            if (newOrderIdx == 0) {
                std::cerr<<"CRITICAL ERROR: Order Pool Exhausted!\n";
            }

            OrderNode& newNode = orderPool.get(newOrderIdx);
            newNode.order = order;
            newNode.nextOrderIdx = 0;

            PriceLevel& level = asks[order.price];

            if (level.headOrderIdx == 0) {
                level.headOrderIdx = newOrderIdx;
                level.tailOrderIdx = newOrderIdx;
                newNode.prevOrderIdx = 0;
            }
            else {
                OrderNode& oldTail = orderPool.get(level.tailOrderIdx);
                oldTail.nextOrderIdx = newOrderIdx;
                newNode.prevOrderIdx = level.tailOrderIdx;
                level.tailOrderIdx = newOrderIdx;
            }

            level.volume+=order.quantity;

            if (order.price<bestAsk) {
                bestAsk = order.price;
            }

            std::cout<<"[ADDED] SELL Order "<<order.orderID<<" resting "<<order.quantity<<" shares at ₹"<<order.price<<"\n";
        }
    }
};

void LimitOrderBook::printBook() {
    std::cout << "\n----------LIMIT ORDER BOOK----------\n";

    for (uint32_t i = 500; i > 0; --i) {
        uint32_t price = bestAsk + i;
        if (price < MAX_PRICE && asks[price].headOrderIdx != 0) {
            std::cout << "ASK | ₹" << price << " | " << asks[price].volume << " shares\n";
        }
    }
    
    if (bestAsk < MAX_PRICE && asks[bestAsk].headOrderIdx != 0) {
        std::cout << "ASK | ₹" << bestAsk << " | " << asks[bestAsk].volume << " shares\n";
    }

    std::cout << "------------------------------------\n";
    
    for (uint32_t i = 0; i <= 500; ++i) {
        if (bestBid < i) break;
        
        uint32_t price = bestBid - i;
        
        if (bids[price].headOrderIdx != 0) {
            std::cout << "BID | ₹" << price << " | " << bids[price].volume << " shares\n";
        }
    }
    std::cout << "------------------------------------\n\n";
}