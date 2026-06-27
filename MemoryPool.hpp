#pragma once
#include <vector>
#include <sys/mman.h>
#include <iostream>
#include <cstdlib>
#include "Order.hpp"

class MemoryPool {
    private:
        OrderNode* poolPtr;
        size_t poolCapacity;
        std::vector<uint32_t> freeList;

    public:
        MemoryPool(size_t capacity) {
            poolCapacity = capacity;
            poolPtr = static_cast<OrderNode*>(mmap(nullptr, capacity * sizeof(OrderNode), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0));
            if (poolPtr == MAP_FAILED) {
                std::cerr << "[SYSTEM] CRITICAL ERROR: mmap failed to allocate MemoryPool!\n";
                exit(1);
            }
            madvise(poolPtr, capacity * sizeof(OrderNode), MADV_HUGEPAGE);
            freeList.reserve(capacity);
            for (size_t i = capacity-1; i > 0; i--) {
                freeList.push_back(i);
            }
        }

        ~MemoryPool() {
            if (poolPtr != MAP_FAILED) {
                munmap(poolPtr, poolCapacity * sizeof(OrderNode));
            }
        }

        uint32_t allocate() {
            if (freeList.empty()) return 0;
            uint32_t idx = freeList.back();
            freeList.pop_back();
            return idx;
        }

        void deallocate(uint32_t idx) {
            freeList.push_back(idx);
        }

        OrderNode& get(uint32_t idx) {
            return poolPtr[idx];
        } 
};