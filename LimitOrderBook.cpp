#include "LimitOrderBook.hpp" 
#include <iostream> 

void LimitOrderBook::addOrder(Order order){
    if (order.side == Side::BUY) {
        // BUY Logic
        while (order.quantity >0 && !asks.empty()) {
            auto bestAsk = asks.begin();

            if (order.price < bestAsk->first) break;

            std::list<Order>& queue = bestAsk->second;

            while (order.quantity >0 && !queue.empty()) {
                Order& restingOrder = queue.front();
                
                uint32_t fillQuantity = std::min(order.quantity, restingOrder.quantity);


                order.quantity-=fillQuantity;
                restingOrder.quantity-=fillQuantity;

                std::cout<<"[Execution] "<<fillQuantity<<" shares matched at ₹"<< bestAsk->first<<" (RestingID: "<<restingOrder.orderID<<")"<<" | Incoming ID: "<<order.orderID<<"\n";

                if (restingOrder.quantity == 0) queue.pop_front();

            }

            if (queue.empty()) asks.erase(bestAsk);
            
        }

        if (order.quantity > 0) {
            bids[order.price].push_back(order);
            std::cout<<"[Added] BUY Order "<<order.orderID<<" resting "<<order.quantity<<" shares at ₹"<<order.price<<"\n";
        }
        
    }
    else {
        //SELL Logic
        while (order.quantity > 0 && !bids.empty()) {
            auto bestBid = bids.begin();

            if (order.price > bestBid->first) break;

            std::list<Order>& queue = bestBid->second;

            while (order.quantity > 0 && !queue.empty()) {
                Order& restingOrder = queue.front();

                uint32_t fillQuantity = std::min(order.quantity, restingOrder.quantity);

                std::cout<<"[Execution] "<<fillQuantity<<" shares matched at ₹"<< bestBid->first<<" (RestingID: "<<restingOrder.orderID<<")"<<" | Incoming ID: "<<order.orderID<<"\n";

                order.quantity-=fillQuantity;
                restingOrder.quantity-=fillQuantity;

                if (restingOrder.quantity == 0) queue.pop_front();

            }

            if (queue.empty()) bids.erase(bestBid);

        }

        if (order.quantity > 0) {
            asks[order.price].push_back(order);
            std::cout << "[Added] SELL Order "<< order.orderID<<" resting "<<order.quantity<<" shares at ₹"<<order.price<<"\n";
        }       
    }
}

void LimitOrderBook::printBook() {
    std::cout<<"\n-------LIMIT ORDER BOOK-------\n";
    
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        uint32_t levelVolume = 0;
        for (const auto& o: it-> second) levelVolume+=o.quantity;
        std::cout<<"ASK | ₹"<<it->first<<" | "<<levelVolume<<" shares\n";
    }
    std::cout<<"------------------------------\n";
    
    for (const auto& [price, queue] : bids) {
        uint32_t levelVolume = 0;
        for (const auto& o: queue) levelVolume += o.quantity;
        std::cout<<"BID | ₹"<< price<<" | "<<levelVolume<<" shares\n";
    }
    std::cout<<"------------------------------\n";
}