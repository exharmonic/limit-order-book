#pragma once
#include <atomic>
#include <array>
#include <cstddef>

template <typename T, size_t Capacity>
class RingBuffer {

    private:
        std::array<T, Capacity> buffer;
        alignas(64) std::atomic<size_t> head{0}; // Written exclusively by the Engine thread.
        alignas(64) std::atomic<size_t> tail{0}; // Written exclusively by the Network thread.

    public:
        bool push(const T& item) {
            size_t currentTail = tail.load(std::memory_order_relaxed);
            size_t nextTail = (currentTail+1) % Capacity;

            if (nextTail == head.load(std::memory_order_acquire)) {
                return false;
            }

            buffer[currentTail] = item;
            tail.store(nextTail, std::memory_order_release);

            return true;
        }
        bool pop(T& item) {
            size_t currentHead = head.load(std::memory_order_relaxed);

            if (currentHead == tail.load(std::memory_order_acquire)) {
                return false;
            }

            item = buffer[currentHead];
            head.store((currentHead+1) % Capacity, std::memory_order_release);

            return true;

        }
};