#include "DetectionOverlayModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace
{
constexpr double kCenterTauSeconds = 0.070;
constexpr double kExpandTauSeconds = 0.040;
constexpr double kShrinkTauSeconds = 0.080;
constexpr double kAppearTauSeconds = 0.080;
constexpr double kExpireTauSeconds = 0.150;
constexpr double kProvisionalHoldSeconds = 0.150;
constexpr double kLegacyHoldSeconds = 0.500;
constexpr double kMinimumBandwidthHz = 1.0;
constexpr double kInvisibleOpacity = 0.01;

double centerOf(const HQSigMF::DetectionObject& object)
{
    return (static_cast<double>(object.f_start_hz) +
            static_cast<double>(object.f_end_hz)) * 0.5;
}

double bandwidthOf(const HQSigMF::DetectionObject& object)
{
    return std::max(kMinimumBandwidthHz,
                    static_cast<double>(object.f_end_hz - object.f_start_hz));
}
}

DetectionOverlayModel::IngestResult DetectionOverlayModel::ingest(
    const HQSigMF& signal, bool provisional, double viewportSpanHz)
{
    IngestResult ingestResult;
    std::map<int32_t, HQSigMF::TrackingResult> trackingByObject;
    std::set<int64_t> inactiveTrackIds;

    auto trackingSection = signal.getSection(
        Domain::FEATURE,
        static_cast<int32_t>(FeatureCategory::TRACKING_RESULT));
    if (trackingSection && trackingSection->property.data_type == DataType::OBJ)
    {
        const auto tracking = trackingSection->getData<HQSigMF::TrackingResult>();
        for (size_t i = 0; tracking.first && i < tracking.second; ++i)
        {
            const auto& result = tracking.first[i];
            if (result.version != 1 ||
                result.record_size < static_cast<int32_t>(sizeof(HQSigMF::TrackingResult)))
                continue;

            if (result.object_index >= 0)
                trackingByObject[result.object_index] = result;
            else
            {
                if ((result.state == TrackLifeState::COASTING ||
                     result.state == TrackLifeState::EXPIRED) &&
                    result.track_id > 0)
                {
                    inactiveTrackIds.insert(result.track_id);
                }

                auto entry = m_entries.find(result.track_id);
                if (entry == m_entries.end()) continue;
                entry->second.managedByTracker = true;
                entry->second.state = result.state;
                if (result.state == TrackLifeState::COASTING ||
                    result.state == TrackLifeState::EXPIRED)
                {
                    entry->second.targetOpacity = 0.0;
                    entry->second.deactivationReported = true;
                }
            }
        }
    }

    int32_t objectIndex = 0;
    const auto objectSections = signal.getSections(Domain::OBJECTS);
    for (const auto& section : objectSections)
    {
        if (!section) continue;
        const auto objects = section->getData<HQSigMF::DetectionObject>();
        for (size_t i = 0; objects.first && i < objects.second; ++i, ++objectIndex)
        {
            Observation observation;
            observation.object = objects.first[i];
            observation.provisional = provisional;

            auto tracking = trackingByObject.find(objectIndex);
            if (tracking != trackingByObject.end())
            {
                observation.state = tracking->second.state;
                observation.object.f_start_hz = tracking->second.tracked_start_hz;
                observation.object.f_end_hz = tracking->second.tracked_end_hz;
                if (tracking->second.track_id > 0)
                {
                    observation.object.id = tracking->second.track_id;
                }
                else
                {
                    // Capacity-limited observations are explicitly untracked (id=0). Give
                    // each object a presentation-only key so multiple results cannot alias a
                    // shared map entry or make one overlay jump between frequency bands.
                    observation.object.id = provisionalKey(objectIndex);
                    observation.state = TrackLifeState::TENTATIVE;
                    observation.provisional = true;
                }
            }
            else
            {
                if (provisional || observation.object.id <= 0)
                {
                    observation.state = TrackLifeState::TENTATIVE;
                    observation.object.id = provisionalKey(objectIndex);
                    observation.provisional = true;
                }
                else
                {
                    observation.state = TrackLifeState::CONFIRMED;
                }
            }

            const int64_t key = observation.object.id;
            const double targetCenter = centerOf(observation.object);
            const double targetBandwidth = bandwidthOf(observation.object);
            const double targetLogBandwidth = std::log(targetBandwidth);
            auto entryIt = m_entries.find(key);
            if (entryIt == m_entries.end())
            {
                Entry entry;
                entry.object = observation.object;
                entry.state = observation.state;
                entry.currentCenterHz = targetCenter;
                entry.targetCenterHz = targetCenter;
                entry.currentLogBandwidth = targetLogBandwidth;
                entry.targetLogBandwidth = targetLogBandwidth;
                entry.opacity = 0.0;
                entry.targetOpacity = 1.0;
                entry.secondsSinceSeen = 0.0;
                entry.provisional = observation.provisional;
                entry.managedByTracker = tracking != trackingByObject.end() &&
                    tracking->second.track_id > 0;
                entry.deactivationReported = false;
                m_entries.emplace(key, entry);
            }
            else
            {
                Entry& entry = entryIt->second;
                const double jump = std::abs(entry.currentCenterHz - targetCenter);
                const double snapThreshold = viewportSpanHz > 0.0
                    ? viewportSpanHz * 0.25
                    : std::numeric_limits<double>::max();
                if (jump > snapThreshold ||
                    entry.provisional != observation.provisional)
                {
                    entry.currentCenterHz = targetCenter;
                    entry.currentLogBandwidth = targetLogBandwidth;
                }
                entry.object = observation.object;
                entry.state = observation.state;
                entry.targetCenterHz = targetCenter;
                entry.targetLogBandwidth = targetLogBandwidth;
                entry.targetOpacity = 1.0;
                entry.secondsSinceSeen = 0.0;
                entry.provisional = observation.provisional;
                entry.managedByTracker = entry.managedByTracker ||
                    (tracking != trackingByObject.end() &&
                     tracking->second.track_id > 0);
                entry.deactivationReported = false;
            }

            ingestResult.observations.push_back(observation);
        }
    }

    ingestResult.inactiveTrackIds.assign(
        inactiveTrackIds.begin(), inactiveTrackIds.end());
    return ingestResult;
}

std::vector<int64_t> DetectionOverlayModel::advance(double elapsedSeconds)
{
    std::vector<int64_t> inactiveTrackIds;
    if (!(elapsedSeconds > 0.0)) return inactiveTrackIds;
    elapsedSeconds = std::min(elapsedSeconds, 0.250);

    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        Entry& entry = it->second;
        entry.secondsSinceSeen += elapsedSeconds;

        const bool shouldDeactivate =
            entry.state == TrackLifeState::COASTING ||
            entry.state == TrackLifeState::EXPIRED ||
            (entry.provisional &&
             entry.secondsSinceSeen > kProvisionalHoldSeconds) ||
            (!entry.managedByTracker &&
             entry.secondsSinceSeen > kLegacyHoldSeconds);
        if (shouldDeactivate)
        {
            entry.targetOpacity = 0.0;
            if (!entry.deactivationReported)
            {
                inactiveTrackIds.push_back(it->first);
                entry.deactivationReported = true;
            }
        }

        const double centerAlpha = smoothingAlpha(elapsedSeconds, kCenterTauSeconds);
        entry.currentCenterHz += centerAlpha *
            (entry.targetCenterHz - entry.currentCenterHz);

        const double bandwidthTau = entry.targetLogBandwidth > entry.currentLogBandwidth
            ? kExpandTauSeconds
            : kShrinkTauSeconds;
        const double bandwidthAlpha = smoothingAlpha(elapsedSeconds, bandwidthTau);
        entry.currentLogBandwidth += bandwidthAlpha *
            (entry.targetLogBandwidth - entry.currentLogBandwidth);

        const double opacityTau = entry.targetOpacity > entry.opacity
            ? kAppearTauSeconds
            : kExpireTauSeconds;
        const double opacityAlpha = smoothingAlpha(elapsedSeconds, opacityTau);
        entry.opacity += opacityAlpha * (entry.targetOpacity - entry.opacity);

        if (entry.targetOpacity <= 0.0 && entry.opacity < kInvisibleOpacity)
            it = m_entries.erase(it);
        else
            ++it;
    }
    return inactiveTrackIds;
}

std::vector<DetectionOverlayModel::Snapshot> DetectionOverlayModel::snapshots() const
{
    std::vector<Snapshot> result;
    result.reserve(m_entries.size());
    for (const auto& pair : m_entries)
    {
        const Entry& entry = pair.second;
        if (entry.opacity < kInvisibleOpacity) continue;

        Snapshot snapshot;
        snapshot.object = entry.object;
        snapshot.state = entry.state;
        snapshot.opacity = entry.opacity;

        const double bandwidth = std::exp(entry.currentLogBandwidth);
        snapshot.object.f_start_hz = static_cast<int64_t>(std::llround(
            entry.currentCenterHz - bandwidth * 0.5));
        snapshot.object.f_end_hz = static_cast<int64_t>(std::llround(
            entry.currentCenterHz + bandwidth * 0.5));
        result.push_back(snapshot);
    }
    return result;
}

void DetectionOverlayModel::clear()
{
    m_entries.clear();
}

double DetectionOverlayModel::smoothingAlpha(double elapsedSeconds,
                                             double timeConstantSeconds)
{
    if (!(timeConstantSeconds > 0.0)) return 1.0;
    return 1.0 - std::exp(-elapsedSeconds / timeConstantSeconds);
}

int64_t DetectionOverlayModel::provisionalKey(int objectIndex)
{
    return -1 - static_cast<int64_t>(objectIndex);
}
