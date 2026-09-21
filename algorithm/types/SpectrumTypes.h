#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

namespace scn::algorithm
{

enum class SourceKind
{
    BB60C = 0,
    Harogic = 1,
    File = 2
};

struct SpectrumFrame
{
    std::uint64_t sequence = 0;
    std::int64_t timestampNs = 0;
    double startFrequencyHz = 0.0;
    double binWidthHz = 1.0;
    double resolutionBandwidthHz = 0.0;
    double referenceLevelDbm = 0.0;
    std::vector<float> powerDb;
    std::string sourceName;

    [[nodiscard]] bool isValid() const noexcept
    {
        if (powerDb.empty() || !std::isfinite(startFrequencyHz) ||
            !std::isfinite(binWidthHz) || binWidthHz <= 0.0) {
            return false;
        }
        const double endFrequency = endFrequencyHz();
        return std::isfinite(endFrequency) && endFrequency > startFrequencyHz;
    }

    [[nodiscard]] double endFrequencyHz() const noexcept
    {
        return startFrequencyHz + binWidthHz * static_cast<double>(powerDb.size());
    }
};

} // namespace scn::algorithm
