#pragma once

#include "PolicyTypes.h"
#include "WhitelistResultResolver.h"

#include <chrono>
#include <map>
#include <optional>

namespace scn::application::policy
{

class PolicyEngine final
{
public:
    PolicyEngine();

    bool setConfig(const PolicyConfig& config, std::string& error,
                   std::vector<AlarmEventChange>* changes = nullptr);
    const PolicyConfig& config() const noexcept { return m_config; }
    void reset(std::uint64_t generation, std::uint64_t segment,
               const std::string& reason, std::vector<AlarmEventChange>* changes = nullptr);

    PolicySnapshot process(const algorithm::DetectionResult& result,
                           std::vector<AlarmEventChange>& changes);
    bool acknowledge(const std::string& eventId, const std::string& note,
                     std::vector<AlarmEventChange>& changes);

private:
    struct RuleState
    {
        std::uint32_t consecutiveHits = 0;
        std::int64_t firstHitNs = 0;
        std::int64_t lastObservationNs = 0;
        std::int64_t clearStartNs = 0;
        bool active = false;
        bool pendingClear = false;
        bool lastMatched = false;
        std::string eventId;
    };

    struct SignalState
    {
        AlarmEvent event;
        std::map<std::int64_t, RuleState> rules;
        std::uint64_t observationCount = 0;
        std::int64_t firstObservationNs = 0;
        std::int64_t lastObservationNs = 0;
    };

    struct SignalKey
    {
        PolicySignalSource source = PolicySignalSource::RawDetection;
        std::int64_t id = 0;

        bool operator<(const SignalKey& other) const noexcept
        {
            if (source != other.source)
                return static_cast<std::uint8_t>(source) < static_cast<std::uint8_t>(other.source);
            return id < other.id;
        }

        bool operator==(const SignalKey& other) const noexcept
        {
            return source == other.source && id == other.id;
        }
    };

    bool validateConfig(const PolicyConfig& config, std::string& error) const;
    bool ruleMatches(const AlarmRule& rule, const algorithm::DetectedSignal& signal) const;
    SignalAnnotation annotate(PolicySignal& signal,
                              const algorithm::DetectionResult& result,
                              std::vector<AlarmEventChange>& changes);
    void emitChange(AlarmEventChange::Kind kind, const AlarmEvent& event,
                    std::vector<AlarmEventChange>& changes);
    void updateEventLevel(SignalState& state, std::int64_t timestampNs,
                          std::vector<AlarmEventChange>& changes);
    const AlarmRule* findRule(std::int64_t id) const;
    void clearMissingSignals(const std::vector<SignalKey>& observed,
                             const algorithm::DetectionResult& result,
                             std::vector<AlarmEventChange>& changes);

    PolicyConfig m_config;
    std::uint64_t m_generation = 0;
    std::uint64_t m_segment = 0;
    std::uint64_t m_revision = 0;
    std::uint64_t m_nextEventId = 1;
    std::map<SignalKey, SignalState> m_signals;
    WhitelistResultResolver m_resolver;
};

} // namespace scn::application::policy
