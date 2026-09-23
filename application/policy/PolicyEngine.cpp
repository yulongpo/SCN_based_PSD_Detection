#include "PolicyEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace scn::application::policy
{

namespace
{
std::int64_t wallClockMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool sameRuleSemantics(const AlarmRule& a, const AlarmRule& b)
{
    return a.enabled == b.enabled && a.startFrequencyHz == b.startFrequencyHz &&
        a.endFrequencyHz == b.endFrequencyHz && a.minBandwidthHz == b.minBandwidthHz &&
        a.maxBandwidthHz == b.maxBandwidthHz && a.useMinSignalLevel == b.useMinSignalLevel &&
        a.minSignalLevelDbm == b.minSignalLevelDbm && a.useMinCnr == b.useMinCnr &&
        a.minCnrDb == b.minCnrDb && a.useMinConfidence == b.useMinConfidence &&
        a.minConfidence == b.minConfidence && a.level == b.level &&
        a.consecutiveHits == b.consecutiveHits &&
        a.minDurationSeconds == b.minDurationSeconds &&
        a.clearDelaySeconds == b.clearDelaySeconds;
}

bool sameWhitelistSemantics(const WhitelistEntry& a, const WhitelistEntry& b)
{
    return a.enabled == b.enabled && a.startFrequencyHz == b.startFrequencyHz &&
        a.endFrequencyHz == b.endFrequencyHz;
}
}

PolicyEngine::PolicyEngine()
{
    m_config.version = 1;
}

bool PolicyEngine::validateConfig(const PolicyConfig& config, std::string& error) const
{
    for (const auto& item : config.whitelists) {
        if (!std::isfinite(item.startFrequencyHz) || !std::isfinite(item.endFrequencyHz) ||
            item.startFrequencyHz >= item.endFrequencyHz) {
            error = "白名单频率范围无效。";
            return false;
        }
    }
    for (const auto& rule : config.alarmRules) {
        if (!std::isfinite(rule.startFrequencyHz) || !std::isfinite(rule.endFrequencyHz) ||
            rule.startFrequencyHz >= rule.endFrequencyHz || rule.consecutiveHits == 0 ||
            !std::isfinite(rule.minDurationSeconds) || rule.minDurationSeconds < 0.0 ||
            !std::isfinite(rule.clearDelaySeconds) || rule.clearDelaySeconds < 0.0 ||
            rule.minBandwidthHz < 0.0 || rule.maxBandwidthHz < 0.0 ||
            (rule.level != AlarmLevel::General && rule.level != AlarmLevel::Critical) ||
            (rule.maxBandwidthHz > 0.0 && rule.maxBandwidthHz < rule.minBandwidthHz) ||
            (rule.useMinConfidence && (!std::isfinite(rule.minConfidence) ||
                                        rule.minConfidence < 0.0F || rule.minConfidence > 1.0F)) ||
            (rule.useMinCnr && !std::isfinite(rule.minCnrDb)) ||
            (rule.useMinSignalLevel && !std::isfinite(rule.minSignalLevelDbm))) {
            error = "告警规则存在无效频率、带宽、阈值或确认参数。";
            return false;
        }
    }
    return true;
}

bool PolicyEngine::setConfig(const PolicyConfig& requested, std::string& error,
                             std::vector<AlarmEventChange>* changes)
{
    if (!validateConfig(requested, error)) return false;
    PolicyConfig next = requested;
    if (next.version <= m_config.version) next.version = m_config.version + 1;

    std::vector<std::int64_t> changedRuleIds;
    for (const auto& oldRule : m_config.alarmRules) {
        const auto nextIt = std::find_if(next.alarmRules.begin(), next.alarmRules.end(),
            [&](const AlarmRule& rule) { return rule.id == oldRule.id; });
        if (nextIt == next.alarmRules.end() || !sameRuleSemantics(oldRule, *nextIt))
            changedRuleIds.push_back(oldRule.id);
    }
    for (const auto& nextRule : next.alarmRules) {
        const auto oldIt = std::find_if(m_config.alarmRules.begin(), m_config.alarmRules.end(),
            [&](const AlarmRule& rule) { return rule.id == nextRule.id; });
        if (oldIt == m_config.alarmRules.end()) changedRuleIds.push_back(nextRule.id);
    }

    bool whitelistSemanticsChanged = false;
    for (const auto& oldWhitelist : m_config.whitelists) {
        const auto nextIt = std::find_if(next.whitelists.begin(), next.whitelists.end(),
            [&](const WhitelistEntry& item) { return item.id == oldWhitelist.id; });
        if (nextIt == next.whitelists.end() || !sameWhitelistSemantics(oldWhitelist, *nextIt)) {
            whitelistSemanticsChanged = true;
            break;
        }
    }
    if (!whitelistSemanticsChanged) {
        for (const auto& nextWhitelist : next.whitelists) {
            const auto oldIt = std::find_if(m_config.whitelists.begin(), m_config.whitelists.end(),
                [&](const WhitelistEntry& item) { return item.id == nextWhitelist.id; });
            if (oldIt == m_config.whitelists.end()) {
                whitelistSemanticsChanged = true;
                break;
            }
        }
    }
    m_config = std::move(next);

    if (whitelistSemanticsChanged) {
        if (changes) {
            for (auto& item : m_signals) {
                if (item.second.event.state == AlarmState::None) continue;
                item.second.event.state = AlarmState::None;
                item.second.event.currentLevel = AlarmLevel::None;
                item.second.event.endedNs = item.second.event.lastHitNs;
                item.second.event.endReason = "白名单归并";
                emitChange(AlarmEventChange::Kind::Ended, item.second.event, *changes);
            }
        }
        m_signals.clear();
    }
    for (auto it = m_signals.begin(); it != m_signals.end();) {
        auto& rules = it->second.rules;
        for (auto ruleIt = rules.begin(); ruleIt != rules.end();) {
            if (std::find(changedRuleIds.begin(), changedRuleIds.end(), ruleIt->first) != changedRuleIds.end())
                ruleIt = rules.erase(ruleIt);
            else
                ++ruleIt;
        }
        if (changes && !changedRuleIds.empty())
            updateEventLevel(it->second, it->second.event.lastHitNs, *changes);
        if (rules.empty() && it->second.event.state == AlarmState::None)
            it = m_signals.erase(it);
        else
            ++it;
    }
    return true;
}

const AlarmRule* PolicyEngine::findRule(std::int64_t id) const
{
    const auto it = std::find_if(m_config.alarmRules.begin(), m_config.alarmRules.end(),
        [id](const AlarmRule& rule) { return rule.id == id; });
    return it == m_config.alarmRules.end() ? nullptr : &*it;
}

bool PolicyEngine::ruleMatches(const AlarmRule& rule, const algorithm::DetectedSignal& signal) const
{
    if (!rule.enabled || !intervalsOverlap(signal.startFrequencyHz, signal.endFrequencyHz,
                                           rule.startFrequencyHz, rule.endFrequencyHz)) return false;
    if (signal.bandwidthHz < rule.minBandwidthHz) return false;
    if (rule.maxBandwidthHz > 0.0 && signal.bandwidthHz > rule.maxBandwidthHz) return false;
    if (rule.useMinSignalLevel && signal.signalLevelDbm < rule.minSignalLevelDbm) return false;
    if (rule.useMinCnr && signal.snrDb < rule.minCnrDb) return false;
    if (rule.useMinConfidence && signal.confidence < rule.minConfidence) return false;
    return true;
}

void PolicyEngine::emitChange(AlarmEventChange::Kind kind, const AlarmEvent& event,
                              std::vector<AlarmEventChange>& changes)
{
    AlarmEventChange change;
    change.kind = kind;
    change.event = event;
    change.revision = ++m_revision;
    change.wallClockMs = wallClockMs();
    changes.push_back(std::move(change));
}

void PolicyEngine::updateEventLevel(SignalState& state, std::int64_t timestampNs,
                                    std::vector<AlarmEventChange>& changes)
{
    AlarmLevel level = AlarmLevel::None;
    const auto previousRuleIds = state.event.matchedRuleIds;
    state.event.matchedRuleIds.clear();
    for (const auto& item : state.rules) {
        const auto* rule = findRule(item.first);
        if (!rule || (!item.second.active && !item.second.pendingClear &&
                      item.second.consecutiveHits == 0)) continue;
        state.event.matchedRuleIds.push_back(item.first);
        level = std::max(level, rule->level);
    }
    std::sort(state.event.matchedRuleIds.begin(), state.event.matchedRuleIds.end());
    if (level == AlarmLevel::None) {
        if (state.event.state != AlarmState::None) {
            state.event.matchedRuleIds = previousRuleIds;
            state.event.state = AlarmState::None;
            state.event.currentLevel = AlarmLevel::None;
            state.event.endedNs = timestampNs;
            state.event.endReason = "规则解除";
            emitChange(AlarmEventChange::Kind::Ended, state.event, changes);
        }
        state.event.eventId.clear();
        state.event.acknowledged = false;
        state.event.highestLevel = AlarmLevel::None;
        state.event.firstHitNs = 0;
        state.event.triggeredNs = 0;
        return;
    }
    const auto previous = state.event.currentLevel;
    const auto previousState = state.event.state;
    state.event.currentLevel = level;
    state.event.highestLevel = std::max(state.event.highestLevel, level);
    if (level > previous && state.event.acknowledged) {
        state.event.acknowledged = false;
        state.event.acknowledgementNote.clear();
        state.event.acknowledgedAtMs = 0;
    }
    bool hasActive = false;
    bool hasPendingClear = false;
    bool hasPending = false;
    for (const auto& item : state.rules) {
        hasActive = hasActive || item.second.active;
        hasPendingClear = hasPendingClear || item.second.pendingClear;
        hasPending = hasPending || (!item.second.active && !item.second.pendingClear &&
                                    item.second.consecutiveHits > 0);
    }
    state.event.state = hasActive ? AlarmState::Active
        : hasPendingClear ? AlarmState::PendingClear
        : hasPending ? AlarmState::Pending : AlarmState::None;
    if ((previous != level || previousState != state.event.state) && state.event.eventId.size() > 0)
        emitChange(AlarmEventChange::Kind::Updated, state.event, changes);
}

SignalAnnotation PolicyEngine::annotate(PolicySignal& policySignal,
                                         const algorithm::DetectionResult& result,
                                         std::vector<AlarmEventChange>& changes)
{
    const auto& signal = policySignal.measurement;
    const SignalKey key{policySignal.source, policySignal.id};
    if (policySignal.observationState == algorithm::ObservationState::TemporarilyUnobserved) {
        SignalAnnotation unknown;
        unknown.source = policySignal.source;
        unknown.signalId = policySignal.id;
        unknown.displayId = policySignal.displayId;
        unknown.representativeSignalId = policySignal.representativeSignalId;
        unknown.originalSignalIds = policySignal.originalSignalIds;
        unknown.whitelistIds = policySignal.whitelistIds;
        unknown.whitelistNames = policySignal.whitelistNames;
        unknown.observationState = policySignal.observationState;
        if (!policySignal.whitelistIds.empty()) {
            unknown.primaryWhitelistId = policySignal.whitelistIds.front();
            unknown.primaryWhitelistName = policySignal.whitelistNames.empty()
                ? std::string{} : policySignal.whitelistNames.front();
        }
        for (const auto& rule : m_config.alarmRules) {
            if (!rule.enabled) continue;
            RuleMatch match;
            match.ruleId = rule.id; match.ruleName = rule.name; match.level = rule.level;
            match.known = false;
            match.reason = "信道当前未观测，规则状态冻结";
            unknown.ruleMatches.push_back(std::move(match));
        }
        const auto existing = m_signals.find(key);
        if (existing != m_signals.end()) {
            unknown.state = existing->second.event.state;
            unknown.level = existing->second.event.currentLevel;
            unknown.acknowledged = existing->second.event.acknowledged;
            unknown.eventId = existing->second.event.eventId;
            for (auto& ruleState : existing->second.rules) ruleState.second.continuityBroken = true;
        }
        return unknown;
    }
    auto& state = m_signals[key];
    const std::int64_t observationTime = signal.lastSeenNs != 0
        ? signal.lastSeenNs : result.timestampNs;
    ++state.observationCount;
    if (state.firstObservationNs == 0) state.firstObservationNs = observationTime;
    state.lastObservationNs = observationTime;
    policySignal.measurement.firstSeenNs = state.firstObservationNs;
    policySignal.measurement.lastSeenNs = state.lastObservationNs;
    policySignal.measurement.occurrenceCount = state.observationCount;
    if (state.event.eventId.empty()) {
        state.event.generation = result.generation;
        state.event.segment = m_segment;
        state.event.source = policySignal.source;
        state.event.signalId = policySignal.id;
        state.event.displayId = policySignal.displayId;
        state.event.representativeSignalId = policySignal.representativeSignalId;
        state.event.sourceName = result.sourceName;
        state.event.policyVersion = m_config.version;
    }
    state.event.originalSignalIds = policySignal.originalSignalIds;
    state.event.matchedWhitelistIds = policySignal.whitelistIds;
    state.event.startFrequencyHz = signal.startFrequencyHz;
    state.event.endFrequencyHz = signal.endFrequencyHz;
    state.event.bandwidthHz = signal.bandwidthHz;
    state.event.signalLevelDbm = signal.signalLevelDbm;
    state.event.cnrDb = signal.snrDb;
    state.event.confidence = signal.confidence;
    if (policySignal.hasBoundaryMetadata) {
        state.event.rawStartFrequencyHz = policySignal.rawMeasurement.startFrequencyHz;
        state.event.rawEndFrequencyHz = policySignal.rawMeasurement.endFrequencyHz;
        state.event.stableStartFrequencyHz = policySignal.stableMeasurement.startFrequencyHz;
        state.event.stableEndFrequencyHz = policySignal.stableMeasurement.endFrequencyHz;
        state.event.boundaryState = policySignal.boundaryState;
        state.event.pendingBoundaryCount = static_cast<std::uint32_t>(policySignal.pendingBoundaryCount);
        state.event.requiredBoundaryCount = static_cast<std::uint32_t>(policySignal.requiredBoundaryCount);
        state.event.measurementBranch = policySignal.measurementBranch;
        state.event.hasBoundaryMetadata = true;
    } else {
        state.event.hasBoundaryMetadata = false;
    }
    SignalAnnotation annotation;
    annotation.source = policySignal.source;
    annotation.signalId = policySignal.id;
    annotation.displayId = policySignal.displayId;
    annotation.representativeSignalId = policySignal.representativeSignalId;
    annotation.originalSignalIds = policySignal.originalSignalIds;
    annotation.whitelistIds = policySignal.whitelistIds;
    annotation.whitelistNames = policySignal.whitelistNames;
    annotation.observationState = policySignal.observationState;
    if (!policySignal.whitelistIds.empty()) {
        annotation.primaryWhitelistId = policySignal.whitelistIds.front();
        annotation.primaryWhitelistName = policySignal.whitelistNames.empty()
            ? std::string{} : policySignal.whitelistNames.front();
    }
    for (const auto& rule : m_config.alarmRules) {
        if (!rule.enabled) continue;
        const bool known = !rule.useMinCnr || policySignal.measurementValid;
        const bool matched = ruleMatches(rule, signal);
        RuleMatch match;
        match.ruleId = rule.id; match.ruleName = rule.name; match.level = rule.level;
        match.matched = matched;
        match.known = known;
        match.reason = !known ? "CNR 测量无效，规则状态冻结" : matched ? "条件满足" : "条件不满足";
        annotation.ruleMatches.push_back(match);
        auto& ruleState = state.rules[rule.id];
        if (!known) {
            ruleState.continuityBroken = true;
            continue;
        }
        const auto previousObservationNs = ruleState.lastObservationNs;
        const bool continuous = !ruleState.continuityBroken && previousObservationNs > 0 &&
            observationTime > previousObservationNs &&
            m_currentIntervalContinuous && observationTime - previousObservationNs <= m_currentResultIntervalNs + 1;
        const auto validDeltaNs = continuous ? observationTime - previousObservationNs : 0;
        ruleState.lastObservationNs = observationTime;
        // A known observation after startup or a data gap becomes the anchor
        // for the next continuous interval; only unknown observations keep the
        // continuity break latched.
        ruleState.continuityBroken = false;
        ruleState.lastMatched = matched;
        if (matched) {
            ruleState.pendingClear = false;
            ruleState.clearStartNs = 0;
            ruleState.clearDurationNs = 0;
            if (!ruleState.active) {
                if (ruleState.consecutiveHits == 0) {
                    ruleState.firstHitNs = observationTime;
                    ruleState.matchedDurationNs = 0;
                } else if (validDeltaNs > 0) {
                    ruleState.matchedDurationNs += validDeltaNs;
                }
                ++ruleState.consecutiveHits;
                const double duration = static_cast<double>(ruleState.matchedDurationNs) / 1e9;
                if (ruleState.consecutiveHits >= rule.consecutiveHits &&
                    duration >= rule.minDurationSeconds) {
                    ruleState.active = true;
                    if (state.event.eventId.empty()) {
                        std::ostringstream id;
                        id << result.generation << '-' << m_segment << '-' << m_nextEventId++;
                        state.event.eventId = id.str();
                        state.event.firstHitNs = ruleState.firstHitNs;
                        state.event.triggeredNs = observationTime;
                        state.event.acknowledged = false;
                        emitChange(AlarmEventChange::Kind::Created, state.event, changes);
                    }
                    ruleState.eventId = state.event.eventId;
                }
            }
        } else if (ruleState.active) {
            if (!ruleState.pendingClear) {
                ruleState.pendingClear = true;
                ruleState.clearStartNs = observationTime;
                ruleState.clearDurationNs = 0;
            } else if (validDeltaNs > 0) {
                ruleState.clearDurationNs += validDeltaNs;
            }
            const double clearDuration = static_cast<double>(ruleState.clearDurationNs) / 1e9;
            if (clearDuration >= rule.clearDelaySeconds) {
                ruleState.active = false;
                ruleState.pendingClear = false;
                ruleState.consecutiveHits = 0;
                ruleState.firstHitNs = 0;
                ruleState.matchedDurationNs = 0;
                ruleState.clearDurationNs = 0;
            }
        } else {
            ruleState.consecutiveHits = 0;
            ruleState.firstHitNs = 0;
            ruleState.matchedDurationNs = 0;
        }
    }
    state.event.lastHitNs = observationTime;
    updateEventLevel(state, observationTime, changes);
    annotation.state = state.event.state;
    annotation.level = state.event.currentLevel;
    annotation.acknowledged = state.event.acknowledged;
    annotation.eventId = state.event.eventId;
    return annotation;
}

void PolicyEngine::clearMissingSignals(const std::vector<SignalKey>& observed,
                                       const algorithm::DetectionResult& result,
                                       std::vector<AlarmEventChange>& changes)
{
    if (result.diagnostics.truncatedCount > 0) {
        for (auto& item : m_signals)
            if (std::find(observed.begin(), observed.end(), item.first) == observed.end())
                for (auto& ruleState : item.second.rules) ruleState.second.continuityBroken = true;
        return;
    }
    for (auto& item : m_signals) {
        if (std::find(observed.begin(), observed.end(), item.first) != observed.end()) continue;
        for (auto& ruleState : item.second.rules) {
            const auto* rule = findRule(ruleState.first);
            ruleState.second.lastMatched = false;
            if (!rule) continue;
            if (!ruleState.second.active) {
                ruleState.second.pendingClear = false;
                ruleState.second.consecutiveHits = 0;
                ruleState.second.firstHitNs = 0;
                ruleState.second.matchedDurationNs = 0;
                continue;
            }
            if (!ruleState.second.pendingClear) {
                ruleState.second.pendingClear = true;
                ruleState.second.clearStartNs = result.timestampNs;
                ruleState.second.clearDurationNs = 0;
            } else if (!ruleState.second.continuityBroken && m_currentIntervalContinuous) {
                ruleState.second.clearDurationNs += m_currentResultIntervalNs;
            }
            ruleState.second.lastObservationNs = result.timestampNs;
            ruleState.second.continuityBroken = !m_currentIntervalContinuous;
            if (static_cast<double>(ruleState.second.clearDurationNs) / 1e9 >= rule->clearDelaySeconds) {
                ruleState.second.active = false;
                ruleState.second.pendingClear = false;
                ruleState.second.consecutiveHits = 0;
                ruleState.second.firstHitNs = 0;
                ruleState.second.matchedDurationNs = 0;
                ruleState.second.clearDurationNs = 0;
            }
        }
        updateEventLevel(item.second, result.timestampNs, changes);
    }
}

void PolicyEngine::reset(std::uint64_t generation, std::uint64_t segment,
                         const std::string& reason, std::vector<AlarmEventChange>* changes)
{
    if (changes) {
        for (auto& item : m_signals) {
            if (item.second.event.state == AlarmState::None) continue;
            item.second.event.state = AlarmState::None;
            item.second.event.currentLevel = AlarmLevel::None;
            item.second.event.endedNs = item.second.event.lastHitNs;
            item.second.event.endReason = reason;
            emitChange(AlarmEventChange::Kind::Ended, item.second.event, *changes);
        }
    }
    m_signals.clear();
    m_generation = generation;
    m_detectionConfigVersion = 0;
    m_trackingSegment = 0;
    m_segment = segment;
    m_lastResultTimestampNs = 0;
    m_currentResultIntervalNs = 0;
    m_currentIntervalContinuous = false;
    m_resultIntervalsNs.clear();
}

PolicySnapshot PolicyEngine::process(const algorithm::DetectionResult& result,
                                     std::vector<AlarmEventChange>& changes)
{
    if (result.generation != m_generation) {
        reset(result.generation, m_segment + 1, "检测轮次变化", &changes);
    } else if (result.trackingSegment != 0 && m_trackingSegment != 0 &&
               result.trackingSegment != m_trackingSegment) {
        reset(result.generation, m_segment + 1, "检测轨迹分段变化", &changes);
    } else if (m_detectionConfigVersion != 0 &&
               result.configVersion != m_detectionConfigVersion) {
        reset(result.generation, m_segment + 1, "检测配置变化", &changes);
    }
    m_detectionConfigVersion = result.configVersion;
    m_trackingSegment = result.trackingSegment;
    PolicySnapshot snapshot;
    snapshot.generation = result.generation;
    snapshot.detectionConfigVersion = result.configVersion;
    snapshot.sequence = result.sequence;
    snapshot.policyVersion = m_config.version;
    if (result.stage != algorithm::DetectionStage::Accumulating &&
        result.stage != algorithm::DetectionStage::Completed) return snapshot;
    std::int64_t medianIntervalNs = 0;
    if (!m_resultIntervalsNs.empty()) {
        auto sortedIntervals = std::vector<std::int64_t>(m_resultIntervalsNs.begin(), m_resultIntervalsNs.end());
        const auto middle = sortedIntervals.begin() + static_cast<std::ptrdiff_t>(sortedIntervals.size() / 2);
        std::nth_element(sortedIntervals.begin(), middle, sortedIntervals.end());
        medianIntervalNs = *middle;
    }
    const auto scaledMedian = medianIntervalNs > 0 &&
        medianIntervalNs <= std::numeric_limits<std::int64_t>::max() / 3
            ? medianIntervalNs * 3 : std::int64_t{1'000'000'000};
    const auto maxIntervalNs = std::max<std::int64_t>(1'000'000'000, scaledMedian);
    m_currentResultIntervalNs = m_lastResultTimestampNs > 0 &&
        result.timestampNs > m_lastResultTimestampNs
            ? result.timestampNs - m_lastResultTimestampNs : 0;
    m_currentIntervalContinuous = m_currentResultIntervalNs > 0 &&
                                  m_currentResultIntervalNs <= maxIntervalNs;
    if (m_lastResultTimestampNs > 0 && result.timestampNs <= m_lastResultTimestampNs) {
        m_resultIntervalsNs.clear();
        m_currentIntervalContinuous = false;
    } else if (m_currentIntervalContinuous) {
        m_resultIntervalsNs.push_back(m_currentResultIntervalNs);
        while (m_resultIntervalsNs.size() > 16) m_resultIntervalsNs.pop_front();
    } else if (m_lastResultTimestampNs > 0) {
        m_resultIntervalsNs.clear();
    }
    m_lastResultTimestampNs = result.timestampNs;
    const auto policySignals = result.channelAggregationApplied
        ? m_resolver.resolve(result.channelDetections, m_config.whitelists)
        : result.trackingApplied || !result.trackedDetections.empty()
            ? m_resolver.resolve(result.trackedDetections, m_config.whitelists)
            : m_resolver.resolve(result.detections, m_config.whitelists);
    for (const auto& signal : policySignals) {
        const auto& predecessors = signal.source == PolicySignalSource::Whitelist
            ? signal.originalSignalIds : signal.relatedChannelIds;
        const std::string reason = signal.source == PolicySignalSource::Whitelist
            ? "白名单归并" : signal.aggregate ? "信道归并" : "信道拆分";
        for (const auto predecessorId : predecessors) {
            for (auto it = m_signals.begin(); it != m_signals.end();) {
                if (it->first.id != predecessorId ||
                    (it->first.source == signal.source && it->first.id == signal.id)) {
                    ++it;
                    continue;
                }
                auto& event = it->second.event;
                if (event.state != AlarmState::None) {
                    event.state = AlarmState::None;
                    event.currentLevel = AlarmLevel::None;
                    event.endedNs = result.timestampNs;
                    event.endReason = reason;
                    emitChange(AlarmEventChange::Kind::Ended, event, changes);
                }
                it = m_signals.erase(it);
            }
        }
    }
    std::vector<SignalKey> observed;
    observed.reserve(policySignals.size());
    snapshot.businessSignals = policySignals;
    snapshot.annotations.reserve(policySignals.size());
    for (auto& signal : snapshot.businessSignals) {
        observed.push_back(SignalKey{signal.source, signal.id});
        snapshot.annotations.push_back(annotate(signal, result, changes));
    }
    clearMissingSignals(observed, result, changes);
    for (const auto& item : m_signals) {
        if (item.second.event.state != AlarmState::Active &&
            item.second.event.state != AlarmState::PendingClear) continue;
        ++snapshot.activeEventCount;
        if (item.second.event.currentLevel == AlarmLevel::Critical)
            ++snapshot.activeCriticalCount;
        else if (item.second.event.currentLevel == AlarmLevel::General)
            ++snapshot.activeGeneralCount;
    }
    snapshot.revision = m_revision;
    return snapshot;
}

bool PolicyEngine::acknowledge(const std::string& eventId, const std::string& note,
                               std::vector<AlarmEventChange>& changes)
{
    for (auto& item : m_signals) {
        if (item.second.event.eventId != eventId || item.second.event.state == AlarmState::None) continue;
        item.second.event.acknowledged = true;
        item.second.event.acknowledgementNote = note;
        item.second.event.acknowledgedAtMs = wallClockMs();
        emitChange(AlarmEventChange::Kind::Acknowledged, item.second.event, changes);
        return true;
    }
    return false;
}

} // namespace scn::application::policy
