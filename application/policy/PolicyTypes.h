#pragma once

#include "../../algorithm/types/DetectionTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace scn::application::policy
{

enum class AlarmLevel : std::uint8_t
{
    None = 0,
    General = 1,
    Critical = 2
};

enum class AlarmState : std::uint8_t
{
    None = 0,
    Pending = 1,
    Active = 2,
    PendingClear = 3
};

enum class PolicySignalSource : std::uint8_t
{
    RawDetection = 0,
    Whitelist = 1
};

struct WhitelistEntry
{
    std::int64_t id = 0;
    std::string name;
    bool enabled = true;
    double startFrequencyHz = 0.0;
    double endFrequencyHz = 0.0;
    std::string note;
};

struct AlarmRule
{
    std::int64_t id = 0;
    std::string name;
    bool enabled = true;
    double startFrequencyHz = 0.0;
    double endFrequencyHz = 0.0;
    double minBandwidthHz = 0.0;
    double maxBandwidthHz = 0.0;
    bool useMinSignalLevel = false;
    float minSignalLevelDbm = 0.0F;
    bool useMinCnr = false;
    float minCnrDb = 0.0F;
    bool useMinConfidence = false;
    float minConfidence = 0.0F;
    AlarmLevel level = AlarmLevel::General;
    std::uint32_t consecutiveHits = 1;
    double minDurationSeconds = 0.0;
    double clearDelaySeconds = 1.0;
    std::string note;
};

struct PolicyConfig
{
    std::uint64_t version = 1;
    std::vector<WhitelistEntry> whitelists;
    std::vector<AlarmRule> alarmRules;
};

struct RuleMatch
{
    std::int64_t ruleId = 0;
    std::string ruleName;
    AlarmLevel level = AlarmLevel::None;
    bool matched = false;
    std::string reason;
};

struct SignalAnnotation
{
    PolicySignalSource source = PolicySignalSource::RawDetection;
    std::int64_t signalId = 0;
    std::string displayId;
    std::int64_t representativeSignalId = 0;
    std::vector<std::int64_t> originalSignalIds;
    std::vector<std::int64_t> whitelistIds;
    std::vector<std::string> whitelistNames;
    std::int64_t primaryWhitelistId = 0;
    std::string primaryWhitelistName;
    std::vector<RuleMatch> ruleMatches;
    AlarmState state = AlarmState::None;
    AlarmLevel level = AlarmLevel::None;
    bool acknowledged = false;
    std::string eventId;
};

struct PolicySignal
{
    PolicySignalSource source = PolicySignalSource::RawDetection;
    std::int64_t id = 0;
    std::string displayId;
    algorithm::DetectedSignal measurement;
    std::int64_t representativeSignalId = 0;
    std::vector<std::int64_t> originalSignalIds;
    std::vector<std::int64_t> whitelistIds;
    std::vector<std::string> whitelistNames;
};

struct AlarmEvent
{
    std::string eventId;
    std::uint64_t generation = 0;
    std::uint64_t segment = 0;
    PolicySignalSource source = PolicySignalSource::RawDetection;
    std::int64_t signalId = 0;
    std::string displayId;
    std::int64_t representativeSignalId = 0;
    std::vector<std::int64_t> originalSignalIds;
    AlarmLevel currentLevel = AlarmLevel::None;
    AlarmLevel highestLevel = AlarmLevel::None;
    AlarmState state = AlarmState::None;
    bool acknowledged = false;
    std::int64_t firstHitNs = 0;
    std::int64_t lastHitNs = 0;
    std::int64_t triggeredNs = 0;
    std::int64_t endedNs = 0;
    std::string endReason;
    std::string sourceName;
    std::uint64_t policyVersion = 0;
    std::vector<std::int64_t> matchedRuleIds;
    std::vector<std::int64_t> matchedWhitelistIds;
    double startFrequencyHz = 0.0;
    double endFrequencyHz = 0.0;
    double bandwidthHz = 0.0;
    float signalLevelDbm = 0.0F;
    float cnrDb = 0.0F;
    float confidence = 0.0F;
    std::string acknowledgementNote;
    std::int64_t acknowledgedAtMs = 0;
};

struct AlarmEventChange
{
    enum class Kind : std::uint8_t { Created, Updated, Acknowledged, Ended };
    Kind kind = Kind::Updated;
    AlarmEvent event;
    std::uint64_t revision = 0;
    std::int64_t wallClockMs = 0;
};

struct PolicySnapshot
{
    std::uint64_t generation = 0;
    std::uint64_t detectionConfigVersion = 0;
    std::uint64_t sequence = 0;
    std::uint64_t policyVersion = 0;
    std::uint64_t revision = 0;
    // Do not name this member `signals`: Qt defines that token as a keyword macro.
    std::vector<PolicySignal> businessSignals;
    std::vector<SignalAnnotation> annotations;
    std::size_t activeGeneralCount = 0;
    std::size_t activeCriticalCount = 0;
    std::size_t activeEventCount = 0;
};

using PolicySnapshotPtr = std::shared_ptr<const PolicySnapshot>;

inline bool intervalsOverlap(double signalStart, double signalEnd,
                             double policyStart, double policyEnd) noexcept
{
    return signalEnd >= policyStart && signalStart <= policyEnd;
}

inline const char* alarmLevelName(AlarmLevel level) noexcept
{
    switch (level) {
    case AlarmLevel::General: return "General";
    case AlarmLevel::Critical: return "Critical";
    default: return "None";
    }
}

} // namespace scn::application::policy
