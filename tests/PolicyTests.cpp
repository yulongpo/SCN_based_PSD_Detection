#include "../application/policy/PolicyEngine.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#define CHECK(value) do { if (!(value)) throw std::runtime_error("CHECK failed: " #value); } while (false)

using namespace scn::algorithm;
using namespace scn::application::policy;

namespace
{
DetectedSignal signal(double start, double end, std::int64_t id = 7, float level = -40.0F)
{
    DetectedSignal value;
    value.id = id; value.startFrequencyHz = start; value.endFrequencyHz = end;
    value.centerFrequencyHz = (start + end) / 2.0; value.bandwidthHz = end - start;
    value.signalLevelDbm = level; value.snrDb = 12.0F; value.confidence = .9F;
    return value;
}

DetectionResult result(std::int64_t timestamp, std::vector<DetectedSignal> detections)
{
    DetectionResult value;
    value.generation = 3; value.configVersion = 1; value.sequence = static_cast<std::uint64_t>(timestamp);
    value.timestampNs = timestamp; value.stage = DetectionStage::Accumulating;
    value.sourceName = "TEST";
    for (auto& detection : detections) detection.lastSeenNs = timestamp;
    value.detections = std::move(detections);
    return value;
}
}

int main()
{
    try {
    CHECK(!intervalsOverlap(50, 80, 100, 200));
    CHECK(!intervalsOverlap(220, 250, 100, 200));
    CHECK(intervalsOverlap(50, 120, 100, 200));
    CHECK(intervalsOverlap(120, 180, 100, 200));
    CHECK(intervalsOverlap(80, 220, 100, 200));
    CHECK(intervalsOverlap(80, 100, 100, 200));
    CHECK(intervalsOverlap(200, 220, 100, 200));
    PolicyConfig config;
    config.version = 1;
    config.whitelists.push_back({1, "known", true, 100, 200, ""});
    AlarmRule rule;
    rule.id = 10; rule.name = "critical"; rule.startFrequencyHz = 100; rule.endFrequencyHz = 200;
    rule.level = AlarmLevel::Critical; rule.consecutiveHits = 2; rule.minDurationSeconds = 1.0;
    rule.clearDelaySeconds = 1.0; config.alarmRules.push_back(rule);

    PolicyConfig resolverConfig = config;
    resolverConfig.alarmRules.clear();
    PolicyEngine resolverEngine;
    std::string error; CHECK(resolverEngine.setConfig(resolverConfig, error));
    std::vector<AlarmEventChange> changes;
    auto resolved = resolverEngine.process(result(900000000, {
        signal(110, 120, 7, -50.0F),
        signal(160, 180, 8, -30.0F),
        signal(250, 260, 9, -20.0F)}), changes);
    CHECK(resolved.businessSignals.size() == 2);
    CHECK(resolved.businessSignals[0].source == PolicySignalSource::Whitelist);
    CHECK(resolved.businessSignals[0].id == 1);
    CHECK(resolved.businessSignals[0].measurement.startFrequencyHz == 100.0);
    CHECK(resolved.businessSignals[0].measurement.endFrequencyHz == 200.0);
    CHECK(resolved.businessSignals[0].representativeSignalId == 8);
    CHECK(resolved.businessSignals[0].originalSignalIds.size() == 2);
    CHECK(resolved.businessSignals[0].measurement.signalLevelDbm == -30.0F);
    CHECK(resolved.businessSignals[1].source == PolicySignalSource::RawDetection);
    CHECK(resolved.businessSignals[1].id == 9);

    PolicyConfig overlapping = resolverConfig;
    overlapping.whitelists.push_back({2, "overlap", true, 150, 250, ""});
    PolicyEngine overlappingEngine;
    CHECK(overlappingEngine.setConfig(overlapping, error));
    changes.clear();
    auto overlappingResult = overlappingEngine.process(result(950000000, {signal(180, 190)}), changes);
    CHECK(overlappingResult.businessSignals.size() == 2);
    CHECK(overlappingResult.businessSignals[0].id == 1);
    CHECK(overlappingResult.businessSignals[1].id == 2);

    // The policy path must use stabilized boundaries/remeasured metrics while
    // retaining the original raw observation for auditing.
    PolicyConfig stablePolicy;
    stablePolicy.whitelists.push_back({21, "stable band", true, 100, 200, ""});
    AlarmRule stableRule;
    stableRule.id = 22; stableRule.name = "stable CNR";
    stableRule.startFrequencyHz = 100; stableRule.endFrequencyHz = 200;
    stableRule.useMinSignalLevel = true; stableRule.minSignalLevelDbm = -30.0F;
    stableRule.useMinCnr = true; stableRule.minCnrDb = 10.0F;
    stableRule.level = AlarmLevel::Critical;
    stablePolicy.alarmRules.push_back(stableRule);
    PolicyEngine stableEngine;
    CHECK(stableEngine.setConfig(stablePolicy, error));
    auto stableResult = result(8000000000, {signal(90.0, 95.0, 31, -10.0F)});
    stableResult.trackingSegment = 1;
    DetectionResult::TrackedDetection tracked;
    tracked.raw = signal(90.0, 95.0, 31, -10.0F);
    tracked.stable = signal(110.0, 120.0, 31, -25.0F);
    tracked.stable.snrDb = 18.0F;
    tracked.boundaryState = BoundaryState::PendingChange;
    tracked.pendingCount = 2; tracked.requiredCount = 3;
    tracked.measurementBranch = SpectrumBranch::Maximum;
    tracked.diagnostic = "boundary pending";
    stableResult.trackedDetections.push_back(tracked);
    changes.clear();
    const auto stableSnapshot = stableEngine.process(stableResult, changes);
    CHECK(stableSnapshot.businessSignals.size() == 1);
    const auto& stableBusiness = stableSnapshot.businessSignals.front();
    CHECK(stableBusiness.source == PolicySignalSource::Whitelist);
    CHECK(stableBusiness.measurement.startFrequencyHz == 100.0);
    CHECK(stableBusiness.stableMeasurement.startFrequencyHz == 110.0);
    CHECK(stableBusiness.rawMeasurement.startFrequencyHz == 90.0);
    CHECK(stableBusiness.measurement.signalLevelDbm == -25.0F);
    CHECK(stableBusiness.measurement.snrDb == 18.0F);
    CHECK(stableSnapshot.annotations.front().ruleMatches.front().matched);
    CHECK(stableSnapshot.activeCriticalCount == 1);
    CHECK(stableSnapshot.annotations.front().state == AlarmState::Active);
    CHECK(!changes.empty() && changes.back().event.rawStartFrequencyHz == 90.0);
    CHECK(changes.back().event.stableStartFrequencyHz == 110.0);
    changes.clear();
    stableResult.sequence += 1;
    stableResult.timestampNs += 100000000;
    stableResult.configVersion += 1;
    const auto newConfigSnapshot = stableEngine.process(stableResult, changes);
    CHECK(newConfigSnapshot.activeCriticalCount == 1);
    CHECK(std::any_of(changes.begin(), changes.end(), [](const auto& item) {
        return item.kind == AlarmEventChange::Kind::Ended && item.event.endReason == "检测配置变化";
    }));
    changes.clear();
    stableResult.sequence += 1;
    stableResult.timestampNs += 100000000;
    stableResult.trackingSegment = 2;
    const auto newTrackingSegment = stableEngine.process(stableResult, changes);
    CHECK(newTrackingSegment.activeCriticalCount == 1);
    CHECK(std::any_of(changes.begin(), changes.end(), [](const auto& item) {
        return item.kind == AlarmEventChange::Kind::Ended && item.event.endReason == "检测轨迹分段变化";
    }));

    PolicyEngine engine;
    CHECK(engine.setConfig(config, error));
    auto snapshot = engine.process(result(1000000000, {signal(50, 120)}), changes);
    CHECK(snapshot.annotations.size() == 1 && snapshot.annotations[0].whitelistIds.size() == 1);
    CHECK(snapshot.annotations[0].ruleMatches[0].matched && snapshot.activeEventCount == 0);
    changes.clear();
    snapshot = engine.process(result(1500000000, {signal(50, 120)}), changes);
    CHECK(snapshot.activeEventCount == 0);
    changes.clear();
    snapshot = engine.process(result(2100000000, {signal(50, 120)}), changes);
    CHECK(snapshot.activeCriticalCount == 1 && snapshot.activeEventCount == 1);
    CHECK(std::any_of(changes.begin(), changes.end(), [](const auto& item) {
        return item.kind == AlarmEventChange::Kind::Created;
    }));
    const auto eventId = snapshot.annotations[0].eventId;

    changes.clear();
    snapshot = engine.process(result(2500000000, {}), changes);
    CHECK(snapshot.activeEventCount == 1);
    changes.clear();
    snapshot = engine.process(result(3600000000, {}), changes);
    CHECK(snapshot.activeEventCount == 0);
    CHECK(std::any_of(changes.begin(), changes.end(), [](const auto& item) {
        return item.kind == AlarmEventChange::Kind::Ended;
    }));

    changes.clear();
    snapshot = engine.process(result(4000000000, {signal(190, 220)}), changes);
    CHECK(snapshot.annotations[0].whitelistIds.size() == 1);
    CHECK(snapshot.activeEventCount == 0);
    changes.clear();
    snapshot = engine.process(result(5000000000, {signal(190, 220)}), changes);
    snapshot = engine.process(result(6100000000, {signal(190, 220)}), changes);
    CHECK(snapshot.activeCriticalCount == 1 && snapshot.annotations[0].eventId != eventId);

    changes.clear();
    auto truncated = result(7000000000, {});
    truncated.diagnostics.truncatedCount = 1;
    snapshot = engine.process(truncated, changes);
    CHECK(snapshot.activeEventCount == 1);

    PolicyConfig channelPolicy;
    AlarmRule channelRule;
    channelRule.id = 41; channelRule.name = "channel alarm";
    channelRule.startFrequencyHz = 100; channelRule.endFrequencyHz = 200;
    channelRule.level = AlarmLevel::Critical; channelPolicy.alarmRules.push_back(channelRule);
    PolicyEngine channelEngine;
    CHECK(channelEngine.setConfig(channelPolicy, error));
    const auto channelObservation = [](std::int64_t timestamp, scn::algorithm::ObservationState observation) {
        auto value = result(timestamp, {});
        value.channelAggregationApplied = true;
        DetectionResult::ChannelDetection item;
        item.raw = signal(100, 200, -1, -35.0F);
        item.stable = item.raw;
        item.aggregate = true;
        item.observationState = observation;
        value.channelDetections.push_back(item);
        return value;
    };
    changes.clear();
    auto channelSnapshot = channelEngine.process(channelObservation(8000000000,
        scn::algorithm::ObservationState::Observed), changes);
    CHECK(channelSnapshot.businessSignals.size() == 1 && channelSnapshot.activeCriticalCount == 1);
    CHECK(channelSnapshot.businessSignals[0].displayId == "C-1");
    const auto heldEventId = channelSnapshot.annotations[0].eventId;
    changes.clear();
    channelSnapshot = channelEngine.process(channelObservation(8100000000,
        scn::algorithm::ObservationState::TemporarilyUnobserved), changes);
    CHECK(channelSnapshot.activeCriticalCount == 1 && changes.empty());
    CHECK(channelSnapshot.annotations[0].observationState == scn::algorithm::ObservationState::TemporarilyUnobserved);
    CHECK(!channelSnapshot.annotations[0].ruleMatches[0].known);
    CHECK(channelSnapshot.annotations[0].eventId == heldEventId);
    auto mergedChannel = channelObservation(8200000000,
        scn::algorithm::ObservationState::Observed);
    mergedChannel.channelDetections[0].raw.id = -2;
    mergedChannel.channelDetections[0].stable.id = -2;
    mergedChannel.channelDetections[0].relatedChannelIds.push_back(-1);
    changes.clear();
    channelSnapshot = channelEngine.process(mergedChannel, changes);
    CHECK(channelSnapshot.businessSignals.size() == 1 && channelSnapshot.businessSignals[0].id == -2);
    CHECK(channelSnapshot.activeCriticalCount == 1);
    CHECK(std::any_of(changes.begin(), changes.end(), [&](const auto& item) {
        return item.kind == AlarmEventChange::Kind::Ended && item.event.eventId == heldEventId &&
               item.event.endReason == "信道归并";
    }));
    std::cout << "policy tests passed\n";
    return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
