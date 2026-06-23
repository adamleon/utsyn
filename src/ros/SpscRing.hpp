#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace utsyn {

// Single-producer (ROS thread) / single-consumer (render thread) bounded ring of
// shared_ptr<const T>. The producer writes only `head_`, the consumer writes only
// `tail_`, so it is lock-free and race-free for exactly one of each. On overflow
// the newest item is dropped: monitoring favors staying current over blocking the
// ROS callback, and a 256-deep ring only fills if the render thread is far behind.
template <typename T>
class SpscRing {
public:
    explicit SpscRing(std::size_t capacity)
        : slots_(capacity ? capacity : 1) {}

    // Producer side (ROS thread).
    void push(std::shared_ptr<const T> item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            return; // full: drop newest
        }
        slots_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
    }

    // Consumer side (render thread). Returns false when empty.
    bool pop(std::shared_ptr<const T>& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        out = std::move(slots_[tail]);
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

private:
    [[nodiscard]] std::size_t increment(std::size_t i) const noexcept {
        return (i + 1) % slots_.size();
    }

    std::vector<std::shared_ptr<const T>> slots_;
    std::atomic<std::size_t>              head_{0};  // producer writes
    std::atomic<std::size_t>              tail_{0};  // consumer writes
};

} // namespace utsyn
