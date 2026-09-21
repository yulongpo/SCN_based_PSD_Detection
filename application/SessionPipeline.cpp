#include "SessionPipeline.h"
#include "../runtime/PerformanceMonitor.h"

namespace scn::application
{
SessionPipeline::SessionPipeline(StatusSink status, std::unique_ptr<algorithm::IScnBackend> backend)
    : m_status(std::move(status)), m_backend(std::move(backend)), m_thread([this] { detectLoop(); }) {}
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
    latestFrame.reset(); latestResult.reset(); ++revision;
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
    std::uint64_t appliedEpoch = 0, appliedVersion = 0, appliedControl = 0;
    bool initialized = false;
    while (!closing) {
        algorithm::DetectionConfig config;
        std::uint64_t epoch, version, control;
        {
            std::lock_guard<std::mutex> lock(mutex);
            epoch = generation; version = configVersion; control = controlGeneration; config = m_config;
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
            if (epoch != appliedEpoch) { engine.reset(epoch); performance.reset(); }
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
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!cancelled() && result.stage != algorithm::DetectionStage::Cancelled) {
                // Completion is independent of display/config invalidation (notably at FILE EOF).
                m_completedSequence = pending.frame->sequence;
                m_hasCompletedFrame = true;
                if (version == configVersion)
                    latestResult = std::make_shared<const algorithm::DetectionResult>(std::move(result));
                ++revision;
                changed.notify_all();
            }
        }
        busy = false;
    }
}
}
