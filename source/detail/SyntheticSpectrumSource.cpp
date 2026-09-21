#include "SyntheticSpectrumSource.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace scn::source::detail
{

SyntheticSpectrumSource::SyntheticSpectrumSource(
    algorithm::SourceKind kind,
    std::string sourceName)
    : m_kind(kind), m_name(std::move(sourceName))
{
}

bool SyntheticSpectrumSource::open(const SourceConfig& config, std::string& error)
{
    if (config.pointCount < 16 || config.bandwidthHz <= 0.0 || config.frameRateHz <= 0) {
        error = "Invalid spectrum source configuration.";
        return false;
    }
    m_config = config;
    m_sequence = 0;
    m_paused = false;
    return true;
}

bool SyntheticSpectrumSource::start()
{
    m_running = true;
    return true;
}

bool SyntheticSpectrumSource::read(algorithm::SpectrumFrame& frame)
{
    if (!m_running || m_paused) {
        return false;
    }

    frame = {};
    frame.sequence = ++m_sequence;
    frame.timestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame.startFrequencyHz = m_config.centerFrequencyHz - m_config.bandwidthHz / 2.0;
    frame.binWidthHz = m_config.bandwidthHz / static_cast<double>(m_config.pointCount);
    frame.sourceName = m_name;
    frame.powerDb.resize(m_config.pointCount);

    const double phase = static_cast<double>(frame.sequence) * 0.07;
    for (std::size_t i = 0; i < frame.powerDb.size(); ++i) {
        const double x = static_cast<double>(i) / static_cast<double>(frame.powerDb.size());
        const double noise = 2.0 * std::sin(phase + x * 31.0);
        frame.powerDb[i] = static_cast<float>(-92.0 + noise);
    }
    addProfilePeaks(frame.powerDb);
    return true;
}

} // namespace scn::source::detail
