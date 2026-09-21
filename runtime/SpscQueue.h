#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace scn::runtime
{

// Bounded single-producer/single-consumer queue for the future live pipeline.
// The current session uses the same bounded semantics while the algorithm
// worker remains the sole owner of DetectionEngine state.
template <typename T>
class SpscQueue
{
public:
    explicit SpscQueue(std::size_t capacity)
        : m_capacity(capacity + 1), m_buffer(m_capacity)
    {
    }

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    bool tryPush(const T& value)
    {
        const auto tail = m_tail.load(std::memory_order_relaxed);
        const auto next = increment(tail);
        if (next == m_head.load(std::memory_order_acquire)) {
            return false;
        }
        m_buffer[tail] = value;
        m_tail.store(next, std::memory_order_release);
        return true;
    }

    bool tryPush(T&& value)
    {
        const auto tail = m_tail.load(std::memory_order_relaxed);
        const auto next = increment(tail);
        if (next == m_head.load(std::memory_order_acquire)) {
            return false;
        }
        m_buffer[tail] = std::move(value);
        m_tail.store(next, std::memory_order_release);
        return true;
    }

    bool tryPop(T& value)
    {
        const auto head = m_head.load(std::memory_order_relaxed);
        if (head == m_tail.load(std::memory_order_acquire)) {
            return false;
        }
        value = std::move(m_buffer[head]);
        m_head.store(increment(head), std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity - 1; }

private:
    [[nodiscard]] std::size_t increment(std::size_t index) const noexcept
    {
        return (index + 1) % m_capacity;
    }

    const std::size_t m_capacity;
    std::vector<T> m_buffer;
    std::atomic<std::size_t> m_head{0};
    std::atomic<std::size_t> m_tail{0};
};

} // namespace scn::runtime
