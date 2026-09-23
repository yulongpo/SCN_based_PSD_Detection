#include "DetectionConfig.h"
#include "common/Frequency.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace scn::algorithm
{
bool validateConfig(const DetectionConfig& c, std::string& error)
{
    const auto unit = [](double v) { return std::isfinite(v) && v > 0.0 && v <= 1.0; };
    const auto validPriors = [&] {
        std::vector<std::int64_t> ids;
        for (const auto& prior : c.channelAggregation.priors) {
            if (prior.id <= 0 || prior.name.empty() || prior.startFrequencyHz < 0 ||
                prior.endFrequencyHz <= prior.startFrequencyHz ||
                prior.endFrequencyHz > 6'400'000'000LL ||
                std::find(ids.begin(), ids.end(), prior.id) != ids.end()) return false;
            ids.push_back(prior.id);
        }
        return true;
    };
    error.clear();
    if (!c.maxSignals || c.maxSignals > 65536) error = "maxSignals must be in [1,65536].";
    else if (!c.accumulator.frames || c.accumulator.frames > 256) error = "Accumulator frames must be in [1,256].";
    else if (c.detector.inputLength != 32768) error = "The ISA SCN engine requires 32768 input bins.";
    else if (!c.detector.windowStep || c.detector.windowStep > c.detector.inputLength) error = "Window step must be in [1,32768].";
    else if (c.detector.deviceIndex < 0) error = "GPU index must be nonnegative.";
    else if (c.enabled && c.detector.modelPath.empty()) error = "A TensorRT engine path is required.";
    else if (!std::isfinite(c.detector.confidenceThreshold) || c.detector.confidenceThreshold < 0 || c.detector.confidenceThreshold > 1) error = "Confidence must be in [0,1].";
    else if (!unit(c.detector.nmsIou)) error = "NMS IoU must be in (0,1].";
    else if (!c.detector.topK || c.detector.topK > 8192 || !c.detector.maxCandidatesPerWindow || c.detector.maxCandidatesPerWindow > c.detector.topK) error = "Invalid TopK or per-window candidate limit.";
    else if (!std::isfinite(c.refine.cnrThresholdDb)) error = "CNR threshold must be finite.";
    else if (!unit(c.fusion.iou) || !unit(c.fusion.overlapRatio) || c.fusion.gapHz < 0) error = "Invalid fusion thresholds.";
    else if (!std::isfinite(c.channelAggregation.highThresholdDb) ||
             !std::isfinite(c.channelAggregation.lowThresholdDb) ||
             c.channelAggregation.highThresholdDb <= c.channelAggregation.lowThresholdDb ||
             c.channelAggregation.highThresholdDb > 100.0 ||
             c.channelAggregation.lowThresholdDb < -100.0 ||
             !unit(c.channelAggregation.minimumSupportRatio) ||
             !unit(c.channelAggregation.minimumCoverageRatio) ||
             c.channelAggregation.maximumAutomaticBandwidthHz <= 0 ||
             c.channelAggregation.maximumAutomaticBandwidthHz > 6'400'000'000LL ||
             c.channelAggregation.mergeConfirmationCount < 2 ||
             c.channelAggregation.mergeConfirmationCount > 20 ||
             c.channelAggregation.splitConfirmationCount < 2 ||
             c.channelAggregation.splitConfirmationCount > 40 ||
             c.channelAggregation.missingConfirmationCount == 0 ||
             c.channelAggregation.missingConfirmationCount > 20 ||
             !std::isfinite(c.channelAggregation.missingHoldSeconds) ||
             c.channelAggregation.missingHoldSeconds < 0.0 ||
             c.channelAggregation.missingHoldSeconds > 3600.0 ||
             !std::isfinite(c.channelAggregation.historySeconds) ||
             c.channelAggregation.historySeconds <= 0.0 ||
             c.channelAggregation.historySeconds > 3600.0) error = "Invalid channel aggregation settings.";
    else if (!validPriors())
        error = "Invalid channel prior.";
    else if (!unit(c.tracker.overlapRatio) || !std::isfinite(c.tracker.maxMissSeconds) || c.tracker.maxMissSeconds < 0 || c.tracker.maxMissSeconds > 3600 ||
             !std::isfinite(c.tracker.maxBandwidthRatio) || c.tracker.maxBandwidthRatio < 1.0 || c.tracker.maxBandwidthRatio > 100.0 ||
             !std::isfinite(c.tracker.centerDistanceRatio) || c.tracker.centerDistanceRatio <= 0.0 || c.tracker.centerDistanceRatio > 10.0 ||
             c.tracker.medianWindow == 0 || c.tracker.medianWindow > 31 ||
             !std::isfinite(c.tracker.smoothingAlpha) || c.tracker.smoothingAlpha <= 0.0 || c.tracker.smoothingAlpha > 1.0 ||
             c.tracker.jumpConfirmationCount < 2 || c.tracker.jumpConfirmationCount > 20 ||
             !std::isfinite(c.tracker.jumpEdgeChangeRatio) || c.tracker.jumpEdgeChangeRatio <= 0.0 || c.tracker.jumpEdgeChangeRatio > 10.0 ||
             !std::isfinite(c.tracker.jumpCenterToleranceRatio) || c.tracker.jumpCenterToleranceRatio <= 0.0 || c.tracker.jumpCenterToleranceRatio > 10.0 ||
             !std::isfinite(c.tracker.jumpBandwidthToleranceRatio) || c.tracker.jumpBandwidthToleranceRatio < 1.0 || c.tracker.jumpBandwidthToleranceRatio > 10.0) error = "Invalid tracker thresholds.";
    return error.empty();
}

bool sameConfig(const DetectionConfig& a, const DetectionConfig& b)
{
    const auto values = [](const DetectionConfig& c) {
        return std::tie(c.enabled, c.maxSignals, c.accumulator.frames, c.detector.modelPath,
            c.detector.deviceIndex, c.detector.inputLength, c.detector.windowStep, c.detector.confidenceThreshold,
            c.detector.nmsIou, c.detector.topK, c.detector.maxCandidatesPerWindow, c.refine.cnrThresholdDb,
            c.fusion.iou, c.fusion.overlapRatio, c.fusion.gapHz, c.tracker.overlapRatio, c.tracker.maxMissSeconds,
            c.tracker.boundaryStabilityEnabled, c.tracker.maxBandwidthRatio, c.tracker.centerDistanceRatio,
            c.tracker.medianWindow, c.tracker.smoothingAlpha, c.tracker.jumpConfirmationCount,
            c.tracker.jumpEdgeChangeRatio, c.tracker.jumpCenterToleranceRatio,
            c.tracker.jumpBandwidthToleranceRatio,
            c.channelAggregation.enabled, c.channelAggregation.highThresholdDb,
            c.channelAggregation.lowThresholdDb, c.channelAggregation.minimumSupportRatio,
            c.channelAggregation.minimumCoverageRatio,
            c.channelAggregation.maximumAutomaticBandwidthHz,
            c.channelAggregation.mergeConfirmationCount,
            c.channelAggregation.splitConfirmationCount,
            c.channelAggregation.missingConfirmationCount,
            c.channelAggregation.missingHoldSeconds, c.channelAggregation.historySeconds);
    };
    if (values(a) != values(b) || a.channelAggregation.priors.size() != b.channelAggregation.priors.size())
        return false;
    for (std::size_t i = 0; i < a.channelAggregation.priors.size(); ++i) {
        const auto& left = a.channelAggregation.priors[i];
        const auto& right = b.channelAggregation.priors[i];
        if (left.id != right.id || left.name != right.name || left.enabled != right.enabled ||
            left.startFrequencyHz != right.startFrequencyHz || left.endFrequencyHz != right.endFrequencyHz)
            return false;
    }
    return true;
}
ConfigApplyResult classifyConfigChange(const DetectionConfig& a, const DetectionConfig& b)
{
    std::string error;
    if (!validateConfig(b, error)) return ConfigApplyResult::Invalid;
    if (a.detector.modelPath != b.detector.modelPath || a.detector.deviceIndex != b.detector.deviceIndex || a.enabled != b.enabled)
        return ConfigApplyResult::RequiresRestart;
    // Every other detection setting can change candidate topology, grouping,
    // tracking identities, or policy measurements. Start a fresh algorithm
    // segment instead of retaining state produced under a different config.
    return sameConfig(a, b) ? ConfigApplyResult::Applied : ConfigApplyResult::RequiresReset;
}
}
