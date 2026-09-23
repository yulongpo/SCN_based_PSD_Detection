#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
    std::int64_t gapHz = 0;
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
    double jumpEdgeChangeRatio = 0.15;
    double jumpCenterToleranceRatio = 0.10;
    double jumpBandwidthToleranceRatio = 1.20;
};

struct ChannelPrior
{
    std::int64_t id = 0;
    std::string name;
    bool enabled = true;
    std::int64_t startFrequencyHz = 0;
    std::int64_t endFrequencyHz = 0;
};

struct ChannelAggregationConfig
{
    bool enabled = true;
    double highThresholdDb = 6.0;
    double lowThresholdDb = 3.0;
    double minimumSupportRatio = 0.20;
    double minimumCoverageRatio = 0.70;
    std::int64_t maximumAutomaticBandwidthHz = 200'000'000;
    std::uint32_t mergeConfirmationCount = 3;
    std::uint32_t splitConfirmationCount = 5;
    std::uint32_t missingConfirmationCount = 3;
    double missingHoldSeconds = 0.5;
    double historySeconds = 2.0;
    std::vector<ChannelPrior> priors;
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
    ChannelAggregationConfig channelAggregation;
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
