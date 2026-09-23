#pragma once

#include <cstddef>
#include <string>

namespace scn::algorithm
{

struct AccumulatorConfig { std::size_t frames = 16; };
struct DetectorConfig
{
    std::string modelPath = "models/scn_model.engine";
    int deviceIndex = 0;
    std::size_t inputLength = 32768;
    std::size_t windowStep = 16384;
    float confidenceThreshold = 0.4F;
    float nmsIou = 0.5F;
    std::size_t topK = 200;
    std::size_t maxCandidatesPerWindow = 150;
};
struct RefineConfig { float cnrThresholdDb = 3.0F; };
struct FusionConfig
{
    double iou = 0.1;
    double overlapRatio = 0.5;
    double gapHz = 0.0;
};
struct TrackerConfig
{
    double overlapRatio = 0.45;
    double maxMissSeconds = 1.0;
    bool boundaryStabilityEnabled = true;
    double maxBandwidthRatio = 2.0;
    double centerDistanceRatio = 0.25;
    std::size_t medianWindow = 5;
    double smoothingAlpha = 0.35;
    std::size_t jumpConfirmationCount = 3;
};
struct DetectionConfig
{
    bool enabled = true;
    std::size_t maxSignals = 4096;
    AccumulatorConfig accumulator;
    DetectorConfig detector;
    RefineConfig refine;
    FusionConfig fusion;
    TrackerConfig tracker;
};

enum class ConfigApplyResult
{
    Applied,
    RequiresReset,
    RequiresRestart,
    Invalid
};

bool validateConfig(const DetectionConfig& config, std::string& error);
bool sameConfig(const DetectionConfig& a, const DetectionConfig& b);
ConfigApplyResult classifyConfigChange(const DetectionConfig& current,
                                      const DetectionConfig& next);

} // namespace scn::algorithm
