#include "SignalFusion.h"
#include <algorithm>
#include <tuple>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace scn::algorithm
{
std::vector<DetectedSignal> fuseSignals(std::vector<DetectedSignal> items, const FusionConfig& c)
{
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return std::tie(a.startFrequencyHz, a.endFrequencyHz) < std::tie(b.startFrequencyHz, b.endFrequencyHz);
    });
    std::vector<DetectedSignal> merged;
    for (const auto& item : items) {
        if (merged.empty()) { merged.push_back(item); continue; }
        auto& previous = merged.back();
        const double intersection = std::max(0.0, std::min(previous.endFrequencyHz, item.endFrequencyHz) - std::max(previous.startFrequencyHz, item.startFrequencyHz));
        const double w1 = previous.endFrequencyHz - previous.startFrequencyHz;
        const double w2 = item.endFrequencyHz - item.startFrequencyHz;
        const double united = w1 + w2 - intersection;
        const double gap = item.startFrequencyHz - previous.endFrequencyHz;
        const bool merge = (united > 0 && intersection / united >= c.iou) ||
            (std::min(w1, w2) > 0 && intersection / std::min(w1, w2) >= c.overlapRatio) ||
            (c.gapHz > 0 && gap >= 0 && gap <= c.gapHz);
        if (!merge) { merged.push_back(item); continue; }
        previous.startFrequencyHz = std::min(previous.startFrequencyHz, item.startFrequencyHz);
        previous.endFrequencyHz = std::max(previous.endFrequencyHz, item.endFrequencyHz);
        previous.bandwidthHz = previous.endFrequencyHz - previous.startFrequencyHz;
        previous.centerFrequencyHz = previous.startFrequencyHz + previous.bandwidthHz / 2;
        previous.confidence = std::max(previous.confidence, item.confidence);
        previous.signalLevelDbm = std::max(previous.signalLevelDbm, item.signalLevelDbm);
        previous.noiseLevelDbm = std::min(previous.noiseLevelDbm, item.noiseLevelDbm);
        const double cnr = static_cast<double>(previous.signalLevelDbm) - previous.noiseLevelDbm;
        if (!std::isfinite(cnr) || std::abs(cnr) > std::numeric_limits<float>::max())
            throw std::overflow_error("Fused CNR exceeds the finite float range.");
        previous.snrDb = static_cast<float>(cnr);
        previous.branch = static_cast<SpectrumBranch>(static_cast<unsigned>(previous.branch) | static_cast<unsigned>(item.branch));
    }
    return merged;
}
}
