#include "HarogicSource.h"

#include <algorithm>
#include <cmath>

namespace scn::source
{

HarogicSource::HarogicSource()
    : SyntheticSpectrumSource(algorithm::SourceKind::Harogic, "Harogic")
{
}

void HarogicSource::addProfilePeaks(std::vector<float>& values) const
{
    const auto addPeak = [&values](double center, double width, double level) {
        for (std::size_t i = 0; i < values.size(); ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(values.size());
            const double distance = (x - center) / width;
            values[i] = std::max(values[i], static_cast<float>(level * std::exp(-distance * distance)) - 92.0F);
        }
    };
    addPeak(0.23, 0.012, 74.0);
    addPeak(0.51, 0.022, 60.0);
    addPeak(0.83, 0.010, 70.0);
}

} // namespace scn::source
