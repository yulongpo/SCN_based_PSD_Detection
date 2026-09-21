#pragma once

#include "../ISpectrumSource.h"

#include <cstdint>
#include <vector>

namespace scn::source
{

class HarogicSource final : public ISpectrumSource
{
public:
    HarogicSource();
    ~HarogicSource() override;

    algorithm::SourceKind kind() const noexcept override;
    std::string name() const override;
    bool open(const SourceConfig& config, std::string& error) override;
    bool start() override;
    void pause(bool paused) override;
    void stop() override;
    bool read(algorithm::SpectrumFrame& frame) override;
    bool isLive() const noexcept override;

private:
    bool configureDevice(const SourceConfig& config, std::string& error);
    void closeDevice();

    SourceConfig m_config{};
    void* m_device = nullptr;
    bool m_open = false;
    bool m_running = false;
    bool m_paused = false;
    std::uint64_t m_sequence = 0;

    int m_totalHops = 0;
    int m_partialTraceLength = 0;
    std::size_t m_traceLength = 0;
    double m_startFrequencyHz = 0.0;
    double m_binWidthHz = 0.0;
    double m_actualRbwHz = 0.0;

    std::vector<double> m_frequency;
    std::vector<float> m_power;
    std::vector<double> m_partialFrequency;
    std::vector<float> m_partialPower;
};

} // namespace scn::source
