#include "CnrRefiner.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>

namespace scn::algorithm
{
std::vector<DetectedSignal> refineCnr(const std::vector<float>& spectrum,
    SpectrumWindow window, const std::vector<ScnCandidate>& candidates,
    const SpectrumFrame& frame, SpectrumBranch branch, float thresholdDb)
{
    std::vector<DetectedSignal> result;
    const auto mean = [&](std::size_t first, std::size_t last) {
        return std::accumulate(spectrum.begin() + static_cast<std::ptrdiff_t>(window.start + first),
                               spectrum.begin() + static_cast<std::ptrdiff_t>(window.start + last), 0.0);
    };
    for (const auto& c : candidates) {
        // Clamp before integer conversion: predictions can exceed integer limits.
        const auto begin = static_cast<std::size_t>(std::clamp(c.beginBin, 0.0, static_cast<double>(window.length)));
        const auto end = static_cast<std::size_t>(std::clamp(c.endBin, 0.0, static_cast<double>(window.length)));
        if (end <= begin) continue;
        const auto width = end - begin;
        const auto trim = static_cast<std::size_t>(std::round(width / 5.0));
        const auto signalBegin = std::min(end - 1, begin + trim);
        const auto signalEnd = std::max(signalBegin + 1, end - trim);
        const double signal = mean(signalBegin, signalEnd) / (signalEnd - signalBegin);
        const auto noiseWidth = std::max<std::size_t>(1, width / 10);
        const auto left = begin - std::min(begin, noiseWidth);
        const auto right = end + std::min(window.length - end, noiseWidth);
        const auto noiseCount = begin - left + right - end;
        const double noise = noiseCount ? (mean(left, begin) + mean(end, right)) / noiseCount : signal;
        const double cnr = signal - noise;
        if (!std::isfinite(cnr) || std::abs(cnr) > std::numeric_limits<float>::max()) continue;
        if (thresholdDb > 0 && cnr < thresholdDb) continue;
        DetectedSignal detection;
        detection.startFrequencyHz = frame.startFrequencyHz + (window.start + begin) * frame.binWidthHz;
        detection.endFrequencyHz = frame.startFrequencyHz + (window.start + end) * frame.binWidthHz;
        detection.bandwidthHz = detection.endFrequencyHz - detection.startFrequencyHz;
        if (!std::isfinite(detection.startFrequencyHz) || !std::isfinite(detection.endFrequencyHz) ||
            !std::isfinite(detection.bandwidthHz) || detection.bandwidthHz <= 0) continue;
        detection.centerFrequencyHz = detection.startFrequencyHz + detection.bandwidthHz / 2;
        detection.confidence = c.confidence;
        detection.signalLevelDbm = static_cast<float>(signal);
        detection.noiseLevelDbm = static_cast<float>(noise);
        detection.snrDb = static_cast<float>(cnr);
        detection.branch = branch;
        result.push_back(detection);
    }
    return result;
}
}
