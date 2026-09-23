#include "DetectionEngine.h"
#include "../preprocess/SpectrumPreprocessor.h"
#include "../refine/CnrRefiner.h"
#include "../fusion/SignalFusion.h"
#include "../detector/FfscnDecoder.h"
#include "../preprocess/FfscnPreprocessor.h"
#include <algorithm>
#include <chrono>
#include <exception>
#include <optional>
#include <utility>

namespace scn::algorithm
{

namespace
{
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point begin)
{ return std::chrono::duration<double, std::milli>(Clock::now() - begin).count(); }
}

DetectionEngine::DetectionEngine(std::unique_ptr<IScnBackend> backend,
                                 std::unique_ptr<IFfscnBackend> ffscnBackend)
    : m_backend(backend ? std::move(backend) : createTensorRtScnBackend()),
      m_ffscnBackend(ffscnBackend ? std::move(ffscnBackend) : createTensorRtFfscnBackend()) {}

std::string DetectionEngine::modelInfo() const
{
    if (m_config.backend == DetectionBackend::Ffscn)
        return m_ffscnBackend ? m_ffscnBackend->modelInfo() : std::string();
    return m_backend ? m_backend->modelInfo() : std::string();
}

bool DetectionEngine::initialize(const DetectionConfig& config)
{
    if (!validateConfig(config, m_lastError)) return false;
    m_initialized = false;
    m_config = config;
    ++m_configVersion;
    reset();
    try {
        if (config.enabled) {
            const bool ready = config.backend == DetectionBackend::Ffscn
                ? m_ffscnBackend && m_ffscnBackend->initialize(config.ffscn, m_lastError)
                : m_backend && m_backend->initialize(config.detector, m_lastError);
            if (!ready) return false;
        }
        m_initialized = true;
        m_lastError.clear();
    } catch (const std::exception& e) { m_lastError = e.what(); }
    return m_initialized;
}

DetectionResult DetectionEngine::process(const SpectrumFrame& frame, const std::function<bool()>& cancelled)
{
    if (m_config.backend == DetectionBackend::Ffscn) return processFfscn(frame, cancelled);
    const auto begin = Clock::now();
    DetectionResult result;
    result.sequence = frame.sequence; result.timestampNs = frame.timestampNs;
    result.generation = m_generation; result.configVersion = m_configVersion;
    result.startFrequencyHz = frame.startFrequencyHz; result.binWidthHz = frame.binWidthHz;
    result.pointCount = frame.powerDb.size(); result.requiredFrames = m_config.accumulator.frames;
    result.referenceLevelDbm = frame.referenceLevelDbm;
    result.resolutionBandwidthHz = frame.resolutionBandwidthHz;
    result.sourceName = frame.sourceName;
    const auto diagnosticFailed = [&](const char* operation, const char* message) noexcept {
        result.diagnostics.exportFailed = true;
        try {
            if (!result.diagnostics.diagnosticError.empty()) result.diagnostics.diagnosticError += "; ";
            result.diagnostics.diagnosticError += operation;
            result.diagnostics.diagnosticError += ": ";
            result.diagnostics.diagnosticError += message;
        } catch (...) {
            // The failure flag survives even when allocating the diagnostic text fails.
        }
    };
    const auto finish = [&]() {
        result.trackingSegment = m_trackingSegment;
        result.diagnostics.processingTimeMs = elapsed(begin);
        try { result.diagnostics.modelInfo = modelInfo(); }
        catch (const std::exception& e) { diagnosticFailed("modelInfo", e.what()); }
        catch (...) { diagnosticFailed("modelInfo", "Unknown exception"); }
        if (m_observer) {
            try { m_observer->fused(result); }
            catch (const std::exception& e) { diagnosticFailed("observer.fused", e.what()); }
            catch (...) { diagnosticFailed("observer.fused", "Unknown exception"); }
        }
        // Diagnostic failures never change the processing stage or committed detections.
        return std::move(result);
    };
    if (!m_config.enabled) { result.diagnostics.message = "SCN detection disabled."; return finish(); }
    if (!m_initialized) {
        result.stage = DetectionStage::Error;
        result.diagnostics.message = m_lastError.empty() ? "SCN engine is not initialized." : m_lastError;
        return finish();
    }
    if (!validateFrame(frame, result.diagnostics.message)) {
        result.stage = DetectionStage::Error;
        return finish();
    }
    const auto isCancelled = [&] { return cancelled && cancelled(); };
    try {
        if (isCancelled()) { result.stage = DetectionStage::Cancelled; return finish(); }
        const bool changed = m_hasGeometry && (m_geometryCount != frame.powerDb.size() ||
            m_geometry.startFrequencyHz != frame.startFrequencyHz || m_geometry.binWidthHz != frame.binWidthHz ||
            m_geometry.referenceLevelDbm != frame.referenceLevelDbm || m_geometry.resolutionBandwidthHz != frame.resolutionBandwidthHz ||
            m_geometry.sourceName != frame.sourceName || frame.timestampNs < m_lastTimestamp || frame.sequence <= m_lastSequence);
        if (changed) reset(m_generation);
        m_hasGeometry = true; m_geometryCount = frame.powerDb.size();
        m_geometry.startFrequencyHz = frame.startFrequencyHz; m_geometry.binWidthHz = frame.binWidthHz;
        m_geometry.referenceLevelDbm = frame.referenceLevelDbm; m_geometry.resolutionBandwidthHz = frame.resolutionBandwidthHz;
        m_geometry.sourceName = frame.sourceName;
        const auto accumulateBegin = Clock::now();
        m_accumulator.push(frame);
        result.accumulatedFrames = m_accumulator.count();
        result.firstSequence = m_accumulator.firstSequence();
        result.firstTimestampNs = m_accumulator.firstTimestampNs();
        result.diagnostics.accumulationTimeMs = elapsed(accumulateBegin);
        if (m_observer) m_observer->accumulated(frame, m_accumulator.average(), m_accumulator.maximum());
        const auto windows = makeWindows(frame.powerDb.size(), m_config.detector.inputLength, m_config.detector.windowStep);
        std::vector<DetectedSignal> all;
        for (const auto branch : {SpectrumBranch::Average, SpectrumBranch::Maximum}) {
            const auto& spectrum = branch == SpectrumBranch::Average ? m_accumulator.average() : m_accumulator.maximum();
            std::vector<DetectedSignal> branchItems;
            for (const auto window : windows) {
                if (isCancelled()) { m_accumulator.rollback(); result.stage = DetectionStage::Cancelled; return finish(); }
                normalizeWindow(spectrum, window, m_config.detector.inputLength, m_normalized);
                const auto inferBegin = Clock::now();
                if (!m_backend->infer(m_normalized, m_modelOutput, m_lastError)) {
                    m_initialized = false;
                    m_accumulator.rollback();
                    result.stage = DetectionStage::Error; result.diagnostics.message = m_lastError;
                    return finish();
                }
                result.diagnostics.inferenceTimeMs += elapsed(inferBegin);
                ++result.diagnostics.windowCount;
                const auto postBegin = Clock::now();
                const auto candidates = decodeScn(m_modelOutput, m_config.detector);
                auto refined = refineCnr(spectrum, window, candidates, frame, branch, m_config.refine.cnrThresholdDb);
                result.diagnostics.candidateCount += candidates.size();
                result.diagnostics.cnrAcceptedCount += refined.size();
                if (m_observer) m_observer->window(frame.sequence, branch, window.start, window.length,
                                                  m_normalized, m_modelOutput, candidates, refined);
                branchItems.insert(branchItems.end(), refined.begin(), refined.end());
                result.diagnostics.postprocessTimeMs += elapsed(postBegin);
            }
            auto merged = fuseSignals(std::move(branchItems), m_config.fusion);
            all.insert(all.end(), merged.begin(), merged.end());
        }
        if (isCancelled()) { m_accumulator.rollback(); result.stage = DetectionStage::Cancelled; return finish(); }
        const auto postBegin = Clock::now();
        result.detections = fuseSignals(std::move(all), m_config.fusion);
        if (result.detections.size() > m_config.maxSignals) {
            result.diagnostics.truncatedCount = result.detections.size() - m_config.maxSignals;
            std::stable_sort(result.detections.begin(), result.detections.end(), [](const auto& a, const auto& b) { return a.confidence > b.confidence; });
            result.detections.resize(m_config.maxSignals);
            std::sort(result.detections.begin(), result.detections.end(), [](const auto& a, const auto& b) { return a.startFrequencyHz < b.startFrequencyHz; });
        }
        // Tracking, stable-band measurement and accumulator commit form one
        // transaction. Work on a tracker copy so cancellation or a measurement
        // failure cannot partially advance identities/history.
        result.diagnostics.message = "SCN average/maximum fusion; CNR is measured in dB.";
        auto nextTracker = m_tracker;
        if (!nextTracker.update(result.detections, result.trackedDetections, frame,
                                m_config.tracker, m_config.maxSignals, cancelled)) {
            m_accumulator.rollback();
            result.detections.clear(); result.trackedDetections.clear();
            result.stage = DetectionStage::Cancelled;
            return finish();
        }
        if (m_config.tracker.boundaryStabilityEnabled) {
            const auto averagePrefix = makePowerPrefix(m_accumulator.average());
            const auto maximumPrefix = makePowerPrefix(m_accumulator.maximum());
            for (auto& tracked : result.trackedDetections) {
                if (isCancelled()) {
                    m_accumulator.rollback();
                    result.detections.clear(); result.trackedDetections.clear();
                    result.stage = DetectionStage::Cancelled;
                    return finish();
                }
                const auto branch = tracked.raw.branch;
                if (branch == SpectrumBranch::Maximum) {
                    tracked.measurementBranch = SpectrumBranch::Maximum;
                    tracked.stable = remeasureBand(maximumPrefix, frame, tracked.stable,
                                                   SpectrumBranch::Maximum);
                } else if (branch == SpectrumBranch::Both) {
                    const auto average = remeasureBand(averagePrefix, frame, tracked.stable,
                                                       SpectrumBranch::Average);
                    const auto maximum = remeasureBand(maximumPrefix, frame, tracked.stable,
                                                       SpectrumBranch::Maximum);
                    // Keep all measurement fields from one branch. A tie favors Average.
                    if (maximum.signalLevelDbm > average.signalLevelDbm) {
                        tracked.measurementBranch = SpectrumBranch::Maximum;
                        tracked.stable = maximum;
                    } else {
                        tracked.measurementBranch = SpectrumBranch::Average;
                        tracked.stable = average;
                    }
                    tracked.stable.branch = SpectrumBranch::Both;
                } else {
                    tracked.measurementBranch = SpectrumBranch::Average;
                    tracked.stable = remeasureBand(averagePrefix, frame, tracked.stable,
                                                   SpectrumBranch::Average);
                }
            }
        } else {
            for (auto& tracked : result.trackedDetections)
                tracked.measurementBranch = tracked.raw.branch;
        }
        if (isCancelled()) {
            m_accumulator.rollback();
            result.detections.clear(); result.trackedDetections.clear();
            result.stage = DetectionStage::Cancelled;
            return finish();
        }
        result.trackingApplied = true;
        m_tracker = std::move(nextTracker);
        m_accumulator.commit();
        m_lastSequence = frame.sequence; m_lastTimestamp = frame.timestampNs;
        result.diagnostics.postprocessTimeMs += elapsed(postBegin);
        result.stage = m_accumulator.count() < m_config.accumulator.frames ? DetectionStage::Accumulating : DetectionStage::Completed;
    } catch (const std::exception& e) {
        m_accumulator.rollback();
        result.detections.clear(); result.trackedDetections.clear(); result.stage = DetectionStage::Error;
        try { result.diagnostics.message = e.what(); }
        catch (...) { result.diagnostics.message.clear(); }
    } catch (...) {
        m_accumulator.rollback();
        result.detections.clear(); result.trackedDetections.clear(); result.stage = DetectionStage::Error;
        try { result.diagnostics.message = "Unknown detection exception."; }
        catch (...) { result.diagnostics.message.clear(); }
    }
    return finish();
}

DetectionResult DetectionEngine::processFfscn(const SpectrumFrame& frame,
                                              const std::function<bool()>& cancelled)
{
    const auto begin = Clock::now();
    DetectionResult result;
    result.sequence = frame.sequence; result.timestampNs = frame.timestampNs;
    result.generation = m_generation; result.configVersion = m_configVersion;
    result.backend = DetectionBackendId::Ffscn;
    result.startFrequencyHz = frame.startFrequencyHz; result.binWidthHz = frame.binWidthHz;
    result.pointCount = frame.powerDb.size(); result.requiredFrames = m_config.ffscn.frameCount;
    result.referenceLevelDbm = frame.referenceLevelDbm;
    result.resolutionBandwidthHz = frame.resolutionBandwidthHz;
    result.sourceName = frame.sourceName;
    const auto finish = [&]() {
        result.trackingSegment = m_trackingSegment;
        result.diagnostics.processingTimeMs = elapsed(begin);
        try { result.diagnostics.modelInfo = modelInfo(); } catch (...) {}
        if (m_observer) {
            try { m_observer->fused(result); } catch (...) { result.diagnostics.exportFailed = true; }
        }
        return std::move(result);
    };
    const auto isCancelled = [&] { return cancelled && cancelled(); };
    if (!m_config.enabled) {
        result.diagnostics.message = "FFSCN detection disabled.";
        return finish();
    }
    if (!m_initialized) {
        result.stage = DetectionStage::Error;
        result.diagnostics.message = m_lastError.empty() ? "FFSCN engine is not initialized." : m_lastError;
        return finish();
    }
    if (!validateFrame(frame, result.diagnostics.message)) {
        result.stage = DetectionStage::Error;
        return finish();
    }
    if (isCancelled()) { result.stage = DetectionStage::Cancelled; return finish(); }

    bool inserted = false;
    bool removedOldest = false;
    SpectrumFrame oldRow;
    const auto rollback = [&]() {
        if (!inserted) return;
        if (!m_ffscnFrames.empty()) m_ffscnFrames.pop_back();
        if (removedOldest) m_ffscnFrames.push_front(std::move(oldRow));
        inserted = false;
    };
    try {
        const bool changed = m_hasGeometry && (m_geometryCount != frame.powerDb.size() ||
            m_geometry.startFrequencyHz != frame.startFrequencyHz || m_geometry.binWidthHz != frame.binWidthHz ||
            m_geometry.referenceLevelDbm != frame.referenceLevelDbm || m_geometry.resolutionBandwidthHz != frame.resolutionBandwidthHz ||
            m_geometry.sourceName != frame.sourceName || frame.timestampNs < m_lastTimestamp || frame.sequence <= m_lastSequence);
        if (changed) reset(m_generation);
        m_hasGeometry = true; m_geometryCount = frame.powerDb.size();
        m_geometry.startFrequencyHz = frame.startFrequencyHz; m_geometry.binWidthHz = frame.binWidthHz;
        m_geometry.referenceLevelDbm = frame.referenceLevelDbm;
        m_geometry.resolutionBandwidthHz = frame.resolutionBandwidthHz;
        m_geometry.sourceName = frame.sourceName;

        const auto accumulationBegin = Clock::now();
        m_ffscnFrames.push_back(frame);
        inserted = true;
        if (m_ffscnFrames.size() > m_config.ffscn.frameCount) {
            oldRow = std::move(m_ffscnFrames.front());
            m_ffscnFrames.pop_front();
            removedOldest = true;
        }
        result.accumulatedFrames = m_ffscnFrames.size();
        result.warmupFrames = result.accumulatedFrames;
        result.diagnostics.accumulationTimeMs = elapsed(accumulationBegin);
        result.firstSequence = m_ffscnFrames.front().sequence;
        result.firstTimestampNs = m_ffscnFrames.front().timestampNs;
        result.windowStartTimestampNs = result.firstTimestampNs;
        result.windowEndTimestampNs = m_ffscnFrames.back().timestampNs;
        for (std::size_t i = 1; i < m_ffscnFrames.size(); ++i) {
            if (m_ffscnFrames[i].sequence > m_ffscnFrames[i - 1].sequence + 1)
                result.diagnostics.missingFrames += m_ffscnFrames[i].sequence - m_ffscnFrames[i - 1].sequence - 1;
        }
        if (m_ffscnFrames.size() < m_config.ffscn.frameCount) {
            m_lastSequence = frame.sequence; m_lastTimestamp = frame.timestampNs;
            result.stage = DetectionStage::WarmingUp;
            result.diagnostics.message = "FFSCN warming up: " + std::to_string(result.accumulatedFrames) + "/10 spectrum rows.";
            return finish();
        }

        std::vector<SpectrumFrame> rows(m_ffscnFrames.begin(), m_ffscnFrames.end());
        const auto windows = makeFfscnWindows(frame, m_config.ffscn.inputLength, m_config.ffscn.windowStep);
        std::vector<std::vector<double>> prefixes;
        prefixes.reserve(rows.size());
        for (const auto& row : rows) prefixes.push_back(makePowerPrefix(row.powerDb));
        struct MeasuredCandidate { FfscnCandidate candidate; DetectedSignal signal; };
        std::vector<MeasuredCandidate> allCandidates;

        const auto measureBestRow = [&](const DetectedSignal& band, bool applyThreshold) {
            DetectedSignal best = band;
            bool haveMeasurement = false;
            for (std::size_t row = 0; row < rows.size(); ++row) {
                auto measured = remeasureBand(prefixes[row], rows[row], band, SpectrumBranch::TemporalWindow);
                if (!std::isfinite(measured.snrDb)) continue;
                if (!haveMeasurement || measured.snrDb > best.snrDb) {
                    best = measured;
                    best.measurementTimestampNs = rows[row].timestampNs;
                    haveMeasurement = true;
                }
            }
            if (!haveMeasurement || (applyThreshold && best.snrDb < m_config.refine.cnrThresholdDb))
                return std::optional<DetectedSignal>{};
            best.branch = SpectrumBranch::TemporalWindow;
            return std::optional<DetectedSignal>{best};
        };

        for (const auto& window : windows) {
            if (isCancelled()) { rollback(); result.stage = DetectionStage::Cancelled; return finish(); }
            prepareFfscnInput(rows, window, m_ffscnNormalized);
            const auto inferenceBegin = Clock::now();
            if (!m_ffscnBackend->infer(m_ffscnNormalized, window.inputLength, m_ffscnOutput, m_lastError)) {
                m_initialized = false;
                rollback();
                result.stage = DetectionStage::Error;
                result.diagnostics.message = m_lastError;
                return finish();
            }
            result.diagnostics.inferenceTimeMs += elapsed(inferenceBegin);
            ++result.diagnostics.windowCount;
            const auto candidates = decodeFfscn(m_ffscnOutput, m_config.ffscn);
            std::vector<DetectedSignal> refined;
            refined.reserve(candidates.size());
            for (const auto& candidate : candidates) {
                const double startHz = window.startFrequencyHz + candidate.beginBin * window.binWidthHz;
                const double endHz = window.startFrequencyHz + candidate.endBin * window.binWidthHz;
                if (!std::isfinite(startHz) || !std::isfinite(endHz) || endHz <= startHz) continue;
                DetectedSignal proposed;
                proposed.startFrequencyHz = startHz;
                proposed.endFrequencyHz = endHz;
                proposed.bandwidthHz = endHz - startHz;
                proposed.centerFrequencyHz = (startHz + endHz) / 2.0;
                proposed.confidence = candidate.confidence;
                proposed.branch = SpectrumBranch::TemporalWindow;
                const auto measured = measureBestRow(proposed, true);
                if (!measured) continue;
                refined.push_back(*measured);
                FfscnCandidate absolute = candidate;
                absolute.beginBin = startHz;
                absolute.endBin = endHz;
                allCandidates.push_back({absolute, *measured});
            }
            result.diagnostics.candidateCount += candidates.size();
            result.diagnostics.cnrAcceptedCount += refined.size();
            if (m_observer) {
                try {
                    m_observer->ffscnWindow(frame.sequence, window.start, window.inputLength,
                        rows, m_ffscnNormalized, m_ffscnOutput, candidates, refined);
                } catch (const std::exception& e) {
                    result.diagnostics.exportFailed = true;
                    if (!result.diagnostics.diagnosticError.empty()) result.diagnostics.diagnosticError += "; ";
                    result.diagnostics.diagnosticError += std::string("observer.ffscnWindow: ") + e.what();
                } catch (...) {
                    result.diagnostics.exportFailed = true;
                    result.diagnostics.diagnosticError = "observer.ffscnWindow: Unknown exception";
                }
            }
        }

        std::stable_sort(allCandidates.begin(), allCandidates.end(), [](const auto& a, const auto& b) {
            return a.signal.confidence != b.signal.confidence
                ? a.signal.confidence > b.signal.confidence
                : a.signal.startFrequencyHz < b.signal.startFrequencyHz;
        });
        for (const auto& candidate : allCandidates) {
            bool suppressed = false;
            for (const auto& kept : result.detections) {
                const double intersection = (std::max)(0.0,
                    (std::min)(candidate.signal.endFrequencyHz, kept.endFrequencyHz) -
                    (std::max)(candidate.signal.startFrequencyHz, kept.startFrequencyHz));
                const double unionWidth = candidate.signal.bandwidthHz + kept.bandwidthHz - intersection;
                if (unionWidth > 0.0 && intersection / unionWidth > m_config.ffscn.nmsIou) {
                    suppressed = true;
                    break;
                }
            }
            if (!suppressed) result.detections.push_back(candidate.signal);
        }
        if (result.detections.size() > m_config.maxSignals) {
            result.diagnostics.truncatedCount = result.detections.size() - m_config.maxSignals;
            result.detections.resize(m_config.maxSignals);
        }
        const auto postBegin = Clock::now();
        result.diagnostics.message = "FFSCN 10-row temporal window; presence means observed within this window; levels use the highest-CNR source row.";
        auto nextTracker = m_tracker;
        if (!nextTracker.update(result.detections, result.trackedDetections, frame,
                                m_config.tracker, m_config.maxSignals, cancelled)) {
            rollback();
            result.detections.clear(); result.trackedDetections.clear();
            result.stage = DetectionStage::Cancelled;
            return finish();
        }
        for (auto& tracked : result.trackedDetections) {
            const auto measured = measureBestRow(tracked.stable, false);
            if (measured) tracked.stable = *measured;
            tracked.measurementBranch = SpectrumBranch::TemporalWindow;
        }
        if (isCancelled()) {
            rollback();
            result.detections.clear(); result.trackedDetections.clear();
            result.stage = DetectionStage::Cancelled;
            return finish();
        }
        result.trackingApplied = true;
        m_tracker = std::move(nextTracker);
        inserted = false;
        m_lastSequence = frame.sequence; m_lastTimestamp = frame.timestampNs;
        result.diagnostics.postprocessTimeMs = elapsed(postBegin);
        result.stage = DetectionStage::Completed;
    } catch (const std::exception& e) {
        rollback();
        result.detections.clear(); result.trackedDetections.clear();
        result.stage = DetectionStage::Error;
        try { result.diagnostics.message = e.what(); } catch (...) {}
    } catch (...) {
        rollback();
        result.detections.clear(); result.trackedDetections.clear();
        result.stage = DetectionStage::Error;
        result.diagnostics.message = "Unknown FFSCN detection exception.";
    }
    return finish();
}

void DetectionEngine::reset(std::uint64_t generation)
{
    m_generation = generation ? generation : m_generation + 1;
    if (++m_trackingSegment == 0) ++m_trackingSegment;
    m_accumulator.reset(m_config.accumulator.frames);
    m_ffscnFrames.clear();
    m_tracker.reset();
    m_hasGeometry = false;
    m_lastSequence = 0; m_lastTimestamp = 0;
}

ConfigApplyResult DetectionEngine::updateConfig(const DetectionConfig& config)
{
    if (sameConfig(m_config, config)) return ConfigApplyResult::Applied;
    const auto change = classifyConfigChange(m_config, config);
    if (change == ConfigApplyResult::Invalid || change == ConfigApplyResult::RequiresRestart) return change;
    m_config = config; ++m_configVersion;
    if (change == ConfigApplyResult::RequiresReset) reset(m_generation);
    else m_tracker.reset(false);
    return change;
}

} // namespace scn::algorithm
