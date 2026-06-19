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