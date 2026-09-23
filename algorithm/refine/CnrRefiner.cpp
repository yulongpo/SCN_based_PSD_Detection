#include "CnrRefiner.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>

namespace scn::algorithm
{
std::vector<double> makePowerPrefix(const std::vector<float>& spectrum)
{
    std::vector<double> prefix(spectrum.size() + 1, 0.0);
    for (std::size_t i = 0; i < spectrum.size(); ++i)
        prefix[i + 1] = prefix[i] + static_cast<double>(spectrum[i]);
    return prefix;
}

DetectedSignal remeasureBand(const std::vector<double>& prefix,
                             const SpectrumFrame& frame,
                             const DetectedSignal& band,
                             SpectrumBranch branch)
{
    auto measured = band;
    measured.branch = branch;
    if (frame.powerDb.empty() || prefix.size() != frame.powerDb.size() + 1 ||
        !(frame.binWidthHz > 0.0) || !(band.endFrequencyHz > band.startFrequencyHz)) return measured;
    const auto count = frame.powerDb.size();
    auto first = static_cast<std::size_t>(std::clamp(
        std::floor((band.startFrequencyHz - frame.startFrequencyHz) / frame.binWidthHz + 1e-10),
        0.0, static_cast<double>(count)));
    auto last = static_cast<std::size_t>(std::clamp(
        std::ceil((band.endFrequencyHz - frame.startFrequencyHz) / frame.binWidthHz - 1e-10),
        0.0, static_cast<double>(count)));
    if (last <= first) {
        if (first >= count) first = count - 1;
        last = first + 1;
    }
    const auto width = last - first;
    const auto trim = static_cast<std::size_t>(std::round(width * 0.2));
    const auto signalFirst = std::min(last - 1, first + trim);
    const auto signalLast = std::max(signalFirst + 1, last - trim);
    const auto mean = [&prefix](std::size_t begin, std::size_t end) {
        return end > begin ? (prefix[end] - prefix[begin]) / static_cast<double>(end - begin) : 0.0;
    };
    const double signalLevel = mean(signalFirst, signalLast);
    const auto noiseWidth = std::max<std::size_t>(1, width / 10);
    const auto left = first - std::min(first, noiseWidth);
    const auto right = last + std::min(count - last, noiseWidth);
    const auto noiseCount = (first - left) + (right - last);
    const double noiseLevel = noiseCount
        ? ((prefix[first] - prefix[left]) + (prefix[right] - prefix[last])) / static_cast<double>(noiseCount)
        : signalLevel;
    const double cnr = noiseCount ? signalLevel - noiseLevel : 0.0;
    if (!std::isfinite(signalLevel) || !std::isfinite(noiseLevel) || !std::isfinite(cnr)) return measured;
    measured.signalLevelDbm = static_cast<float>(signalLevel);
    measured.noiseLevelDbm = static_cast<float>(noiseLevel);
    measured.snrDb = static_cast<float>(cnr);
    return measured;
}

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
