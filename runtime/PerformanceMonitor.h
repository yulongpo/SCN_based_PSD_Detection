#pragma once

#include <cstdint>
#include <algorithm>
#include <deque>
#include <vector>
#include <chrono>

namespace scn::runtime
{

class PerformanceMonitor
{
public:
    void recordFrame(double processingTimeMs)
    {
        m_totalMs += processingTimeMs;
        ++m_count;
        m_recent.push_back(processingTimeMs);
        if (m_recent.size() > 2048) m_recent.pop_front();
    }
    void reset() { m_totalMs = 0; m_count = 0; m_recent.clear(); m_started = std::chrono::steady_clock::now(); }
    double throughputHz() const
    {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_started).count();
        return seconds > 0 ? static_cast<double>(m_count) / seconds : 0;
    }
    double percentile(double fraction) const
    {
        if (m_recent.empty()) return 0;
        std::vector<double> sorted(m_recent.begin(), m_recent.end());
        const auto index = static_cast<std::size_t>(std::clamp(fraction, 0.0, 1.0) * (sorted.size() - 1));
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(index), sorted.end());
        return sorted[index];
    }

    [[nodiscard]] std::uint64_t frameCount() const noexcept { return m_count; }
    [[nodiscard]] double averageMs() const noexcept
    {
        return m_count == 0 ? 0.0 : m_totalMs / static_cast<double>(m_count);
    }

private:
    double m_totalMs = 0.0;
    std::uint64_t m_count = 0;
    std::deque<double> m_recent;
    std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
};

} // namespace scn::runtime
