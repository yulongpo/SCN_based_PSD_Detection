#include "SignalTracker.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>

namespace scn::algorithm
{
namespace
{
struct Cancelled {};

double median(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    const auto middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    const double upper = values[middle];
    if (values.size() % 2 != 0) return upper;
    const auto lower = *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
    return (lower + upper) / 2.0;
}

double intersectionWidth(const DetectedSignal& a, const DetectedSignal& b)
{
    return std::max(0.0, std::min(a.endFrequencyHz, b.endFrequencyHz) -
                         std::max(a.startFrequencyHz, b.startFrequencyHz));
}

double bandwidthRatio(double a, double b)
{
    const auto narrow = std::min(a, b);
    return narrow > 0.0 ? std::max(a, b) / narrow : std::numeric_limits<double>::infinity();
}

void setAlignedBand(DetectedSignal& signal, double center, double width,
                    const SpectrumFrame& frame)
{
    const double domainStart = frame.startFrequencyHz;
    const double domainEnd = frame.endFrequencyHz();
    const double bins = static_cast<double>(frame.powerDb.size());
    if (!(width > 0.0) || !std::isfinite(center) || !std::isfinite(width) ||
        !std::isfinite(domainEnd) || bins < 1.0) return;

    const double left = std::clamp(center - width / 2.0, domainStart, domainEnd);
    const double right = std::clamp(center + width / 2.0, domainStart, domainEnd);
    auto first = static_cast<long long>(std::floor((left - domainStart) / frame.binWidthHz + 1e-10));
    auto last = static_cast<long long>(std::ceil((right - domainStart) / frame.binWidthHz - 1e-10));
    first = std::clamp(first, 0LL, static_cast<long long>(frame.powerDb.size()));
    last = std::clamp(last, 0LL, static_cast<long long>(frame.powerDb.size()));
    if (last <= first) {
        if (first >= static_cast<long long>(frame.powerDb.size())) first = static_cast<long long>(frame.powerDb.size()) - 1;
        last = first + 1;
    }
    signal.startFrequencyHz = domainStart + static_cast<double>(first) * frame.binWidthHz;
    signal.endFrequencyHz = domainStart + static_cast<double>(last) * frame.binWidthHz;
    signal.bandwidthHz = signal.endFrequencyHz - signal.startFrequencyHz;
    signal.centerFrequencyHz = (signal.startFrequencyHz + signal.endFrequencyHz) / 2.0;
}

template<typename TrackT>
void appendHistory(TrackT& track, const DetectedSignal& observation,
                   const TrackerConfig& config, const SpectrumFrame& frame)
{
    track.centerHistory.push_back(observation.centerFrequencyHz);
    track.bandwidthHistory.push_back(observation.bandwidthHz);
    const auto limit = std::max<std::size_t>(1, config.medianWindow);
    if (track.centerHistory.size() > limit) track.centerHistory.erase(track.centerHistory.begin());
    if (track.bandwidthHistory.size() > limit) track.bandwidthHistory.erase(track.bandwidthHistory.begin());
    const double targetCenter = median(track.centerHistory);
    const double targetWidth = median(track.bandwidthHistory);
    const double alpha = config.smoothingAlpha;
    const double center = track.stable.centerFrequencyHz + alpha * (targetCenter - track.stable.centerFrequencyHz);
    const double width = track.stable.bandwidthHz + alpha * (targetWidth - track.stable.bandwidthHz);
    const auto id = track.stable.id;
    const auto firstSeen = track.stable.firstSeenNs;
    track.stable = observation;
    track.stable.id = id;
    track.stable.firstSeenNs = firstSeen;
    setAlignedBand(track.stable, center, width, frame);
}

} // namespace

void SignalTracker::reset(bool resetIds)
{
    m_tracks.clear();
    m_lastSequence = 0;
    if (resetIds) m_nextId = 1;
}

bool SignalTracker::update(std::vector<DetectedSignal>& observations,
                           std::int64_t timestampNs, const TrackerConfig& config,
                           std::size_t capacity, const std::function<bool()>& cancelled)
{
    double startHz = std::numeric_limits<double>::infinity();
    double endHz = -std::numeric_limits<double>::infinity();
    for (const auto& item : observations) {
        startHz = std::min(startHz, item.startFrequencyHz);
        endHz = std::max(endHz, item.endFrequencyHz);
    }
    for (const auto& track : m_tracks) {
        startHz = std::min(startHz, track.stable.startFrequencyHz);
        endHz = std::max(endHz, track.stable.endFrequencyHz);
    }
    if (!std::isfinite(startHz) || !std::isfinite(endHz)) { startHz = 0.0; endHz = 1.0; }
    if (!(endHz > startHz)) endHz = startHz + 1.0;
    const double spanHz = endHz - startHz;
    constexpr std::size_t maxSyntheticBins = 8192;
    const double syntheticBinWidth = std::max(1.0, spanHz / static_cast<double>(maxSyntheticBins));
    const auto syntheticCount = static_cast<std::size_t>(std::clamp(
        std::ceil(spanHz / syntheticBinWidth), 1.0, static_cast<double>(maxSyntheticBins)));
    SpectrumFrame synthetic;
    synthetic.sequence = m_lastSequence + 1;
    synthetic.timestampNs = timestampNs;
    synthetic.startFrequencyHz = startHz;
    synthetic.binWidthHz = syntheticBinWidth;
    synthetic.powerDb.resize(syntheticCount, 0.0F);
    std::vector<DetectionResult::TrackedDetection> ignored;
    return update(observations, ignored, synthetic, config, capacity, cancelled);
}

bool SignalTracker::update(std::vector<DetectedSignal>& observations,
                           std::vector<DetectionResult::TrackedDetection>& tracked,
                           const SpectrumFrame& frame, const TrackerConfig& config,
                           std::size_t capacity,
                           const std::function<bool()>& cancelled)
{
    const auto checkCancelled = [&] {
        if (cancelled && cancelled()) throw Cancelled{};
    };
    try {
        checkCancelled();
        std::vector<DetectedSignal> workObservations = observations;
        std::vector<Track> workTracks;
        workTracks.reserve(m_tracks.size() + observations.size());
        for (const auto& track : m_tracks) {
            if (frame.timestampNs < track.stable.lastSeenNs ||
                static_cast<long double>(frame.timestampNs) - track.stable.lastSeenNs > config.maxMissSeconds * 1e9L)
                continue;
            workTracks.push_back(track);
        }
        if (m_lastSequence != 0 && frame.sequence != m_lastSequence + 1) {
            for (auto& track : workTracks) {
                track.pendingBands.clear();
                track.lastPendingSequence = 0;
            }
        }
        auto nextId = m_nextId;
        const auto trackCount = workTracks.size();
        const auto observationCount = workObservations.size();
        std::vector<bool> usedTrack(trackCount, false), usedObservation(observationCount, false);

        struct Edge
        {
            std::size_t observation = 0;
            std::size_t track = 0;
            double iou = 0.0;
            double distance = 0.0;
            double ratio = 1.0;
            bool jump = false;
        };
        std::vector<Edge> uniqueJumpEdges;
        std::vector<std::size_t> compatibleObservationDegree(observationCount, 0);
        std::vector<std::size_t> compatibleTrackDegree(trackCount, 0);
        std::vector<Edge> jumpCandidate(observationCount);
        std::vector<bool> hasJumpCandidate(observationCount, false);
        std::vector<bool> ambiguousJumpObservation(observationCount, false);
        std::vector<bool> hadCompatible(observationCount, false);
        std::size_t operationCount = 0;
        const auto checkpoint = [&] {
            if (++operationCount >= 256) { operationCount = 0; checkCancelled(); }
        };
        const double epsilon = 2.0 * frame.binWidthHz;

        // Scan compatible pairs without materializing an O(N*M) matrix. Only a
        // bounded best-edge cache is retained for each observation.
        for (std::size_t oi = 0; oi < observationCount; ++oi) {
            for (std::size_t ti = 0; ti < trackCount; ++ti) {
                checkpoint();
                const auto& obs = workObservations[oi];
                const auto& old = workTracks[ti].stable;
                if (obs.bandwidthHz <= 0.0 || old.bandwidthHz <= 0.0) continue;
                const double overlap = intersectionWidth(obs, old);
                const double narrow = std::min(obs.bandwidthHz, old.bandwidthHz);
                const double overlapOfNarrow = overlap / narrow;
                if (overlapOfNarrow < config.overlapRatio) continue;
                const double distance = std::abs(obs.centerFrequencyHz - old.centerFrequencyHz);
                const double centerLimit = std::max(config.centerDistanceRatio * old.bandwidthHz, epsilon);
                if (config.boundaryStabilityEnabled && distance > centerLimit) continue;
                const double ratio = bandwidthRatio(obs.bandwidthHz, old.bandwidthHz);
                const double unionWidth = obs.bandwidthHz + old.bandwidthHz - overlap;
                const double iou = unionWidth > 0.0 ? overlap / unionWidth : 0.0;
                const double edgeLimit = std::max(config.jumpEdgeChangeRatio * old.bandwidthHz, epsilon);
                const bool edgeMoved = std::abs(obs.startFrequencyHz - old.startFrequencyHz) > edgeLimit ||
                                       std::abs(obs.endFrequencyHz - old.endFrequencyHz) > edgeLimit;
                const bool jump = config.boundaryStabilityEnabled &&
                                  (ratio > config.maxBandwidthRatio || edgeMoved);
                Edge edge{oi, ti, iou, distance, ratio, jump};
                hadCompatible[oi] = true;
                ++compatibleObservationDegree[oi];
                ++compatibleTrackDegree[ti];
                if (jump && !hasJumpCandidate[oi]) {
                    jumpCandidate[oi] = edge;
                    hasJumpCandidate[oi] = true;
                }
            }
        }

        for (std::size_t oi = 0; oi < observationCount; ++oi) {
            checkpoint();
            if (!hasJumpCandidate[oi]) continue;
            if (compatibleObservationDegree[oi] == 1 &&
                compatibleTrackDegree[jumpCandidate[oi].track] == 1)
                uniqueJumpEdges.push_back(jumpCandidate[oi]);
            else
                ambiguousJumpObservation[oi] = true;
        }

        auto edgeBetter = [&](const Edge& a, const Edge& b) {
            if (a.iou != b.iou) return a.iou > b.iou;
            if (a.distance != b.distance) return a.distance < b.distance;
            if (a.ratio != b.ratio) return a.ratio < b.ratio;
            const auto idA = workTracks[a.track].stable.id;
            const auto idB = workTracks[b.track].stable.id;
            if (idA != idB) return idA < idB;
            return a.observation < b.observation;
        };
        std::sort(uniqueJumpEdges.begin(), uniqueJumpEdges.end(), edgeBetter);
        std::vector<DetectionResult::TrackedDetection> workTracked(observationCount);

        const auto attachMetadata = [&](DetectedSignal& value, const DetectedSignal& previous) {
            value.id = previous.id;
            value.firstSeenNs = previous.firstSeenNs;
            value.lastSeenNs = frame.timestampNs;
            value.occurrenceCount = previous.occurrenceCount + 1;
        };

        for (const auto& edge : uniqueJumpEdges) {
            checkpoint();
            if (usedTrack[edge.track] || usedObservation[edge.observation]) continue;
            auto& track = workTracks[edge.track];
            auto& observation = workObservations[edge.observation];
            auto& out = workTracked[edge.observation];
            attachMetadata(observation, track.stable);
            const auto expectedSequence = track.lastPendingSequence + 1;
            if (track.pendingBands.empty() || frame.sequence != expectedSequence) track.pendingBands.clear();

            std::vector<double> centers, widths;
            centers.reserve(track.pendingBands.size());
            widths.reserve(track.pendingBands.size());
            for (const auto& band : track.pendingBands) {
                centers.push_back(band.centerHz);
                widths.push_back(band.bandwidthHz);
            }
            const double candidateCenter = median(centers);
            const double candidateWidth = median(widths);
            const bool consistent = !track.pendingBands.empty() &&
                std::abs(observation.centerFrequencyHz - candidateCenter) <=
                    std::max(config.jumpCenterToleranceRatio * candidateWidth, epsilon) &&
                bandwidthRatio(observation.bandwidthHz, candidateWidth) <= config.jumpBandwidthToleranceRatio;
            if (!consistent) track.pendingBands.clear();
            track.pendingBands.push_back({observation.centerFrequencyHz, observation.bandwidthHz});
            track.lastPendingSequence = frame.sequence;

            if (track.pendingBands.size() >= config.jumpConfirmationCount) {
                centers.clear(); widths.clear();
                for (const auto& band : track.pendingBands) {
                    centers.push_back(band.centerHz);
                    widths.push_back(band.bandwidthHz);
                }
                const double acceptedCenter = median(centers);
                const double acceptedWidth = median(widths);
                track.stable = observation;
                setAlignedBand(track.stable, acceptedCenter, acceptedWidth, frame);
                track.centerHistory.assign(1, acceptedCenter);
                track.bandwidthHistory.assign(1, acceptedWidth);
                track.pendingBands.clear();
                out.boundaryState = BoundaryState::Stable;
                out.diagnostic = "边界突变连续确认完成";
            } else {
                const auto center = track.stable.centerFrequencyHz;
                const auto width = track.stable.bandwidthHz;
                track.stable = observation;
                setAlignedBand(track.stable, center, width, frame);
                out.boundaryState = BoundaryState::PendingChange;
                out.pendingCount = track.pendingBands.size();
                out.requiredCount = config.jumpConfirmationCount;
                out.diagnostic = "边界变化待确认";
            }
            track.lastObservedSequence = frame.sequence;
            out.raw = observation;
            out.stable = track.stable;
            out.associationIou = edge.iou;
            out.centerDistanceHz = edge.distance;
            out.bandwidthRatio = edge.ratio;
            usedTrack[edge.track] = usedObservation[edge.observation] = true;
        }

        struct EdgeRow { std::vector<Edge> batch; std::size_t next = 0; bool exhausted = false; };
        std::vector<EdgeRow> rows(observationCount);
        constexpr std::size_t cachedEdgesPerObservation = 64;
        const auto refill = [&](std::size_t oi) {
            auto& row = rows[oi];
            row.batch.clear(); row.next = 0;
            std::size_t eligible = 0;
            if (usedObservation[oi] || ambiguousJumpObservation[oi]) { row.exhausted = true; return; }
            auto worseFirst = [&](const Edge& a, const Edge& b) { return edgeBetter(a, b); };
            for (std::size_t ti = 0; ti < trackCount; ++ti) {
                checkpoint();
                if (usedTrack[ti]) continue;
                const auto& obs = workObservations[oi];
                const auto& old = workTracks[ti].stable;
                if (obs.bandwidthHz <= 0.0 || old.bandwidthHz <= 0.0) continue;
                const double overlap = intersectionWidth(obs, old);
                const double narrow = std::min(obs.bandwidthHz, old.bandwidthHz);
                if (overlap / narrow < config.overlapRatio) continue;
                const double distance = std::abs(obs.centerFrequencyHz - old.centerFrequencyHz);
                if (config.boundaryStabilityEnabled &&
                    distance > std::max(config.centerDistanceRatio * old.bandwidthHz, epsilon)) continue;
                const double ratio = bandwidthRatio(obs.bandwidthHz, old.bandwidthHz);
                const double edgeLimit = std::max(config.jumpEdgeChangeRatio * old.bandwidthHz, epsilon);
                const bool edgeMoved = std::abs(obs.startFrequencyHz - old.startFrequencyHz) > edgeLimit ||
                                       std::abs(obs.endFrequencyHz - old.endFrequencyHz) > edgeLimit;
                if (config.boundaryStabilityEnabled &&
                    (ratio > config.maxBandwidthRatio || edgeMoved)) continue;
                const double unionWidth = obs.bandwidthHz + old.bandwidthHz - overlap;
                Edge candidate{oi, ti, unionWidth > 0.0 ? overlap / unionWidth : 0.0,
                               distance, ratio, false};
                ++eligible;
                row.batch.push_back(candidate);
                std::push_heap(row.batch.begin(), row.batch.end(), worseFirst);
                if (row.batch.size() > cachedEdgesPerObservation) {
                    std::pop_heap(row.batch.begin(), row.batch.end(), worseFirst);
                    row.batch.pop_back();
                }
            }
            std::sort(row.batch.begin(), row.batch.end(), edgeBetter);
            row.exhausted = eligible <= cachedEdgesPerObservation;
        };
        const auto headCompare = [&](const Edge& a, const Edge& b) { return edgeBetter(b, a); };
        std::vector<Edge> heads;
        heads.reserve(observationCount);
        const auto advanceRow = [&](std::size_t oi) {
            auto& row = rows[oi];
            do { ++row.next; }
            while (row.next < row.batch.size() && usedTrack[row.batch[row.next].track]);
            if (row.next == row.batch.size() && !row.exhausted) refill(oi);
            if (row.next < row.batch.size()) {
                heads.push_back(row.batch[row.next]);
                std::push_heap(heads.begin(), heads.end(), headCompare);
            }
        };
        for (std::size_t oi = 0; oi < observationCount; ++oi) {
            if (usedObservation[oi]) continue;
            refill(oi);
            if (!rows[oi].batch.empty()) {
                heads.push_back(rows[oi].batch.front());
                std::push_heap(heads.begin(), heads.end(), headCompare);
            }
        }
        while (!heads.empty()) {
            checkpoint();
            std::pop_heap(heads.begin(), heads.end(), headCompare);
            const auto edge = heads.back();
            heads.pop_back();
            if (usedTrack[edge.track]) {
                advanceRow(edge.observation);
                continue;
            }
            auto& track = workTracks[edge.track];
            auto& observation = workObservations[edge.observation];
            attachMetadata(observation, track.stable);
            track.pendingBands.clear();
            track.lastPendingSequence = 0;
            if (config.boundaryStabilityEnabled) appendHistory(track, observation, config, frame);
            else {
                track.stable = observation;
                track.centerHistory.clear();
                track.bandwidthHistory.clear();
            }
            track.lastObservedSequence = frame.sequence;
            auto& out = workTracked[edge.observation];
            out.raw = observation;
            out.stable = track.stable;
            if (!config.boundaryStabilityEnabled) out.boundaryState = BoundaryState::Disabled;
            else out.boundaryState = BoundaryState::Stable;
            out.associationIou = edge.iou;
            out.centerDistanceHz = edge.distance;
            out.bandwidthRatio = edge.ratio;
            usedTrack[edge.track] = usedObservation[edge.observation] = true;
        }

        for (std::size_t ti = 0; ti < trackCount; ++ti) {
            if (!usedTrack[ti]) {
                workTracks[ti].pendingBands.clear();
                workTracks[ti].lastPendingSequence = 0;
            }
        }

        for (std::size_t oi = 0; oi < observationCount; ++oi) {
            checkpoint();
            if (usedObservation[oi]) continue;
            auto& observation = workObservations[oi];
            observation.id = nextId++;
            observation.firstSeenNs = observation.lastSeenNs = frame.timestampNs;
            observation.occurrenceCount = 1;
            Track track;
            track.stable = observation;
            track.lastObservedSequence = frame.sequence;
            track.centerHistory.push_back(observation.centerFrequencyHz);
            track.bandwidthHistory.push_back(observation.bandwidthHz);
            workTracks.push_back(std::move(track));
            auto& out = workTracked[oi];
            out.raw = observation;
            out.stable = observation;
            if (!config.boundaryStabilityEnabled) out.boundaryState = BoundaryState::Disabled;
            else if (hadCompatible[oi]) {
                out.boundaryState = BoundaryState::Ambiguous;
                out.diagnostic = ambiguousJumpObservation[oi]
                    ? "突变关联不唯一，作为新信号建立身份"
                    : "轨迹关联不唯一，作为新信号建立身份";
            } else out.boundaryState = BoundaryState::Stable;
        }

        // Retain recently observed identities first, then dormant tracks by recency.
        std::stable_sort(workTracks.begin(), workTracks.end(), [&frame](const Track& a, const Track& b) {
            const bool observedA = a.lastObservedSequence == frame.sequence;
            const bool observedB = b.lastObservedSequence == frame.sequence;
            if (observedA != observedB) return observedA;
            if (a.stable.lastSeenNs != b.stable.lastSeenNs) return a.stable.lastSeenNs > b.stable.lastSeenNs;
            return a.stable.id < b.stable.id;
        });
        if (workTracks.size() > capacity) workTracks.resize(capacity);
        checkCancelled();
        observations.swap(workObservations);
        tracked.swap(workTracked);
        m_tracks.swap(workTracks);
        m_nextId = nextId;
        m_lastSequence = frame.sequence;
        return true;
    } catch (const Cancelled&) {
        return false;
    }
}

} // namespace scn::algorithm
