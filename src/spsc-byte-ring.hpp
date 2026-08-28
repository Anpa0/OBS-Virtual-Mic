#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

class SpscByteRing {
public:
    explicit SpscByteRing(size_t capacity)
        : buffer_(capacity + 1), capacity_(capacity + 1) {}

    size_t write(const uint8_t *src, size_t bytes)
    {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        const size_t free = freeSpace(head, tail);
        const size_t count = std::min(bytes, free);
        if (count == 0)
            return 0;

        const size_t first = std::min(count, capacity_ - head);
        std::memcpy(buffer_.data() + head, src, first);
        if (count > first)
            std::memcpy(buffer_.data(), src + first, count - first);

        head_.store((head + count) % capacity_, std::memory_order_release);
        return count;
    }

    size_t read(uint8_t *dst, size_t bytes)
    {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t avail = available(head, tail);
        const size_t count = std::min(bytes, avail);
        if (count == 0)
            return 0;

        const size_t first = std::min(count, capacity_ - tail);
        std::memcpy(dst, buffer_.data() + tail, first);
        if (count > first)
            std::memcpy(dst + first, buffer_.data(), count - first);

        tail_.store((tail + count) % capacity_, std::memory_order_release);
        return count;
    }

    size_t size() const
    {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return available(head, tail);
    }

    void clear()
    {
        const size_t head = head_.load(std::memory_order_acquire);
        tail_.store(head, std::memory_order_release);
    }

private:
    size_t available(size_t head, size_t tail) const
    {
        return head >= tail ? head - tail : capacity_ - tail + head;
    }

    size_t freeSpace(size_t head, size_t tail) const
    {
        return capacity_ - 1 - available(head, tail);
    }

    std::vector<uint8_t> buffer_;
    const size_t capacity_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};
