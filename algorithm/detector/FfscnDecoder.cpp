#include "FfscnDecoder.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scn::algorithm
{
std::vector<FfscnCandidate> suppressFfscnCandidates(std::vector<FfscnCandidate> candidates,
                                                     float iouThreshold,
                                                     std::size_t capacity)
{
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.confidence != b.confidence ? a.confidence > b.confidence : a.peakIndex < b.peakIndex;
    });
    std::vector<FfscnCandidate> kept;
    kept.reserve((std::min)(capacity, candidates.size()));
    for (const auto& candidate : candidates) {
        if (!std::isfinite(candidate.beginBin) || !std::isfinite(candidate.endBin) ||
            !std::isfinite(candidate.confidence) || candidate.endBin <= candidate.beginBin) continue;
        bool suppressed = false;
        for (const auto& accepted : kept) {
            const double intersection = (std::max)(0.0,
                (std::min)(candidate.endBin, accepted.endBin) - (std::max)(candidate.beginBin, accepted.beginBin));
            const double unionWidth = (candidate.endBin - candidate.beginBin) +
                (accepted.endBin - accepted.beginBin) - intersection;
            // Match FFSCN's reference NMS: retain when IoU <= threshold.
            if (unionWidth > 0.0 && intersection / unionWidth > iouThreshold) {
                suppressed = true;
                break;
            }
        }
        if (!suppressed) {
            kept.push_back(candidate);
            if (kept.size() >= capacity) break;
        }
    }
    return kept;
}

std::vector<FfscnCandidate> decodeFfscn(const FfscnModelOutput& output,
                                        const FfscnConfig& config)
{
    constexpr std::size_t stride = 4;
    if (output.inputLength < (std::size_t{1} << 13) || output.inputLength > (std::size_t{1} << 17) ||
        output.inputLength % stride != 0)
        throw std::runtime_error("FFSCN input width must be a power of two in [8192,131072].");
    const auto n = output.inputLength / stride;
    if (output.heatmap.size() != n || output.bandwidth.size() != n || output.offset.size() != n)
        throw std::runtime_error("FFSCN hm/bw/off must be equal stride-4 output vectors.");
    std::vector<FfscnCandidate> candidates;
    candidates.reserve((std::min)(config.topK, n));
    for (std::size_t i = 0; i < n; ++i) {
        const float score = output.heatmap[i];
        if (!std::isfinite(score) || !(score > config.confidenceThreshold)) continue;
        const auto first = i > 3 ? i - 3 : 0;
        const auto last = (std::min)(n - 1, i + 3);
        bool localMaximum = true;
        for (auto j = first; j <= last; ++j) {
            if (output.heatmap[j] > score) { localMaximum = false; break; }
        }
        if (!localMaximum || !std::isfinite(output.bandwidth[i]) ||
            !std::isfinite(output.offset[i]) || output.bandwidth[i] <= 0.0F) continue;
        const double center = static_cast<double>(i) + output.offset[i];
        const double half = static_cast<double>(output.bandwidth[i]) / 2.0;
        const double begin = std::clamp((center - half) * stride, 0.0,
                                        static_cast<double>(output.inputLength - 1));
        const double end = std::clamp((center + half) * stride, 0.0,
                                      static_cast<double>(output.inputLength - 1));
        candidates.push_back({begin, end, score, i});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.confidence != b.confidence ? a.confidence > b.confidence : a.peakIndex < b.peakIndex;
    });
    if (candidates.size() > config.topK) candidates.resize(config.topK);
    return suppressFfscnCandidates(std::move(candidates), config.nmsIou,
                                   config.maxCandidatesPerWindow);
}
} // namespace scn::algorithm
