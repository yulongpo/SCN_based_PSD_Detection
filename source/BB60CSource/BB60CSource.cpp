#include "BB60CSource.h"

#include <algorithm>
#include <cmath>

namespace scn::source
{

BB60CSource::BB60CSource()
    : SyntheticSpectrumSource(algorithm::SourceKind::BB60C, "BB60C")
{
}

void BB60CSource::addProfilePeaks(std::vector<float>& values) const
{
    const auto addPeak = [&values](double center, double width, double level) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(values.size());
            const double distance = (x - center) / width;
            values[i] = std::max(values[i], static_cast<float>(level * std::exp(-distance * distance)) - 92.0F);
        }
    };
    addPeak(0.34, 0.018, 66.0);
    addPeak(0.68, 0.030, 52.0);
}

} // namespace scn::source
