#include "TemporalAccumulator.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace scn::algorithm
{
void TemporalAccumulator::reset(std::size_t capacity)
{
    if (!capacity) throw std::invalid_argument("Accumulator capacity must be positive.");
    m_rows.clear(); m_rows.resize(capacity);
    m_sum.clear(); m_average.clear(); m_maximum.clear();
    m_next = m_count = 0;
    m_backup = {}; m_pending = false;
}
void TemporalAccumulator::push(const SpectrumFrame& f)
{
    if (m_rows.empty()) reset(16);
    if (m_count && m_sum.size() != f.powerDb.size()) reset(m_rows.size());
    if (!m_count) {
        m_sum.assign(f.powerDb.size(), 0);
        m_average.resize(f.powerDb.size());
        m_maximum.assign(f.powerDb.size(), -std::numeric_limits<float>::infinity());
    }
    auto& row = m_rows[m_next];
    const bool full = m_count == m_rows.size();
    m_previousCount = m_count; m_previousNext = m_next;
    std::swap(row, m_backup); // One reusable spare row makes cancellation reversible.
    m_pending = true;
    const auto nextCount = std::min(m_count + 1, m_rows.size());
    for (std::size_t i = 0; i < f.powerDb.size(); ++i) {
        const float added = f.powerDb[i];
        const float removed = full ? m_backup.power[i] : -std::numeric_limits<float>::infinity();
        m_sum[i] += static_cast<double>(added) - (full ? removed : 0.0);
        if (added >= m_maximum[i]) m_maximum[i] = added;
        else if (full && removed >= m_maximum[i]) {
            float maximum = added;
            for (std::size_t r = 0; r < m_count; ++r)
                if (r != m_next) maximum = std::max(maximum, m_rows[r].power[i]);
            m_maximum[i] = maximum;
        }
        m_average[i] = static_cast<float>(m_sum[i] / nextCount);
    }
    row.power = f.powerDb; row.sequence = f.sequence; row.time = f.timestampNs;
    m_count = nextCount; m_next = (m_next + 1) % m_rows.size();
}
void TemporalAccumulator::rollback()
{
    if (!m_pending) return;
    std::swap(m_rows[m_previousNext], m_backup);
    m_next = m_previousNext; m_count = m_previousCount; m_pending = false;
    std::fill(m_sum.begin(), m_sum.end(), 0.0);
    std::fill(m_maximum.begin(), m_maximum.end(), -std::numeric_limits<float>::infinity());
    for (std::size_t r = 0; r < m_count; ++r) {
        for (std::size_t i = 0; i < m_sum.size(); ++i) {
            m_sum[i] += m_rows[r].power[i];
            m_maximum[i] = std::max(m_maximum[i], m_rows[r].power[i]);
        }
    }
    for (std::size_t i = 0; i < m_sum.size(); ++i)
        m_average[i] = m_count ? static_cast<float>(m_sum[i] / m_count) : 0;
}
std::uint64_t TemporalAccumulator::firstSequence() const
{ return m_count ? m_rows[m_count == m_rows.size() ? m_next : 0].sequence : 0; }
std::int64_t TemporalAccumulator::firstTimestampNs() const
{ return m_count ? m_rows[m_count == m_rows.size() ? m_next : 0].time : 0; }
}
