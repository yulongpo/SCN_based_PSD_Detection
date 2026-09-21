#include "SessionPipeline.h"
#include "../runtime/PerformanceMonitor.h"
#include "policy/PolicyRepository.h"

#include <optional>

namespace scn::application
{
SessionPipeline::SessionPipeline(StatusSink status, std::unique_ptr<algorithm::IScnBackend> backend)
    : m_status(std::move(status)), m_backend(std::move(backend)),
      m_history(policy::PolicyRepository::historyPath()),
      m_thread([this] { detectLoop(); })
{
    std::string error;
    m_history.start(error);
    if (!error.empty() && m_status) m_status(0, 0, "Policy history unavailable: " + error);
}
SessionPipeline::~SessionPipeline() { shutdown(); }
void SessionPipeline::shutdown()
{
    closing = true; queue.close(); changed.notify_all();
    if (m_thread.joinable()) m_thread.join();
}
std::uint64_t SessionPipeline::newEpoch(std::uint64_t expectedEpoch)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (closing || (expectedEpoch && expectedEpoch != generation)) return 0;
    const auto epoch = ++generation;
    if (!expectedEpoch) { active = false; ++controlGeneration; }
    latestFrame.reset(); latestResult.reset(); latestPolicy.reset();
    ++revision; ++policyRevision;
    m_hasCompletedFrame = false;
    // Shared ordering with submit(): no new-epoch frame can be cleared here.
    queue.clear();
    return epoch;
}
bool SessionPipeline::setActive(std::uint64_t epoch, bool running)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (closing || epoch != generation) return false;
    active = running;
    return true;
}
bool SessionPipeline::drained(std::uint64_t epoch) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return epoch == generation && (!latestFrame ||
        (m_hasCompletedFrame && m_completedSequence == latestFrame->sequence));
}
algorithm::DetectionConfig SessionPipeline::detectionConfig() const
{ std::lock_guard<std::mutex> lock(mutex); return m_config; }
bool SessionPipeline::configurePolicy(const policy::PolicyConfig& config, std::string& error)
{
    policy::PolicyEngine validator;
    if (!validator.setConfig(config, error)) return false;
    std::lock_guard<std::mutex> lock(mutex);
    if (config.version <= m_policyConfig.version) {
        m_policyConfig = config;
        m_policyConfig.version = m_policyConfig.version + 1;
    } else {
        m_policyConfig = config;
    }
    ++m_policyVersion;
    ++revision;
    error.clear();
    changed.notify_all();
    return true;
}
policy::PolicyConfig SessionPipeline::policyConfig() const
{ std::lock_guard<std::mutex> lock(mutex); return m_policyConfig; }
bool SessionPipeline::acknowledgeAlarm(const std::string& eventId, const std::string& note)
{
    if (eventId.empty()) return false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (closing) return false;
        m_pendingAcknowledgements.push_back({eventId, note});
        ++revision;
    }
    queue.wake();
    return true;
}
bool SessionPipeline::loadAlarmHistory(std::vector<policy::AlarmEvent>& events, std::string& error) const
{ return m_history.loadEvents(events, error); }
algorithm::ConfigApplyResult SessionPipeline::configureDetection(const algorithm::DetectionConfig& config)
{
    algorithm::ConfigApplyResult change;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (configVersion && algorithm::sameConfig(m_config, config)) return algorithm::ConfigApplyResult::Applied;
        change = algorithm::classifyConfigChange(m_config, config);
        if (change == algorithm::ConfigApplyResult::Invalid ||
            (change == algorithm::ConfigApplyResult::RequiresRestart && active)) return change;
        m_config = config; ++configVersion;
        latestResult.reset(); ++revision;
    }
    queue.wake();
    return change;
}
bool SessionPipeline::submit(std::shared_ptr<const algorithm::SpectrumFrame> frame, std::uint64_t epoch, bool live)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!frame || epoch != generation || closing) return false;
    if (!queue.tryPush({epoch, frame, std::chrono::steady_clock::now()}, live)) return false;
    latestFrame = std::move(frame); ++revision;
    return true;
}
void SessionPipeline::detectLoop()
{
    algorithm::DetectionEngine engine(std::move(m_backend));
    runtime::PerformanceMonitor performance;
    std::uint64_t appliedEpoch = 0, appliedVersion = 0, appliedControl = 0, appliedPolicyVersion = 0;
    bool initialized = false;
    policy::PolicyEngine policyEngine;
    std::uint64_t policySegment = 1;
    while (!closing) {
        algorithm::DetectionConfig config;
        std::uint64_t epoch, version, control, policyVersion;
        policy::PolicyConfig policyConfig;
        {
            std::lock_guard<std::mutex> lock(mutex);
            epoch = generation; version = configVersion; control = controlGeneration; config = m_config;
            policyVersion = m_policyVersion; policyConfig = m_policyConfig;
        }
        if (policyVersion != appliedPolicyVersion) {
            std::string policyError;
            std::vector<policy::AlarmEventChange> changes;
            if (!policyEngine.setConfig(policyConfig, policyError, &changes))
                m_status(control, version, "Policy unavailable: " + policyError);
            if (!changes.empty()) {
                m_history.enqueue(changes);
                std::lock_guard<std::mutex> lock(mutex);
                if (latestPolicy && latestPolicy->generation == epoch) {
                    auto updated = std::make_shared<policy::PolicySnapshot>(*latestPolicy);
                    for (const auto& change : changes) {
                        for (auto& annotation : updated->annotations) {
                            if (annotation.eventId != change.event.eventId) continue;
                            annotation.state = change.event.state;
                            annotation.level = change.event.currentLevel;
                            annotation.acknowledged = change.event.acknowledged;
                            if (change.kind == policy::AlarmEventChange::Kind::Ended) {
                                annotation.eventId.clear();
                                annotation.state = policy::AlarmState::None;
                                annotation.level = policy::AlarmLevel::None;
                            }
                        }
                    }
                    latestPolicy = std::move(updated);
                }
                pendingAlarmChanges.insert(pendingAlarmChanges.end(), changes.begin(), changes.end());
                ++policyRevision; ++revision;
            }
            appliedPolicyVersion = policyVersion;
        }
        std::optional<PendingAcknowledgement> acknowledgement;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!m_pendingAcknowledgements.empty()) {
                acknowledgement = std::move(m_pendingAcknowledgements.front());
                m_pendingAcknowledgements.pop_front();
            }
        }
        if (acknowledgement) {
            std::vector<policy::AlarmEventChange> changes;
            if (policyEngine.acknowledge(acknowledgement->eventId, acknowledgement->note, changes)) {
                m_history.enqueue(changes);
                std::lock_guard<std::mutex> lock(mutex);
                if (latestPolicy && latestPolicy->generation == epoch) {
                    auto updated = std::make_shared<policy::PolicySnapshot>(*latestPolicy);
                    for (auto& annotation : updated->annotations) {
                        if (annotation.eventId == acknowledgement->eventId) annotation.acknowledged = true;
                    }
                    latestPolicy = std::move(updated);
                }
                pendingAlarmChanges.insert(pendingAlarmChanges.end(), changes.begin(), changes.end());
                ++policyRevision; ++revision;
            }
        }
        if (version && (version != appliedVersion || epoch != appliedEpoch)) {
            // A file loop resets history only, even after a model failure. Retry
            // model initialization on an explicit session/configuration change.
            if (!appliedVersion || (!initialized && (version != appliedVersion || control != appliedControl)) || (version != appliedVersion &&
                algorithm::classifyConfigChange(engine.config(), config) == algorithm::ConfigApplyResult::RequiresRestart)) {
                busy = true;
                m_status(control, version, "SCN: loading TensorRT engine...");
                initialized = engine.initialize(config);
                busy = false;
                m_status(control, version, initialized ? (config.enabled ? "SCN ready: " + engine.modelInfo() : "SCN disabled")
                                            : "SCN unavailable: " + engine.lastError());
            } else if (version != appliedVersion) {
                engine.updateConfig(config);
            }
            if (epoch != appliedEpoch) {
                engine.reset(epoch); performance.reset();
                std::vector<policy::AlarmEventChange> changes;
                policyEngine.reset(epoch, ++policySegment, "监测轮次变化", &changes);
                m_history.enqueue(changes);
                std::lock_guard<std::mutex> lock(mutex);
                pendingAlarmChanges.insert(pendingAlarmChanges.end(), changes.begin(), changes.end());
                ++policyRevision;
            }
            appliedEpoch = epoch; appliedVersion = version; appliedControl = control;
        }
        PendingFrame pending;
        const auto cancelled = [&] { return closing || generation != epoch; };
        const auto controlChanged = [&] { return cancelled() || configVersion != version; };
        if (!queue.waitPop(pending, controlChanged)) continue;
        if (!version || pending.generation != epoch || cancelled()) continue;
        busy = true;
        auto result = engine.process(*pending.frame, cancelled);
        initialized = engine.initialized();
        result.generation = epoch; result.configVersion = version;
        if (result.stage == algorithm::DetectionStage::Accumulating ||
            result.stage == algorithm::DetectionStage::Completed)
            performance.recordFrame(result.diagnostics.processingTimeMs);
        result.diagnostics.queueDepth = queue.size();
        result.diagnostics.completedCount = performance.frameCount();
        result.diagnostics.processingP50Ms = performance.percentile(0.5);
        result.diagnostics.processingP95Ms = performance.percentile(0.95);
        result.diagnostics.throughputHz = performance.throughputHz();
        result.diagnostics.resultLatencyMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - pending.acquiredAt).count();
        policy::PolicySnapshot policySnapshot;
        std::vector<policy::AlarmEventChange> alarmChanges;
        if (result.stage == algorithm::DetectionStage::Accumulating ||
            result.stage == algorithm::DetectionStage::Completed) {
            policySnapshot = policyEngine.process(result, alarmChanges);
        }
        m_history.enqueue(alarmChanges);
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!cancelled() && result.stage != algorithm::DetectionStage::Cancelled) {
                // Completion is independent of display/config invalidation (notably at FILE EOF).
                m_completedSequence = pending.frame->sequence;
                m_hasCompletedFrame = true;
                if (version == configVersion)
                    latestResult = std::make_shared<const algorithm::DetectionResult>(std::move(result));
                if (policySnapshot.generation != 0 && policyVersion == m_policyVersion) {
                    latestPolicy = std::make_shared<const policy::PolicySnapshot>(std::move(policySnapshot));
                    ++policyRevision;
                }
                pendingAlarmChanges.insert(pendingAlarmChanges.end(), alarmChanges.begin(), alarmChanges.end());
                ++revision;
                changed.notify_all();
            }
        }
        busy = false;
    }
}
}
