#include <cstdint>
#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <immintrin.h>
#include "RingBuffer.hpp"
#include "Order.hpp"

class CSVParser {


    public:
        static inline uint32_t fastParseInt(const char*& ptr, const char* end) {
            uint32_t val = 0;
            while (ptr<end && *ptr>='0' && *ptr<='9') {
                val = val*10+(*ptr-'0');
                ptr++;
            }
            return val;
        } ;

        static void parseAndPush(const char* filepath, RingBuffer<Order, 1048576>& buffer) {

            int fd = open(filepath, O_RDONLY); // open file using the fcntl::open instead of the fstream::open (slower)
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

            // Skip the first row (CSV header). The while loop stops exactly on the '\n' character.
            while (ptr<end && *ptr!='\n') ptr++; 
            // Safely step over the '\n' so the pointer rests on the first actual byte of data.
            if (ptr<end) ptr++;

            // std::cout << "[NETWORK] Memory Mapping successful. Ingesting raw byte stream...\n";

            while (ptr<end) {
                Order order;

                order.orderID = fastParseInt(ptr, end);
                if (*ptr == ',') ptr++;
                order.price = fastParseInt(ptr, end);
                if (*ptr == ',') ptr++;
                order.quantity = fastParseInt(ptr, end);
                if (*ptr == ',') ptr++;
                order.side = (*ptr == '0')? Side::BUY : Side::SELL;
                while (ptr<end && *ptr!='\n') ptr++;
                if (ptr< end) ptr++;

                while (!buffer.push(order)) {
                    _mm_pause();
                }
            }
            munmap((void*)data, length);
            close(fd);
        }
};