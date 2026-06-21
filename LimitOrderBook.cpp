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

                //std::cout<<"[EXECUTION] "<<fillQuantity<<" shares matched at ₹"<<bestAsk<<" (RestingID: "<<restingOrder.orderID<<")"<<" | Incoming ID: "<<order.orderID<<"\n";

                uint32_t nextIdx = restingNode.nextOrderIdx;

                if (restingOrder.quantity == 0) {
                    level.headOrderIdx = nextIdx;
                    if (nextIdx != 0) {
                        orderPool.get(nextIdx).prevOrderIdx = 0;
                    }
                    else {
                        level.tailOrderIdx = 0;
                    }
                    orderMap[restingOrder.orderID] = 0; // We assume zero poolIdx as the integer "nullptr" equivalent.
                    orderPool.deallocate(currIdx);
                }
                currIdx = nextIdx;
            }
            if (level.headOrderIdx == 0) {
                bestAsk++;
            }
        }

        if (order.quantity>0) { // BUY Order is not fully satisfied, i.e. bestAsk > order.price. So, add the order to the bids array.
            uint32_t newOrderIdx = orderPool.allocate();
            if (newOrderIdx == 0) {
                std::cerr<<"CRITICAL ERROR: Order Pool Exhausted!\n";
            }

            OrderNode& newNode = orderPool.get(newOrderIdx);
            newNode.order = order;
            orderMap[order.orderID] = newOrderIdx; // Update orderMap to look for this order in O(1) time.

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

            //std::cout<<"[ADDED] BUY Order "<<order.orderID<<" resting "<<order.quantity<<" shares at ₹"<<order.price<<"\n";
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

                //std::cout<<"[EXECUTION] "<<fillQuantity<<" shares matched at ₹"<<bestBid<<" (RestingID: "<<restingOrder.orderID<<")"<<" | Incoming ID: "<<order.orderID<<"\n";

                uint32_t nextIdx = restingNode.nextOrderIdx;

                if (restingOrder.quantity == 0) {
                    level.headOrderIdx = nextIdx;
                    if (nextIdx != 0) {
                        orderPool.get(nextIdx).prevOrderIdx = 0;
                    }
                    else {
                        level.tailOrderIdx = 0;
                    }

                    orderMap[restingOrder.orderID] = 0; // Set its poolIdx as zero.
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
            orderMap[order.orderID] = newOrderIdx;
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

            //std::cout<<"[ADDED] SELL Order "<<order.orderID<<" resting "<<order.quantity<<" shares at ₹"<<order.price<<"\n";
        }
    }
};

void LimitOrderBook::cancelOrder(uint32_t orderID) {
    uint32_t poolIdx = orderMap[orderID];

    if (poolIdx == 0) return;

    OrderNode& delNode = orderPool.get(poolIdx);
    Order& delOrder = delNode.order;

    uint32_t prevNodeIdx = delNode.prevOrderIdx;
    uint32_t nextNodeIdx = delNode.nextOrderIdx;
    PriceLevel& level = delOrder.side == Side::BUY ? bids[delOrder.price] : asks[delOrder.price];
    
    if (prevNodeIdx != 0) { // Previous node exists.
        orderPool.get(prevNodeIdx).nextOrderIdx = nextNodeIdx;
    }
    else { // First node of the level. Set the next node as the head of the level.
        level.headOrderIdx = nextNodeIdx;
    }

    if (nextNodeIdx != 0) { // Next node exists. Can safely grab nextNodeIdx
        orderPool.get(nextNodeIdx).prevOrderIdx =prevNodeIdx; 
    }
    else { // Last node of the level. Set the previous node as the tail of the level.
        level.tailOrderIdx = prevNodeIdx;
    }

    level.volume-=delOrder.quantity;
    orderPool.deallocate(poolIdx);
    orderMap[orderID] = 0; // We assume zero poolIdx as the integer "nullptr" equivalent.

    // If the level just emptied out completely due to this cancellation, update our global tracking anchors
    if (level.headOrderIdx == 0) {

        if (delOrder.side == Side::BUY && delOrder.price == bestBid) {
            while (bestBid > 0 && bids[bestBid].headOrderIdx == 0) {
                bestBid--;
            }
        } else if (delOrder.side == Side::SELL && delOrder.price == bestAsk) {
            while (bestAsk < MAX_PRICE && asks[bestAsk].headOrderIdx == 0) {
                bestAsk++;
            }
        }
        
    }

    //std::cout << "[CANCELLED] Order " << orderID << " cancelled.\n"; // Order cancelled in O(1) time.

}

void LimitOrderBook::printBook() {
    //std::cout << "\n----------LIMIT ORDER BOOK----------\n";

    for (uint32_t i = 500; i > 0; --i) {
        uint32_t price = bestAsk + i;
        if (price < MAX_PRICE && asks[price].headOrderIdx != 0) {
            //std::cout << "ASK | ₹" << price << " | " << asks[price].volume << " shares\n";
        }
    }
    
    if (bestAsk < MAX_PRICE && asks[bestAsk].headOrderIdx != 0) {
        //std::cout << "ASK | ₹" << bestAsk << " | " << asks[bestAsk].volume << " shares\n";
    }

    //std::cout << "------------------------------------\n";
    
    for (uint32_t i = 0; i <= 500; ++i) {
        if (bestBid < i) break;
        
        uint32_t price = bestBid - i;
        
        if (bids[price].headOrderIdx != 0) {
            //std::cout << "BID | ₹" << price << " | " << bids[price].volume << " shares\n";
        }
    }
    //std::cout << "------------------------------------\n\n";
}