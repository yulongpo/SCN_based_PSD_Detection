#include "DetectionConfig.h"
#include "common/Frequency.h"
#include <cmath>
#include <tuple>

namespace scn::algorithm
{
bool validateConfig(const DetectionConfig& c, std::string& error)
{
    const auto unit = [](double v) { return std::isfinite(v) && v > 0.0 && v <= 1.0; };
    error.clear();
    if (c.backend != DetectionBackend::Scn && c.backend != DetectionBackend::Ffscn) error = "Unknown detection backend.";
    else if (!c.maxSignals || c.maxSignals > 65536) error = "maxSignals must be in [1,65536].";
    else if (!c.accumulator.frames || c.accumulator.frames > 256) error = "Accumulator frames must be in [1,256].";
    else if (c.backend == DetectionBackend::Scn && c.detector.inputLength != 32768) error = "The ISA SCN engine requires 32768 input bins.";
    else if (c.backend == DetectionBackend::Ffscn &&
             (c.ffscn.inputLength != 131072 || c.ffscn.frameCount != 10 ||
              c.ffscn.windowStep != 65536)) error = "FFSCN requires a 17th-order engine, a 10-frame input, and a 65536-bin window step.";
    else if (!c.detector.windowStep || c.detector.windowStep > c.detector.inputLength) error = "Window step must be in [1,32768].";
    else if (c.detector.deviceIndex < 0) error = "GPU index must be nonnegative.";
    else if (c.ffscn.deviceIndex < 0) error = "FFSCN GPU index must be nonnegative.";
    else if (c.enabled && c.backend == DetectionBackend::Scn && c.detector.modelPath.empty()) error = "An SCN TensorRT engine path is required.";
    else if (c.enabled && c.backend == DetectionBackend::Ffscn && c.ffscn.modelPath.empty()) error = "An FFSCN TensorRT engine path is required.";
    else if (!std::isfinite(c.detector.confidenceThreshold) || c.detector.confidenceThreshold < 0 || c.detector.confidenceThreshold > 1) error = "Confidence must be in [0,1].";
    else if (!unit(c.detector.nmsIou)) error = "NMS IoU must be in (0,1].";
    else if (!c.detector.topK || c.detector.topK > 8192 || !c.detector.maxCandidatesPerWindow || c.detector.maxCandidatesPerWindow > c.detector.topK) error = "Invalid TopK or per-window candidate limit.";
    else if (!std::isfinite(c.ffscn.confidenceThreshold) || c.ffscn.confidenceThreshold < 0 || c.ffscn.confidenceThreshold > 1) error = "FFSCN confidence must be in [0,1].";
    else if (!unit(c.ffscn.nmsIou) || !c.ffscn.topK || c.ffscn.topK > 32768 ||
             !c.ffscn.maxCandidatesPerWindow || c.ffscn.maxCandidatesPerWindow > c.ffscn.topK) error = "Invalid FFSCN NMS, TopK or candidate limit.";
    else if (!std::isfinite(c.refine.cnrThresholdDb)) error = "CNR threshold must be finite.";
    else if (!unit(c.fusion.iou) || !unit(c.fusion.overlapRatio) || c.fusion.gapHz < 0) error = "Invalid fusion thresholds.";
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
        return std::tie(c.enabled, c.backend, c.maxSignals, c.accumulator.frames, c.detector.modelPath,
            c.detector.deviceIndex, c.detector.inputLength, c.detector.windowStep, c.detector.confidenceThreshold,
            c.detector.nmsIou, c.detector.topK, c.detector.maxCandidatesPerWindow,
            c.ffscn.modelPath, c.ffscn.deviceIndex, c.ffscn.inputLength, c.ffscn.frameCount,
            c.ffscn.windowStep, c.ffscn.confidenceThreshold, c.ffscn.nmsIou,
            c.ffscn.topK, c.ffscn.maxCandidatesPerWindow, c.refine.cnrThresholdDb,
            c.fusion.iou, c.fusion.overlapRatio, c.fusion.gapHz, c.tracker.overlapRatio, c.tracker.maxMissSeconds,
            c.tracker.boundaryStabilityEnabled, c.tracker.maxBandwidthRatio, c.tracker.centerDistanceRatio,
            c.tracker.medianWindow, c.tracker.smoothingAlpha, c.tracker.jumpConfirmationCount,
            c.tracker.jumpEdgeChangeRatio, c.tracker.jumpCenterToleranceRatio,
            c.tracker.jumpBandwidthToleranceRatio);
    };
    return values(a) == values(b);
}
ConfigApplyResult classifyConfigChange(const DetectionConfig& a, const DetectionConfig& b)
{
    std::string error;
    if (!validateConfig(b, error)) return ConfigApplyResult::Invalid;
    if (a.backend != b.backend || a.detector.modelPath != b.detector.modelPath ||
        a.detector.deviceIndex != b.detector.deviceIndex || a.ffscn.modelPath != b.ffscn.modelPath ||
        a.ffscn.deviceIndex != b.ffscn.deviceIndex || a.enabled != b.enabled)
        return ConfigApplyResult::RequiresRestart;
    if (a.ffscn.inputLength != b.ffscn.inputLength || a.ffscn.frameCount != b.ffscn.frameCount ||
        a.ffscn.windowStep != b.ffscn.windowStep) return ConfigApplyResult::RequiresReset;
    if (a.accumulator.frames != b.accumulator.frames) return ConfigApplyResult::RequiresReset;
    if (a.tracker.overlapRatio != b.tracker.overlapRatio ||
        a.tracker.maxMissSeconds != b.tracker.maxMissSeconds ||
        a.tracker.boundaryStabilityEnabled != b.tracker.boundaryStabilityEnabled ||
        a.tracker.maxBandwidthRatio != b.tracker.maxBandwidthRatio ||
        a.tracker.centerDistanceRatio != b.tracker.centerDistanceRatio ||
        a.tracker.medianWindow != b.tracker.medianWindow ||
        a.tracker.smoothingAlpha != b.tracker.smoothingAlpha ||
        a.tracker.jumpConfirmationCount != b.tracker.jumpConfirmationCount ||
        a.tracker.jumpEdgeChangeRatio != b.tracker.jumpEdgeChangeRatio ||
        a.tracker.jumpCenterToleranceRatio != b.tracker.jumpCenterToleranceRatio ||
        a.tracker.jumpBandwidthToleranceRatio != b.tracker.jumpBandwidthToleranceRatio)
        return ConfigApplyResult::RequiresReset;
    return ConfigApplyResult::Applied;
}
}
