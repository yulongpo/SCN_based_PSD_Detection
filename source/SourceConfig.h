#pragma once

#include "../algorithm/types/SpectrumTypes.h"

#include <cstddef>
#include <string>

namespace scn::source
{

struct SourceConfig
{
    algorithm::SourceKind kind = algorithm::SourceKind::BB60C;
    std::string filePath;
    double centerFrequencyHz = 2.4e9;
    double bandwidthHz = 100.0e6;
    double resolutionBandwidthHz = 50.0e3;
    double referenceLevelDbm = -25.0;
    std::size_t pointCount = 4096;
    int frameRateHz = 30;
    bool loopFile = true;
};

} // namespace scn::source
