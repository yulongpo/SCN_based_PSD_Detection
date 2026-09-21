#include "SignalTracker.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace scn::algorithm
{
void SignalTracker::reset(bool resetIds)
{
    m_tracks.clear();
    if (resetIds) m_nextId = 1;
}
bool SignalTracker::update(std::vector<DetectedSignal>& observations, std::int64_t time,
                           const TrackerConfig& config, std::size_t capacity,
                           const std::function<bool()>& cancelled)
{
    struct UpdateCancelled {};
    const auto checkCancelled = [&] {
        if (cancelled && cancelled()) throw UpdateCancelled{};
    };
    std::size_t operations = 0;
    const auto checkpoint = [&] {
        // Also used by sort/heap comparators. Throwing only disturbs work copies.
        if (++operations == 64) { operations = 0; checkCancelled(); }
    };
    try {
        checkCancelled();
        std::vector<DetectedSignal> workingObservations;
        workingObservations.reserve(observations.size());
        for (const auto& observation : observations) {
            checkpoint();
            workingObservations.push_back(observation);
        }
        std::vector<DetectedSignal> workingTracks;
        workingTracks.reserve(m_tracks.size());
        for (const auto& track : m_tracks) {
            checkpoint();
            if (time < track.lastSeenNs ||
                (static_cast<long double>(time) - track.lastSeenNs) > config.maxMissSeconds * 1e9L) continue;
            workingTracks.push_back(track);
        }
        const auto oldTrackCount = workingTracks.size();
        auto nextId = m_nextId;
        std::vector<bool> usedObservations(observations.size()), usedTracks(oldTrackCount);

        struct Match { std::size_t observation, track; double score, distance; };
        constexpr std::size_t batchSize = 64;
        struct Row
        {
            std::array<Match, batchSize> batch;
            std::size_t count = 0, next = 0;
            bool exhausted = false;
        };
        std::vector<Row> rows;
        if (oldTrackCount) {
            rows.reserve(observations.size());
            for (std::size_t i = 0; i < observations.size(); ++i) {
                checkpoint();
                rows.emplace_back();
            }
        }
        const auto better = [&](const Match& a, const Match& b) {
            checkpoint();
            if (a.score != b.score) return a.score > b.score;
            if (a.distance != b.distance) return a.distance < b.distance;
            if (workingTracks[a.track].id != workingTracks[b.track].id)
                return workingTracks[a.track].id < workingTracks[b.track].id;
            return a.observation < b.observation;
        };
        const auto lowerPriority = [&](const Match& a, const Match& b) { return better(b, a); };
        const auto refill = [&](std::size_t observationIndex) {
            checkCancelled();
            auto& row = rows[observationIndex];
            row.count = row.next = 0;
            std::size_t eligible = 0;
            const auto& observation = workingObservations[observationIndex];
            for (std::size_t t = 0; t < oldTrackCount; ++t) {
                checkpoint();
                if (usedTracks[t]) continue;
                // Unused tracks still contain their original, unsmoothed measurements.
                const auto& old = workingTracks[t];
                const double intersection = std::max(0.0, std::min(observation.endFrequencyHz, old.endFrequencyHz) -
                    std::max(observation.startFrequencyHz, old.startFrequencyHz));
                const double narrow = std::min(observation.bandwidthHz, old.bandwidthHz);
                if (narrow <= 0 || intersection / narrow < config.overlapRatio) continue;
                ++eligible;
                const Match match{observationIndex, t, intersection / narrow,
                    std::abs(observation.centerFrequencyHz - old.centerFrequencyHz)};
                // better as heap comparator keeps the WORST cached edge at the root.
                if (row.count < batchSize) {
                    row.batch[row.count++] = match;
                    std::push_heap(row.batch.begin(), row.batch.begin() + row.count, better);
                } else if (better(match, row.batch.front())) {
                    std::pop_heap(row.batch.begin(), row.batch.begin() + row.count, better);
                    row.batch[row.count - 1] = match;
                    std::push_heap(row.batch.begin(), row.batch.begin() + row.count, better);
                }
            }
            std::sort(row.batch.begin(), row.batch.begin() + row.count, better);
            row.exhausted = eligible <= batchSize;
        };
        // At most 64 edges per observation plus one head per active row, never NxM.
        // Refill scans ALL remaining tracks: the batch is a cache, not an edge cap.
        // Adversarial contention trades repeated scans for this bounded storage.
        std::vector<Match> heads;
        heads.reserve(rows.size());
        const auto pushHead = [&](const Match& match) {
            heads.push_back(match);
            std::push_heap(heads.begin(), heads.end(), lowerPriority);
        };
        for (std::size_t i = 0; i < rows.size(); ++i) {
            refill(i);
            if (rows[i].count) pushHead(rows[i].batch[0]);
        }
        while (!heads.empty()) {
            checkpoint();
            std::pop_heap(heads.begin(), heads.end(), lowerPriority);
            const auto match = heads.back();
            heads.pop_back();
            if (usedTracks[match.track]) {
                auto& row = rows[match.observation];
                do { checkpoint(); ++row.next; }
                while (row.next < row.count && usedTracks[row.batch[row.next].track]);
                if (row.next == row.count && !row.exhausted) refill(match.observation);
                if (row.next < row.count) pushHead(row.batch[row.next]);
                continue;
            }
            // Each row head is its best remaining edge (or a stale, already-used
            // edge). Removing stale heads therefore reproduces the global greedy
            // score/distance/track-ID/observation order without materializing it.
            auto& observation = workingObservations[match.observation];
            auto& track = workingTracks[match.track];
            observation.id = track.id;
            observation.firstSeenNs = track.firstSeenNs;
            observation.lastSeenNs = time;
            observation.occurrenceCount = track.occurrenceCount + 1;
            track = observation;
            usedObservations[match.observation] = usedTracks[match.track] = true;
            // A matched row is retired: it never contributes another head.
        }
        for (std::size_t i = 0; i < workingObservations.size(); ++i) {
            checkpoint();
            if (usedObservations[i]) continue;
            auto& observation = workingObservations[i];
            observation.id = nextId++;
            observation.firstSeenNs = observation.lastSeenNs = time;
            observation.occurrenceCount = 1;
            workingTracks.push_back(observation);
        }
        // Sort indices so observed status remains attached to the original slot.
        std::vector<std::size_t> order;
        order.reserve(workingTracks.size());
        for (std::size_t t = 0; t < workingTracks.size(); ++t) { checkpoint(); order.push_back(t); }
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            checkpoint();
            const bool currentA = a >= oldTrackCount || usedTracks[a];
            const bool currentB = b >= oldTrackCount || usedTracks[b];
            if (currentA != currentB) return currentA;
            const auto& first = workingTracks[a];
            const auto& second = workingTracks[b];
            return first.lastSeenNs != second.lastSeenNs ? first.lastSeenNs > second.lastSeenNs : first.id < second.id;
        });
        std::vector<DetectedSignal> retained;
        const auto retainedCount = std::min(capacity, order.size());
        retained.reserve(retainedCount);
        for (std::size_t i = 0; i < retainedCount; ++i) { checkpoint(); retained.push_back(workingTracks[order[i]]); }
        checkCancelled();
        // Commit boundary: no allocations or callbacks after the final check.
        m_tracks.swap(retained);
        observations.swap(workingObservations);
        m_nextId = nextId;
        return true;
    } catch (const UpdateCancelled&) {
        return false;
    }
}
}
