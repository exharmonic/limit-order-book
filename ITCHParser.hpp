#pragma once
#include <cstdint>
#include <sys/mman.h>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include "RingBuffer.hpp"
#include "Order.hpp"


#pragma pack(push, 1)

/*
OFFICIAL NASDAQ ITCH PROTOCOL SPECIFICATION

Name                    Offset  Length  Value       Notes 
Message Type            0       1       “A”         Add Order – No MPID Attribution Message. 
Stock Locate            1       2       Integer     Locate code identifying the security 
Tracking Number         3       2       Integer     Nasdaq internal tracking number 
Timestamp               5       6       Integer     Nanoseconds since midnight. 
Order Reference Number  11      8       Integer     The unique reference number assigned to the new order at the time of receipt.  
Buy/Sell Indicator      19      1       Alpha       The type of order being added. “B” = Buy Order. “S” = Sell Order. 
Shares                  20      4       Integer     The total number of shares associated with the order being added to the book. 
Stock                   24      8       Alpha       Stock symbol, right padded with spaces 
Price                   32      4       Price       (4) The display price of the new order. Refer to Data Types for field processing notes. 
*/

struct ITCHOrderMessage { // Type A
    char messageType;
    uint16_t stockLocate;
    uint16_t trackingNumber;
    uint8_t timestamp[6];
    uint64_t orderRef;
    char buySellIndicator;
    uint32_t shares;
    char stock[8];
    uint32_t price;
};

struct ITCHAddOrderAttributionMessage { // Type 'F'
    char messageType;           
    uint16_t stockLocate;
    uint16_t trackingNumber;
    uint8_t timestamp[6];         
    uint64_t orderRef;    
    char buySellIndicator;      
    uint32_t shares;            
    char stock[8];              
    uint32_t price;             
    char attribution[4];
};

#pragma pack(pop)

class ITCHParser {
    public:
        static inline uint16_t bswap16(uint16_t val) { return __builtin_bswap16(val); }
        static inline uint32_t bswap32(uint32_t val) { return __builtin_bswap32(val); }
        static inline uint64_t bswap64(uint64_t val) { return __builtin_bswap64(val); }
        static inline uint64_t parse48(const uint8_t* ts) {
        return (static_cast<uint64_t>(ts[0]) << 40) | (static_cast<uint64_t>(ts[1]) << 32) | (static_cast<uint64_t>(ts[2]) << 24) |
               (static_cast<uint64_t>(ts[3]) << 16) | (static_cast<uint64_t>(ts[4]) << 8)  | (static_cast<uint64_t>(ts[5]));
        }

        static void parseAndPush(const char* filepath, RingBuffer<Order, 1048576>& buffer) {
            int fd = open(filepath, O_RDONLY);
            if (fd == -1) {
                std::cerr<<"[SYSTEM] Failed to open the dataset.\n";
                return;
            }
            struct stat sb;
            if (fstat(fd, &sb) == -1) {
                std::cerr<<"[SYSTEM] Failed to get file size.\n";
                return;
            }
            size_t length = sb.st_size; // Size of files in bytes
            
            const char* data = static_cast<const char*>(mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0));

            if (data == MAP_FAILED) {
                std::cerr<<"[NETWORK] Failed to map memory.\n";
                return;
            }

            const char* ptr = data;
            const char* end = data + length;
            while (ptr + 2 <= end) { // Must have at least 2 bytes to read the length header
                uint16_t msgLength = bswap16(*reinterpret_cast<const uint16_t*>(ptr));
                ptr += 2; // Step past the length header
                if (ptr + msgLength > end) break;

                char readMessage = *ptr;

                switch(readMessage) {
                    case 'A': { // Add order.
                        const auto* msg = reinterpret_cast<const ITCHOrderMessage*> (ptr); // Use the sizes of the struct elements and automatically cast raw binary into respective struct elements.

                        Order order;
                        order.orderID = bswap64(msg->orderRef);
                        order.quantity = bswap32(msg->shares);
                        order.price = bswap32(msg->price);
                        order.side = (msg->buySellIndicator == 'B')? Side::BUY : Side::SELL;

                        while (!buffer.push(order)) {
                            // Drop logic or Retry Spin logic
                        }
                        break;
                    }
                    case 'F': {
                        const auto* msg = reinterpret_cast<const ITCHAddOrderAttributionMessage*> (ptr); // Use the sizes of the struct elements and automatically cast raw binary into respective struct elements.

                        Order order;
                        order.orderID = bswap64(msg->orderRef);
                        order.quantity = bswap32(msg->shares);
                        order.price = bswap32(msg->price);
                        order.side = (msg->buySellIndicator == 'B')? Side::BUY : Side::SELL;

                        while (!buffer.push(order)) {
                            // Drop logic or Retry Spin logic
                        }
                        break;
                    }
                    default:
                        break;
                    }
                ptr += msgLength;   
            }
            munmap((void*)data, length);
            close(fd);
        }
};
