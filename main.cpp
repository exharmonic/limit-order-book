#include "LimitOrderBook.hpp"
#include <iostream>

int main() {
    std::cout << "Starting Limit Order Book Engine...\n";
    
    LimitOrderBook engine;

    engine.addOrder({1, 15000, 100, Side::BUY});
    engine.addOrder({2, 14900, 50, Side::BUY});
    engine.addOrder({3, 15100, 200, Side::SELL});
    engine.addOrder({4, 15200, 100, Side::SELL});

    std::cout << "\n[STATE 1: Pre-Trade Book]\n";
    engine.printBook();

    std::cout << "\n[INCOMING: Aggressive Market Taker]\n";
    engine.addOrder({5, 15150, 250, Side::BUY});

    std::cout << "\n[STATE 2: Post-Trade Book]\n";
    engine.printBook();

    return 0;
}