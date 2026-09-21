#pragma once
#include "../types/SpectrumTypes.h"
#include <memory>

namespace scn::algorithm
{
class TemporalAccumulator
{
public:
    void reset(std::size_t capacity);
    void push(const SpectrumFrame& frame);
    void commit() noexcept { m_pending = false; }
    void rollback();
    const std::vector<float>& average() const noexcept { return m_average; }
    const std::vector<float>& maximum() const noexcept { return m_maximum; }
    std::size_t count() const noexcept { return m_count; }
    std::uint64_t firstSequence() const;
    std::int64_t firstTimestampNs() const;
private:
    struct Row { std::vector<float> power; std::uint64_t sequence = 0; std::int64_t time = 0; };
    std::vector<Row> m_rows;
    std::vector<double> m_sum;
    std::vector<float> m_average, m_maximum;
    std::size_t m_next = 0, m_count = 0;
    Row m_backup;
    std::size_t m_previousCount = 0, m_previousNext = 0;
    bool m_pending = false;
};
}
