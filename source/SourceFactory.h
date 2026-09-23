#pragma once

#include "ISpectrumSource.h"

#include <memory>

namespace scn::source
{

struct LiveSourcePresence
{
    bool bb60c = false;
    bool harogic = false;
};

std::unique_ptr<ISpectrumSource> createSource(algorithm::SourceKind kind);
LiveSourcePresence probeLiveSourcePresence() noexcept;

} // namespace scn::source
