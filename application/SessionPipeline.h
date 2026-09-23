#pragma once
#include "../algorithm/DetectionEngine/DetectionEngine.h"
#include "../runtime/BoundedChannel.h"
#include "policy/PolicyEngine.h"
#include "policy/PolicyTypes.h"
#include "policy/AlarmHistoryStore.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <deque>

namespace scn::application
{
struct PendingFrame
{
    std::uint64_t generation = 0;
    std::shared_ptr<const algorithm::SpectrumFrame> frame;
    std::chrono::steady_clock::time_point acquiredAt;
};

// Internal shared state. UI, source and detector exchange immutable objects;
// the detector thread exclusively owns its engine/context/stream.
class SessionPipeline
{
public:
    using StatusSink = std::function<void(std::uint64_t, std::uint64_t, const std::string&)>;
    explicit SessionPipeline(StatusSink status, std::unique_ptr<algorithm::IScnBackend> backend = {},
                             std::unique_ptr<algorithm::IFfscnBackend> ffscnBackend = {});
    ~SessionPipeline();
    // expectedEpoch prevents a file-loop boundary from superseding a newer UI command.
    // Returns zero if that source operation has already been invalidated.
    std::uint64_t newEpoch(std::uint64_t expectedEpoch = 0);
    bool setActive(std::uint64_t epoch, bool running);
    bool drained(std::uint64_t epoch) const;
    algorithm::ConfigApplyResult configureDetection(const algorithm::DetectionConfig& config);
    algorithm::DetectionConfig detectionConfig() const;
    bool configurePolicy(const policy::PolicyConfig& config, std::string& error);
    policy::PolicyConfig policyConfig() const;
    bool acknowledgeAlarm(const std::string& eventId, const std::string& note);
    bool loadAlarmHistory(std::vector<policy::AlarmEvent>& events, std::string& error) const;
    bool submit(std::shared_ptr<const algorithm::SpectrumFrame> frame, std::uint64_t epoch, bool live);
    void shutdown();

    runtime::BoundedChannel<PendingFrame> queue{8};
    std::atomic<std::uint64_t> generation{1}, configVersion{0};
    // File loops change algorithm epochs, but pause/resume belong to the same UI session.
    std::atomic<std::uint64_t> controlGeneration{1};
    std::atomic<bool> active{false}, busy{false}, closing{false};
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::shared_ptr<const algorithm::SpectrumFrame> latestFrame;
    std::shared_ptr<const algorithm::DetectionResult> latestResult;
    policy::PolicySnapshotPtr latestPolicy;
    std::vector<policy::AlarmEventChange> pendingAlarmChanges;
    std::uint64_t policyRevision = 0;
    std::uint64_t revision = 0;
private:
    void detectLoop();
    algorithm::DetectionConfig m_config;
    policy::PolicyConfig m_policyConfig;
    std::uint64_t m_policyVersion = 0;
    struct PendingAcknowledgement { std::string eventId; std::string note; };
    std::deque<PendingAcknowledgement> m_pendingAcknowledgements;
    bool m_hasCompletedFrame = false;
    std::uint64_t m_completedSequence = 0;
    StatusSink m_status;
    std::unique_ptr<algorithm::IScnBackend> m_backend;
    std::unique_ptr<algorithm::IFfscnBackend> m_ffscnBackend;
    policy::AlarmHistoryStore m_history;
    std::thread m_thread;
};
}
