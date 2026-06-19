#include <iostream>
#include <cstdint>

enum class Side {
    BUY, SELL
};

struct Order {
    uint64_t orderID;
    uint64_t price;
    uint32_t quantity;
    Side side;
};

struct OrderNode {
    Order order;
    uint32_t prevOrderIdx;
    uint32_t nextOrderIdx;
};

int main() {
    std::cout << "Order Struct Size: " << sizeof(Order) << " bytes\n";
    std::cout << "OrderNode Struct Size: " << sizeof(OrderNode) << " bytes\n";

    return 0;
}