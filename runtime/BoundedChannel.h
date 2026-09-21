#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace scn::runtime
{
// Producer drop-oldest and consumer pop share a lock. FILE producers use a
// nonblocking capacity check from their Qt timer so stop/configure remain live.
template<class T> class BoundedChannel
{
public:
    explicit BoundedChannel(std::size_t capacity) : m_capacity(capacity)
    { if (!capacity) throw std::invalid_argument("Channel capacity must be positive."); }
    bool tryPush(T value, bool dropOldest)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed) return false;
        if (m_items.size() == m_capacity) {
            if (!dropOldest) return false;
            m_items.pop_front(); ++m_dropped;
        }
        m_items.push_back(std::move(value)); m_cv.notify_one(); return true;
    }
    template<class Cancel> bool waitPop(T& value, Cancel cancelled)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const auto wake = m_wake;
        m_cv.wait(lock, [&] { return m_closed || cancelled() || m_wake != wake || !m_items.empty(); });
        if (m_closed || cancelled() || m_wake != wake || m_items.empty()) return false;
        value = std::move(m_items.front()); m_items.pop_front(); return true;
    }
    void wake() { std::lock_guard<std::mutex> lock(m_mutex); ++m_wake; m_cv.notify_all(); }
    void clear(bool resetDropped = true)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_items.clear(); if (resetDropped) m_dropped = 0;
        ++m_wake; m_cv.notify_all();
    }
    void close() { std::lock_guard<std::mutex> lock(m_mutex); m_closed = true; m_cv.notify_all(); }
    bool hasCapacity() const { std::lock_guard<std::mutex> lock(m_mutex); return !m_closed && m_items.size() < m_capacity; }
    std::size_t size() const { std::lock_guard<std::mutex> lock(m_mutex); return m_items.size(); }
    std::uint64_t dropped() const { std::lock_guard<std::mutex> lock(m_mutex); return m_dropped; }
private:
    const std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<T> m_items;
    std::uint64_t m_wake = 0, m_dropped = 0;
    bool m_closed = false;
};
}
