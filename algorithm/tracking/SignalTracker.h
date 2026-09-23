#pragma once

#include "../DetectionConfig.h"
#include "../types/DetectionTypes.h"
#include "../types/SpectrumTypes.h"

#include <functional>
#include <vector>

namespace scn::algorithm
{

class SignalTracker
{
public:
    void reset(bool resetIds = true);

    // State and outputs are built in temporaries. A cancellation leaves both the
    // tracker and caller-owned observations/results untouched.
    bool update(std::vector<DetectedSignal>& observations,
                std::vector<DetectionResult::TrackedDetection>& tracked,
                const SpectrumFrame& frame, const TrackerConfig& config,
                std::size_t capacity,
                const std::function<bool()>& cancelled = {});
    // Compatibility helper for focused tracker tests/tools that do not carry a
    // spectrum grid. Production detection always uses the full-grid overload.
    bool update(std::vector<DetectedSignal>& observations, std::int64_t timestampNs,
                const TrackerConfig& config, std::size_t capacity,
                const std::function<bool()>& cancelled = {});

private:
    struct CandidateBand
    {
        double centerHz = 0.0;
        double bandwidthHz = 0.0;
    };

    struct Track
    {
        DetectedSignal stable;
        std::vector<double> centerHistory;
        std::vector<double> bandwidthHistory;
        std::vector<CandidateBand> pendingBands;
        std::uint64_t lastPendingSequence = 0;
        std::uint64_t lastObservedSequence = 0;
    };

    std::vector<Track> m_tracks;
    std::int64_t m_nextId = 1;
    std::uint64_t m_lastSequence = 0;
};

} // namespace scn::algorithm
