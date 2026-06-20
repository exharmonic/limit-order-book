#pragma once
#include <cstdint>

enum class Side: uint8_t {
    BUY = 0,
    SELL = 1
};

struct Order {
    uint64_t orderID;
    uint32_t price;
    uint32_t quantity;
    Side side;
};

struct OrderNode {
    Order order;
    uint32_t prevOrderIdx = 0;
    uint32_t nextOrderIdx = 0;
};

struct PriceLevel {
    uint32_t volume;
    uint32_t headOrderIdx;
    uint32_t tailOrderIdx;
};
