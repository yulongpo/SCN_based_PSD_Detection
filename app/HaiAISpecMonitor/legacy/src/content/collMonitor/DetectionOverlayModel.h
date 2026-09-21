#pragma once

#include "radioai/icd/HQSigMF.hpp"

#include <cstdint>
#include <map>
#include <vector>

/**
 * @brief Presentation-only state model for live detection overlays.
 *
 * Tracker boundaries remain the business result. This class interpolates the
 * center, log bandwidth and opacity at UI frame rate without writing back to
 * HQSigMF or affecting alarms and persistence.
 */
class DetectionOverlayModel
{
public:
    struct Observation
    {
        HQSigMF::DetectionObject object{};
        TrackLifeState state = TrackLifeState::CONFIRMED;
        bool provisional = false;
    };

    struct Snapshot
    {
        HQSigMF::DetectionObject object{};
        TrackLifeState state = TrackLifeState::CONFIRMED;
        double opacity = 1.0;
    };

    struct IngestResult
    {
        std::vector<Observation> observations;
        std::vector<int64_t> inactiveTrackIds;
    };

    /**
     * Ingest a packet and return both current observations and tracks which
     * became inactive in this packet. Inactive IDs let time-frequency views
     * close their current segment without coupling presentation lifetime to
     * the tracker's identity-retention timeout.
     */
    IngestResult ingest(const HQSigMF& signal,
                        bool provisional,
                        double viewportSpanHz);

    /**
     * Advance interpolation/fades and return IDs whose presentation lifetime
     * ended on this tick so the time-frequency view can release active marks.
     */
    std::vector<int64_t> advance(double elapsedSeconds);

    /** Return the current spectrum-overlay snapshots. */
    std::vector<Snapshot> snapshots() const;

    void clear();

private:
    struct Entry
    {
        HQSigMF::DetectionObject object{};
        TrackLifeState state = TrackLifeState::CONFIRMED;
        double currentCenterHz = 0.0;
        double targetCenterHz = 0.0;
        double currentLogBandwidth = 0.0;
        double targetLogBandwidth = 0.0;
        double opacity = 0.0;
        double targetOpacity = 1.0;
        double secondsSinceSeen = 0.0;
        bool provisional = false;
        bool managedByTracker = false;
        bool deactivationReported = false;
    };

    static double smoothingAlpha(double elapsedSeconds, double timeConstantSeconds);
    static int64_t provisionalKey(int objectIndex);

    std::map<int64_t, Entry> m_entries;
};
