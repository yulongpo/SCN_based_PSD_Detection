#pragma once
#include "../algorithm/DetectionEngine/DetectionEngine.h"
#include "../runtime/BoundedChannel.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>

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
    explicit SessionPipeline(StatusSink status, std::unique_ptr<algorithm::IScnBackend> backend = {});
    ~SessionPipeline();
    // expectedEpoch prevents a file-loop boundary from superseding a newer UI command.
    // Returns zero if that source operation has already been invalidated.
    std::uint64_t newEpoch(std::uint64_t expectedEpoch = 0);
    bool setActive(std::uint64_t epoch, bool running);
    bool drained(std::uint64_t epoch) const;
    algorithm::ConfigApplyResult configureDetection(const algorithm::DetectionConfig& config);
    algorithm::DetectionConfig detectionConfig() const;
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
    std::uint64_t revision = 0;
private:
    void detectLoop();
    algorithm::DetectionConfig m_config;
    bool m_hasCompletedFrame = false;
    std::uint64_t m_completedSequence = 0;
    StatusSink m_status;
    std::unique_ptr<algorithm::IScnBackend> m_backend;
    std::thread m_thread;
};
}
