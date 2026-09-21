#pragma once

#include <cstdint>
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
        return !powerDb.empty() && binWidthHz > 0.0;
    }

    [[nodiscard]] double endFrequencyHz() const noexcept
    {
        return startFrequencyHz + binWidthHz * static_cast<double>(powerDb.size());
    }
};

} // namespace scn::algorithm
