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

enum class BoundaryState : std::uint8_t
{
    Stable,
    PendingChange,
    Ambiguous,
    Disabled
};

enum class ObservationState : std::uint8_t
{
    Observed,
    TemporarilyUnobserved
};

struct CandidateReference
{
    std::uint64_t sequence = 0;
    std::uint32_t candidateIndex = 0;
};

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

struct ChannelCandidateRecord
{
    DetectedSignal signal;
    bool passedCnr = false;
};

struct ChannelGroupingDiagnostic
{
    std::vector<std::uint32_t> candidateIndices;
    double startFrequencyHz = 0.0;
    double endFrequencyHz = 0.0;
    double occupancyCoverage = 0.0;
    double currentKnownRatio = 0.0;
    double currentOccupiedRatio = 0.0;
    std::int64_t resultingChannelId = 0;
    std::string disposition;
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
    std::size_t aggregateCount = 0;
    std::size_t pendingChannelCount = 0;
    std::size_t channelRejectedCount = 0;
    double channelAggregationTimeMs = 0.0;
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
    std::uint64_t trackingSegment = 0;
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
    std::vector<ChannelCandidateRecord> channelCandidates;
    // Stable, remeasured counterparts keyed by the same tracker ID. `detections`
    // retains the original fused frequency boundaries and measurements.
    struct TrackedDetection
    {
        DetectedSignal raw;
        DetectedSignal stable;
        BoundaryState boundaryState = BoundaryState::Stable;
        std::size_t pendingCount = 0;
        std::size_t requiredCount = 0;
        double associationIou = 0.0;
        double centerDistanceHz = 0.0;
        double bandwidthRatio = 1.0;
        SpectrumBranch measurementBranch = SpectrumBranch::Average;
        std::string diagnostic;
    };
    std::vector<TrackedDetection> trackedDetections;
    struct ChannelDetection
    {
        DetectedSignal raw;
        DetectedSignal stable;
        BoundaryState boundaryState = BoundaryState::Stable;
        ObservationState observationState = ObservationState::Observed;
        bool aggregate = false;
        std::size_t pendingMergeCount = 0;
        std::size_t pendingSplitCount = 0;
        std::size_t missingCount = 0;
        std::size_t requiredMissingCount = 0;
        double occupancyCoverage = 0.0;
        double noiseFloorDbm = 0.0;
        std::string priorName;
        std::string diagnostic;
        std::vector<CandidateReference> contributors;
        std::vector<std::int64_t> relatedChannelIds;
        bool measurementValid = true;
    };
    // Business-ready channel observations. Raw SCN and 1:1 tracking results
    // above remain available for diagnostics and A/B comparison.
    std::vector<ChannelDetection> channelDetections;
    bool channelAggregationApplied = false;
    double channelEvidenceUnitWidthHz = 0.0;
    std::size_t channelEvidenceHistoryRows = 0;
    std::vector<double> channelNoiseFloorDbm;
    std::vector<std::uint8_t> channelEvidenceKnown;
    std::vector<std::uint8_t> channelOccupancyMask;
    std::vector<ChannelGroupingDiagnostic> channelGroupingDiagnostics;
    bool trackingApplied = false;
    DetectionDiagnostics diagnostics;
};

} // namespace scn::algorithm
