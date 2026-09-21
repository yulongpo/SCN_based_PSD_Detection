#include "DetectionConfig.h"
#include <cmath>
#include <tuple>

namespace scn::algorithm
{
bool validateConfig(const DetectionConfig& c, std::string& error)
{
    const auto unit = [](double v) { return std::isfinite(v) && v > 0.0 && v <= 1.0; };
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
    else if (!unit(c.fusion.iou) || !unit(c.fusion.overlapRatio) || !std::isfinite(c.fusion.gapHz) || c.fusion.gapHz < 0) error = "Invalid fusion thresholds.";
    else if (!unit(c.tracker.overlapRatio) || !std::isfinite(c.tracker.maxMissSeconds) || c.tracker.maxMissSeconds < 0 || c.tracker.maxMissSeconds > 3600) error = "Invalid tracker thresholds.";
    return error.empty();
}

bool sameConfig(const DetectionConfig& a, const DetectionConfig& b)
{
    const auto values = [](const DetectionConfig& c) {
        return std::tie(c.enabled, c.maxSignals, c.accumulator.frames, c.detector.modelPath,
            c.detector.deviceIndex, c.detector.inputLength, c.detector.windowStep, c.detector.confidenceThreshold,
            c.detector.nmsIou, c.detector.topK, c.detector.maxCandidatesPerWindow, c.refine.cnrThresholdDb,
            c.fusion.iou, c.fusion.overlapRatio, c.fusion.gapHz, c.tracker.overlapRatio, c.tracker.maxMissSeconds);
    };
    return values(a) == values(b);
}
ConfigApplyResult classifyConfigChange(const DetectionConfig& a, const DetectionConfig& b)
{
    std::string error;
    if (!validateConfig(b, error)) return ConfigApplyResult::Invalid;
    if (a.detector.modelPath != b.detector.modelPath || a.detector.deviceIndex != b.detector.deviceIndex || a.enabled != b.enabled)
        return ConfigApplyResult::RequiresRestart;
    if (a.accumulator.frames != b.accumulator.frames) return ConfigApplyResult::RequiresReset;
    return ConfigApplyResult::Applied;
}
}
