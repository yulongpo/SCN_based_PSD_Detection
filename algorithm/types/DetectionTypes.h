#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace scn::algorithm
{

enum class DetectionStage
{
    Bypassed,
    Accumulating,
    Completed,
    Error,
    Cancelled
};

enum class SpectrumBranch : std::uint8_t { Average = 1, Maximum = 2, Both = 3 };

struct DetectedSignal
{
    std::int64_t id = 0;
    double startFrequencyHz = 0.0;
    double endFrequencyHz = 0.0;
    double centerFrequencyHz = 0.0;
    double bandwidthHz = 0.0;
    float confidence = 0.0F;
    float snrDb = 0.0F;
    float signalLevelDbm = 0.0F;
    float noiseLevelDbm = 0.0F;
    SpectrumBranch branch = SpectrumBranch::Average;
    std::int64_t firstSeenNs = 0;
    std::int64_t lastSeenNs = 0;
    std::uint64_t occurrenceCount = 0;
};

struct DetectionDiagnostics
{
    double processingTimeMs = 0.0;
    double accumulationTimeMs = 0.0;
    double inferenceTimeMs = 0.0;
    double postprocessTimeMs = 0.0;
    std::size_t windowCount = 0;
    std::size_t candidateCount = 0;
    std::size_t cnrAcceptedCount = 0;
    std::size_t truncatedCount = 0;
    std::size_t queueDepth = 0;
    std::uint64_t completedCount = 0;
    double processingP50Ms = 0.0;
    double processingP95Ms = 0.0;
    double throughputHz = 0.0;
    double resultLatencyMs = 0.0;
    std::string message;
    std::string modelInfo;
    // Independent of stage: a failed diagnostic export does not invalidate detections.
    bool exportFailed = false;
    std::string diagnosticError;
};

struct DetectionResult
{
    std::uint64_t sequence = 0;
    std::uint64_t generation = 0;
    std::uint64_t configVersion = 0;
    std::uint64_t firstSequence = 0;
    std::int64_t firstTimestampNs = 0;
    std::int64_t timestampNs = 0;
    std::size_t accumulatedFrames = 0;
    std::size_t requiredFrames = 16;
    double startFrequencyHz = 0.0;
    double binWidthHz = 0.0;
    std::size_t pointCount = 0;
    double referenceLevelDbm = 0.0;
    double resolutionBandwidthHz = 0.0;
    std::string sourceName;
    DetectionStage stage = DetectionStage::Bypassed;
    // Do not name this member `signals`: Qt defines that token as a keyword macro.
    std::vector<DetectedSignal> detections;
    DetectionDiagnostics diagnostics;
};

} // namespace scn::algorithm
