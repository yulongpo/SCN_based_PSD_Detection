#include "WhitelistResultResolver.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace scn::application::policy
{

namespace
{
bool stronger(const algorithm::DetectedSignal& lhs, const algorithm::DetectedSignal& rhs)
{
    if (lhs.signalLevelDbm != rhs.signalLevelDbm)
        return lhs.signalLevelDbm > rhs.signalLevelDbm;
    if (lhs.confidence != rhs.confidence)
        return lhs.confidence > rhs.confidence;
    return lhs.id < rhs.id;
}
}

std::vector<PolicySignal> WhitelistResultResolver::resolve(
    const std::vector<algorithm::DetectedSignal>& rawSignals,
    const std::vector<WhitelistEntry>& whitelists) const
{
    std::vector<PolicySignal> resolved;
    std::vector<bool> replaced(rawSignals.size(), false);

    for (const auto& whitelist : whitelists) {
        if (!whitelist.enabled) continue;

        std::vector<std::size_t> matches;
        for (std::size_t index = 0; index < rawSignals.size(); ++index) {
            const auto& signal = rawSignals[index];
            if (intervalsOverlap(signal.startFrequencyHz, signal.endFrequencyHz,
                                 whitelist.startFrequencyHz, whitelist.endFrequencyHz)) {
                matches.push_back(index);
                replaced[index] = true;
            }
        }
        if (matches.empty()) continue;

        const auto representative = *std::min_element(matches.begin(), matches.end(),
            [&rawSignals](std::size_t lhs, std::size_t rhs) {
                return stronger(rawSignals[lhs], rawSignals[rhs]);
            });
        const auto& source = rawSignals[representative];

        PolicySignal result;
        result.source = PolicySignalSource::Whitelist;
        result.id = whitelist.id;
        result.displayId = "W-" + std::to_string(whitelist.id);
        result.representativeSignalId = source.id;
        result.measurement = source;
        result.stableMeasurement = source;
        result.rawMeasurement = source;
        result.measurementBranch = source.branch;
        result.measurement.id = whitelist.id;
        result.measurement.startFrequencyHz = static_cast<double>(whitelist.startFrequencyHz);
        result.measurement.endFrequencyHz = static_cast<double>(whitelist.endFrequencyHz);
        result.measurement.centerFrequencyHz =
            (static_cast<double>(whitelist.startFrequencyHz) +
             static_cast<double>(whitelist.endFrequencyHz)) / 2.0;
        result.measurement.bandwidthHz = static_cast<double>(whitelist.endFrequencyHz) -
                                         static_cast<double>(whitelist.startFrequencyHz);
        result.whitelistIds.push_back(whitelist.id);
        result.whitelistNames.push_back(whitelist.name);
        result.originalSignalIds.reserve(matches.size());
        for (const auto index : matches)
            result.originalSignalIds.push_back(rawSignals[index].id);
        std::sort(result.originalSignalIds.begin(), result.originalSignalIds.end());
        result.measurement.occurrenceCount = source.occurrenceCount;
        resolved.push_back(std::move(result));
    }

    for (std::size_t index = 0; index < rawSignals.size(); ++index) {
        if (replaced[index]) continue;
        PolicySignal result;
        result.source = PolicySignalSource::RawDetection;
        result.id = rawSignals[index].id;
        result.displayId = std::to_string(rawSignals[index].id);
        result.measurement = rawSignals[index];
        result.stableMeasurement = rawSignals[index];
        result.rawMeasurement = rawSignals[index];
        result.measurementBranch = rawSignals[index].branch;
        result.representativeSignalId = rawSignals[index].id;
        result.originalSignalIds.push_back(rawSignals[index].id);
        resolved.push_back(std::move(result));
    }
    return resolved;
}

std::vector<PolicySignal> WhitelistResultResolver::resolve(
    const std::vector<algorithm::DetectionResult::TrackedDetection>& trackedSignals,
    const std::vector<WhitelistEntry>& whitelists) const
{
    std::vector<algorithm::DetectedSignal> stableSignals;
    stableSignals.reserve(trackedSignals.size());
    std::unordered_map<std::int64_t, const algorithm::DetectionResult::TrackedDetection*> byId;
    byId.reserve(trackedSignals.size());
    for (const auto& tracked : trackedSignals) {
        stableSignals.push_back(tracked.stable);
        byId.emplace(tracked.stable.id, &tracked);
    }
    auto resolved = resolve(stableSignals, whitelists);
    for (auto& policySignal : resolved) {
        const auto representativeId = policySignal.representativeSignalId;
        const auto found = byId.find(representativeId);
        if (found == byId.end()) continue;
        const auto& source = *found->second;
        policySignal.rawMeasurement = source.raw;
        policySignal.stableMeasurement = source.stable;
        policySignal.boundaryState = source.boundaryState;
        policySignal.pendingBoundaryCount = source.pendingCount;
        policySignal.requiredBoundaryCount = source.requiredCount;
        policySignal.measurementBranch = source.measurementBranch;
        policySignal.associationIou = source.associationIou;
        policySignal.associationCenterDistanceHz = source.centerDistanceHz;
        policySignal.associationBandwidthRatio = source.bandwidthRatio;
        policySignal.boundaryDiagnostic = source.diagnostic;
        policySignal.hasBoundaryMetadata = true;
    }
    return resolved;
}

std::vector<PolicySignal> WhitelistResultResolver::resolve(
    const std::vector<algorithm::DetectionResult::ChannelDetection>& channels,
    const std::vector<WhitelistEntry>& whitelists) const
{
    std::vector<PolicySignal> resolved;
    std::vector<bool> replaced(channels.size(), false);
    const auto attachChannel = [](PolicySignal& target,
                                  const algorithm::DetectionResult::ChannelDetection& channel) {
        target.rawMeasurement = channel.raw;
        target.stableMeasurement = channel.stable;
        target.boundaryState = channel.boundaryState;
        target.measurementBranch = channel.stable.branch;
        target.hasBoundaryMetadata = true;
        target.observationState = channel.observationState;
        target.aggregate = channel.aggregate;
        target.measurementValid = channel.measurementValid;
        target.priorName = channel.priorName;
        target.contributors = channel.contributors;
        target.relatedChannelIds = channel.relatedChannelIds;
    };

    for (const auto& whitelist : whitelists) {
        if (!whitelist.enabled) continue;
        std::vector<std::size_t> observedMatches;
        std::vector<std::size_t> unknownMatches;
        for (std::size_t index = 0; index < channels.size(); ++index) {
            const auto& channel = channels[index];
            if (!intervalsOverlap(channel.stable.startFrequencyHz, channel.stable.endFrequencyHz,
                                  whitelist.startFrequencyHz, whitelist.endFrequencyHz)) continue;
            replaced[index] = true;
            (channel.observationState == algorithm::ObservationState::Observed
                ? observedMatches : unknownMatches).push_back(index);
        }
        const auto& matches = observedMatches.empty() ? unknownMatches : observedMatches;
        if (matches.empty()) continue;
        const auto representative = *std::min_element(matches.begin(), matches.end(), [&](auto lhs, auto rhs) {
            return stronger(channels[lhs].stable, channels[rhs].stable);
        });
        const auto& source = channels[representative];
        PolicySignal signal;
        signal.source = PolicySignalSource::Whitelist;
        signal.id = whitelist.id;
        signal.displayId = "W-" + std::to_string(whitelist.id);
        signal.measurement = source.stable;
        signal.measurement.id = whitelist.id;
        signal.measurement.startFrequencyHz = static_cast<double>(whitelist.startFrequencyHz);
        signal.measurement.endFrequencyHz = static_cast<double>(whitelist.endFrequencyHz);
        signal.measurement.centerFrequencyHz =
            (static_cast<double>(whitelist.startFrequencyHz) +
             static_cast<double>(whitelist.endFrequencyHz)) * 0.5;
        signal.measurement.bandwidthHz = static_cast<double>(whitelist.endFrequencyHz) -
                                         static_cast<double>(whitelist.startFrequencyHz);
        signal.representativeSignalId = source.stable.id;
        signal.originalSignalIds.reserve(matches.size());
        for (const auto index : matches) {
            signal.originalSignalIds.push_back(channels[index].stable.id);
            signal.contributors.insert(signal.contributors.end(), channels[index].contributors.begin(),
                                       channels[index].contributors.end());
        }
        std::sort(signal.originalSignalIds.begin(), signal.originalSignalIds.end());
        signal.originalSignalIds.erase(std::unique(signal.originalSignalIds.begin(),
                                                   signal.originalSignalIds.end()),
                                       signal.originalSignalIds.end());
        signal.whitelistIds.push_back(whitelist.id);
        signal.whitelistNames.push_back(whitelist.name);
        attachChannel(signal, source);
        signal.measurement.id = whitelist.id;
        signal.measurement.startFrequencyHz = static_cast<double>(whitelist.startFrequencyHz);
        signal.measurement.endFrequencyHz = static_cast<double>(whitelist.endFrequencyHz);
        signal.measurement.centerFrequencyHz =
            (static_cast<double>(whitelist.startFrequencyHz) +
             static_cast<double>(whitelist.endFrequencyHz)) * 0.5;
        signal.measurement.bandwidthHz = static_cast<double>(whitelist.endFrequencyHz) -
                                         static_cast<double>(whitelist.startFrequencyHz);
        signal.observationState = observedMatches.empty()
            ? algorithm::ObservationState::TemporarilyUnobserved
            : algorithm::ObservationState::Observed;
        resolved.push_back(std::move(signal));
    }

    for (std::size_t index = 0; index < channels.size(); ++index) {
        if (replaced[index]) continue;
        const auto& channel = channels[index];
        PolicySignal signal;
        signal.source = PolicySignalSource::RawDetection;
        signal.id = channel.stable.id;
        const auto magnitude = channel.stable.id < 0
            ? static_cast<std::uint64_t>(-(channel.stable.id + 1)) + 1
            : static_cast<std::uint64_t>(channel.stable.id);
        signal.displayId = "C-" + std::to_string(magnitude);
        signal.measurement = channel.stable;
        signal.representativeSignalId = channel.stable.id;
        signal.originalSignalIds.push_back(channel.stable.id);
        attachChannel(signal, channel);
        resolved.push_back(std::move(signal));
    }
    return resolved;
}

} // namespace scn::application::policy
