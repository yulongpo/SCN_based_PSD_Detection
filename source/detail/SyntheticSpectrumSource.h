#pragma once

#include "../ISpectrumSource.h"

#include <cstdint>
#include <string>
#include <vector>

namespace scn::source::detail
{

class SyntheticSpectrumSource : public ISpectrumSource
{
public:
    explicit SyntheticSpectrumSource(algorithm::SourceKind kind, std::string sourceName);

    algorithm::SourceKind kind() const noexcept override { return m_kind; }
    std::string name() const override { return m_name; }
    bool open(const SourceConfig& config, std::string& error) override;
    bool start() override;
    void pause(bool paused) override { m_paused = paused; }
    void stop() override { m_running = false; }
    bool read(algorithm::SpectrumFrame& frame) override;
    bool isLive() const noexcept override { return true; }

protected:
    virtual void addProfilePeaks(std::vector<float>& values) const = 0;

    SourceConfig m_config{};
    algorithm::SourceKind m_kind;
    std::string m_name;
    std::uint64_t m_sequence = 0;
    bool m_running = false;
    bool m_paused = false;
};

} // namespace scn::source::detail
