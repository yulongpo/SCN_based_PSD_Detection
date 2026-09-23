#pragma once

#include "../source/SourceFactory.h"

#include <memory>
#include <string>

namespace scn::application
{

class SourceManager
{
public:
    bool configure(const source::SourceConfig& config, std::string& error);
    bool start();
    void pause(bool paused);
    void stop();
    bool read(algorithm::SpectrumFrame& frame);
    bool atFileEnd() const;
    bool hasOpenLiveSource(algorithm::SourceKind kind) const noexcept;

    [[nodiscard]] algorithm::SourceKind kind() const noexcept { return m_config.kind; }
    [[nodiscard]] const source::SourceConfig& config() const noexcept { return m_config; }

private:
    source::SourceConfig m_config{};
    std::unique_ptr<source::ISpectrumSource> m_source;
};

} // namespace scn::application
