#pragma once
#include "../DetectionConfig.h"
#include "../types/DetectionTypes.h"
#include <functional>

namespace scn::algorithm
{
class SignalTracker
{
public:
    void reset(bool resetIds = true);
    // False means cancelled. Cancellation and exceptions leave both observations
    // and tracker state unchanged; only a successful update commits its work.
    bool update(std::vector<DetectedSignal>& observations, std::int64_t timestampNs,
                const TrackerConfig& config, std::size_t capacity,
                const std::function<bool()>& cancelled = {});
private:
    std::vector<DetectedSignal> m_tracks;
    std::int64_t m_nextId = 1;
};
}
