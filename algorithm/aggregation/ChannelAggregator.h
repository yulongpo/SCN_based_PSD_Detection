#pragma once

#include "../DetectionConfig.h"
#include "../types/DetectionTypes.h"
#include "../types/SpectrumTypes.h"

#include <deque>
#include <functional>
#include <vector>

namespace scn::algorithm
{

struct ChannelCandidate
{
    DetectedSignal signal;
    bool passedCnr = false;
};

struct ChannelEvidence
{
    double unitWidthHz = 0.0;
    std::vector<double> noiseFloorDbm;
    std::vector<std::uint8_t> known;
    std::vector<std::uint8_t> occupied;
    std::size_t historyRows = 0;
    std::vector<ChannelGroupingDiagnostic> groupingDiagnostics;
};

class ChannelAggregator final
{
public:
    struct HistoryRow
    {
        std::uint64_t sequence = 0;
        std::int64_t timestampNs = 0;
        std::vector<std::uint8_t> known;
        std::vector<std::uint8_t> occupied;
    };

    void reset();

    bool update(const SpectrumFrame& frame,
                const std::vector<float>& averageSpectrum,
                const std::vector<float>& maximumSpectrum,
                const std::vector<ChannelCandidate>& candidates,
                const std::vector<DetectionResult::TrackedDetection>& rawTracked,
                const ChannelAggregationConfig& config,
                float cnrThresholdDb,
                bool definitiveObservation,
                std::vector<DetectionResult::ChannelDetection>& output,
                ChannelEvidence* evidence = nullptr,
                const std::function<bool()>& cancelled = {});

private:
    struct Proposal
    {
        DetectedSignal measurement;
        std::vector<CandidateReference> contributors;
        std::size_t candidateCount = 0;
        double coverage = 0.0;
        double noiseFloorDbm = 0.0;
        std::string priorName;
        bool measurementValid = true;
        std::size_t diagnosticIndex = 0;
    };

    struct PendingProposal
    {
        Proposal proposal;
        std::uint32_t hits = 0;
        std::uint64_t lastSequence = 0;
    };

    struct TrackedChannel
    {
        DetectionResult::ChannelDetection value;
        std::deque<std::pair<double, double>> boundaryHistory;
        double estimatedCenterHz = 0.0;
        double estimatedBandwidthHz = 0.0;
        bool boundaryInitialized = false;
        std::uint32_t missingCount = 0;
        std::uint32_t splitCount = 0;
        std::int64_t missingDurationNs = 0;
        std::int64_t lastEvaluatedNs = 0;
        std::int64_t lastObservedNs = 0;
        std::uint64_t lastSequence = 0;
    };

    struct TrackedIndividual
    {
        DetectionResult::ChannelDetection value;
        std::int64_t rawTrackId = 0;
        std::uint32_t missingCount = 0;
        std::int64_t missingDurationNs = 0;
        std::int64_t lastEvaluatedNs = 0;
        std::int64_t lastObservedNs = 0;
        std::uint64_t lastSequence = 0;
    };

    std::deque<HistoryRow> m_history;
    std::vector<PendingProposal> m_pending;
    std::vector<TrackedChannel> m_channels;
    std::vector<TrackedIndividual> m_individuals;
    std::int64_t m_nextBusinessId = -1;
    std::uint64_t m_lastSequence = 0;
    std::int64_t m_lastTimestampNs = 0;
    std::deque<std::int64_t> m_frameIntervalsNs;
    bool m_previousObservationDefinitive = true;
};

} // namespace scn::algorithm
