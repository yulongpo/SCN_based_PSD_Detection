#pragma once

#include "../ISpectrumSource.h"

#include <cstdint>
#include <vector>

namespace scn::source
{

class BB60CSource final : public ISpectrumSource
{
public:
    BB60CSource();
    ~BB60CSource() override;

    algorithm::SourceKind kind() const noexcept override;
    std::string name() const override;
    bool open(const SourceConfig& config, std::string& error) override;
    bool start() override;
    void pause(bool paused) override;
    void stop() override;
    bool read(algorithm::SpectrumFrame& frame) override;
    bool isLive() const noexcept override;

private:
    void closeDevice();

    SourceConfig m_config{};
    int m_device = -1;
    bool m_open = false;
    bool m_running = false;
    bool m_paused = false;
    bool m_measurementInitiated = false;
    std::uint64_t m_sequence = 0;
    std::size_t m_traceLength = 0;
    double m_binWidthHz = 0.0;
    double m_startFrequencyHz = 0.0;
    std::vector<float> m_minTrace;
    std::vector<float> m_maxTrace;
};

} // namespace scn::source
