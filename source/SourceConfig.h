#pragma once

#include "../algorithm/types/SpectrumTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace scn::source
{

enum class RbwShape : std::int32_t
{
    Nuttall = 0,
    Flattop = 1,
    Cispr = 2,
};

struct SourceConfig
{
    algorithm::SourceKind kind = algorithm::SourceKind::BB60C;
    std::string filePath;
    double centerFrequencyHz = 2.4e9;
    double bandwidthHz = 100.0e6;
    double resolutionBandwidthHz = 50.0e3;
    double referenceLevelDbm = -25.0;
    RbwShape rbwShape = RbwShape::Nuttall;
    std::size_t pointCount = 4096;
    int frameRateHz = 30;
    bool loopFile = true;
};

} // namespace scn::source
