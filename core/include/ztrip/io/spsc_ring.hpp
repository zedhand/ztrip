#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ztrip::io {

// Lock-free single-producer, single-consumer ring of trivially copyable
// values, for handing captured audio (or timestamped I/O events) from a
// device thread to the thread that evaluates the graph. write() never blocks
// or allocates: what does not fit is dropped and counted, so the reader can
// fill the gap (e.g. with silence) and keep its clock.
template <typename T>
class SpscRing {
public:
    explicit SpscRing(std::size_t capacity) : buffer_(capacity + 1) {}

    std::size_t capacity() const noexcept { return buffer_.size() - 1; }

    // Producer: appends as many values as fit, returns how many.
    std::size_t write(std::span<const T> values) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t n = std::min(values.size(), free_space(head, tail));
        copy_in(head, values.first(n));
        head_.store((head + n) % buffer_.size(), std::memory_order_release);
        if (n < values.size()) dropped_.fetch_add(values.size() - n, std::memory_order_relaxed);
        return n;
    }

    // Consumer: takes up to out.size() values, returns how many.
    std::size_t read(std::span<T> out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t n = std::min(out.size(), used(head, tail));
        for (std::size_t i = 0; i < n; ++i) out[i] = buffer_[(tail + i) % buffer_.size()];
        tail_.store((tail + n) % buffer_.size(), std::memory_order_release);
        return n;
    }

    // Consumer: values waiting to be read.
    std::size_t available() const noexcept {
        return used(head_.load(std::memory_order_acquire), tail_.load(std::memory_order_relaxed));
    }

    // Consumer: values dropped since the last call, and resets the count.
    std::uint64_t take_dropped() noexcept { return dropped_.exchange(0, std::memory_order_relaxed); }

private:
    std::size_t used(std::size_t head, std::size_t tail) const noexcept {
        return (head + buffer_.size() - tail) % buffer_.size();
    }
    std::size_t free_space(std::size_t head, std::size_t tail) const noexcept {
        return capacity() - used(head, tail);
    }
    void copy_in(std::size_t head, std::span<const T> values) noexcept {
        for (std::size_t i = 0; i < values.size(); ++i) buffer_[(head + i) % buffer_.size()] = values[i];
    }

    std::vector<T> buffer_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

} // namespace ztrip::io
