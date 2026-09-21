#include "WhitelistResultResolver.h"

#include <algorithm>
#include <string>

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
        result.measurement.id = whitelist.id;
        result.measurement.startFrequencyHz = whitelist.startFrequencyHz;
        result.measurement.endFrequencyHz = whitelist.endFrequencyHz;
        result.measurement.centerFrequencyHz =
            (whitelist.startFrequencyHz + whitelist.endFrequencyHz) / 2.0;
        result.measurement.bandwidthHz = whitelist.endFrequencyHz - whitelist.startFrequencyHz;
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
        result.representativeSignalId = rawSignals[index].id;
        result.originalSignalIds.push_back(rawSignals[index].id);
        resolved.push_back(std::move(result));
    }
    return resolved;
}

} // namespace scn::application::policy
