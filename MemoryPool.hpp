#pragma once
#include <vector>
#include <sys/mman.h>
#include "Order.hpp"

class MemoryPool {
    private:
        std::vector<OrderNode> pool;
        OrderNode* poolPtr;
        std::vector<uint32_t> freeList;

    public:
        MemoryPool(size_t capacity) {
            pool.resize(capacity);
            madvise(pool.data(), capacity * sizeof(OrderNode), MADV_HUGEPAGE);
            poolPtr = pool.data();
            freeList.reserve(capacity);

            for (size_t i = capacity-1; i > 0; i--) {
                freeList.push_back(i);
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