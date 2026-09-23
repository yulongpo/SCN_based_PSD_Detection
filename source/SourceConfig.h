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
    std::int64_t centerFrequencyHz = 2400000000LL;
    std::int64_t bandwidthHz = 100000000LL;
    std::int64_t resolutionBandwidthHz = 50000LL;
    double referenceLevelDbm = -25.0;
    RbwShape rbwShape = RbwShape::Nuttall;
    std::size_t pointCount = 4096;
    int frameRateHz = 30;
    bool loopFile = true;
};

} // namespace scn::source
