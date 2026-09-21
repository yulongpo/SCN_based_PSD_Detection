#include "SpectrumPreprocessor.h"
#include <algorithm>
#include <stdexcept>

namespace scn::algorithm
{
bool validateFrame(const SpectrumFrame& f, std::string& error)
{
    error.clear();
    if (!f.isValid() || f.startFrequencyHz + f.binWidthHz <= f.startFrequencyHz ||
        !std::isfinite(f.referenceLevelDbm) || !std::isfinite(f.resolutionBandwidthHz))
        error = "Invalid spectrum geometry or metadata.";
    else if (!std::all_of(f.powerDb.begin(), f.powerDb.end(), [](float p) { return std::isfinite(p); }))
        error = "Spectrum contains NaN/Inf; frame rejected before accumulation.";
    return error.empty();
}
bool sameGeometry(const SpectrumFrame& a, const SpectrumFrame& b)
{
    return a.powerDb.size() == b.powerDb.size() && a.startFrequencyHz == b.startFrequencyHz &&
        a.binWidthHz == b.binWidthHz && a.referenceLevelDbm == b.referenceLevelDbm &&
        a.resolutionBandwidthHz == b.resolutionBandwidthHz && a.sourceName == b.sourceName;
}
std::vector<SpectrumWindow> makeWindows(std::size_t count, std::size_t length, std::size_t step)
{
    if (!length || !step || step > length) throw std::invalid_argument("Invalid SCN window geometry.");
    std::vector<SpectrumWindow> windows;
    for (std::size_t start = 0; start < count;) {
        const auto valid = std::min(length, count - start);
        windows.push_back({start, valid});
        if (valid == count - start) break;
        start += step;
    }
    return windows;
}
void normalizeWindow(const std::vector<float>& spectrum, SpectrumWindow window,
                     std::size_t inputLength, std::vector<float>& normalized)
{
    if (!window.length || window.length > inputLength || window.start > spectrum.size() ||
        window.length > spectrum.size() - window.start) throw std::invalid_argument("Invalid spectrum window.");
    const auto first = spectrum.begin() + static_cast<std::ptrdiff_t>(window.start);
    const auto bounds = std::minmax_element(first, first + static_cast<std::ptrdiff_t>(window.length));
    const double minimum = *bounds.first;
    const double range = static_cast<double>(*bounds.second) - minimum;
    normalized.assign(inputLength, 0.0F); // Min-value padding becomes zero after normalization.
    if (range <= 0) return;
    for (std::size_t i = 0; i < window.length; ++i)
        normalized[i] = static_cast<float>((spectrum[window.start + i] - minimum) / range);
}
}
