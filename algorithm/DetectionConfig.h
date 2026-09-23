#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace scn::algorithm
{

struct AccumulatorConfig { std::size_t frames = 16; };
enum class DetectionBackend : std::uint8_t { Scn = 0, Ffscn = 1 };
struct DetectorConfig
{
    std::string modelPath = "models/scn_model.engine";
    int deviceIndex = 0;
    std::size_t inputLength = 32768;
    std::size_t windowStep = 16384;
    float confidenceThreshold = 0.1F;
    float nmsIou = 0.5F;
    std::size_t topK = 200;
    std::size_t maxCandidatesPerWindow = 150;
};
struct FfscnConfig
{
    std::string modelPath = "models/ffscn_17.engine";
    int deviceIndex = 0;
    // The model remains a 17th-order detector. Its engine accepts dynamic
    // widths from 2^13 through 2^17; shorter spectra are interpolated upward.
    std::size_t inputLength = 131072;
    std::size_t frameCount = 10;
    std::size_t windowStep = 65536;
    float confidenceThreshold = 0.7F;
    float nmsIou = 0.3F;
    std::size_t topK = 512;
    std::size_t maxCandidatesPerWindow = 512;
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
struct DetectionConfig
{
    bool enabled = true;
    DetectionBackend backend = DetectionBackend::Scn;
    std::size_t maxSignals = 4096;
    AccumulatorConfig accumulator;
    DetectorConfig detector;
    FfscnConfig ffscn;
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
