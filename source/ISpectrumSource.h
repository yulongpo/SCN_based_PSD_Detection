#pragma once

#include "SourceConfig.h"
#include "../algorithm/types/SpectrumTypes.h"

#include <string>

namespace scn::source
{

class ISpectrumSource
{
public:
    virtual ~ISpectrumSource() = default;

    virtual algorithm::SourceKind kind() const noexcept = 0;
    virtual std::string name() const = 0;
    virtual bool open(const SourceConfig& config, std::string& error) = 0;
    virtual bool start() = 0;
    virtual void pause(bool paused) = 0;
    virtual void stop() = 0;
    virtual bool read(algorithm::SpectrumFrame& frame) = 0;
    virtual bool isLive() const noexcept = 0;
};

} // namespace scn::source
