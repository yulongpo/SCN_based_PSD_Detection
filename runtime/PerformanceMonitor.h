#pragma once

#include <cstdint>

namespace scn::runtime
{

class PerformanceMonitor
{
public:
    void recordFrame(double processingTimeMs) noexcept
    {
        m_totalMs += processingTimeMs;
        ++m_count;
    }

    [[nodiscard]] std::uint64_t frameCount() const noexcept { return m_count; }
    [[nodiscard]] double averageMs() const noexcept
    {
        return m_count == 0 ? 0.0 : m_totalMs / static_cast<double>(m_count);
    }

private:
    double m_totalMs = 0.0;
    std::uint64_t m_count = 0;
};

} // namespace scn::runtime
