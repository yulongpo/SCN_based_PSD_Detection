#include "DetectionEngine.h"
#include "../preprocess/SpectrumPreprocessor.h"
#include "../refine/CnrRefiner.h"
#include "../fusion/SignalFusion.h"
#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace scn::algorithm
{

namespace
{
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point begin)
{ return std::chrono::duration<double, std::milli>(Clock::now() - begin).count(); }
}

DetectionEngine::DetectionEngine(std::unique_ptr<IScnBackend> backend)
    : m_backend(backend ? std::move(backend) : createTensorRtScnBackend()) {}

bool DetectionEngine::initialize(const DetectionConfig& config)
{
    if (!validateConfig(config, m_lastError)) return false;
    m_initialized = false;
    m_config = config;
    ++m_configVersion;
    reset();
    try {
        if (config.enabled && (!m_backend || !m_backend->initialize(config.detector, m_lastError))) return false;
        m_initialized = true;
        m_lastError.clear();
    } catch (const std::exception& e) { m_lastError = e.what(); }
    return m_initialized;
}

DetectionResult DetectionEngine::process(const SpectrumFrame& frame, const std::function<bool()>& cancelled)
{
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
        std::vector<DetectedSignal> allCandidates;
        for (const auto branch : {SpectrumBranch::Average, SpectrumBranch::Maximum}) {
            const auto& spectrum = branch == SpectrumBranch::Average ? m_accumulator.average() : m_accumulator.maximum();
            std::vector<DetectedSignal> branchItems;
            std::vector<DetectedSignal> branchCandidates;
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
                auto unfiltered = refineCnr(spectrum, window, candidates, frame, branch, 0.0F);
                result.diagnostics.candidateCount += candidates.size();
                result.diagnostics.cnrAcceptedCount += refined.size();
                if (m_observer) m_observer->window(frame.sequence, branch, window.start, window.length,
                                                  m_normalized, m_modelOutput, candidates, refined);
                branchItems.insert(branchItems.end(), refined.begin(), refined.end());
                branchCandidates.insert(branchCandidates.end(), unfiltered.begin(), unfiltered.end());
                result.diagnostics.postprocessTimeMs += elapsed(postBegin);
            }
            auto merged = fuseSignals(std::move(branchItems), m_config.fusion);
            all.insert(all.end(), merged.begin(), merged.end());
            auto mergedCandidates = fuseSignals(std::move(branchCandidates), m_config.fusion);
            allCandidates.insert(allCandidates.end(), mergedCandidates.begin(), mergedCandidates.end());
        }
        if (isCancelled()) { m_accumulator.rollback(); result.stage = DetectionStage::Cancelled; return finish(); }
        const auto postBegin = Clock::now();
        result.detections = fuseSignals(std::move(all), m_config.fusion);
        allCandidates = fuseSignals(std::move(allCandidates), m_config.fusion);
        if (result.detections.size() > m_config.maxSignals) {
            result.diagnostics.truncatedCount = result.detections.size() - m_config.maxSignals;
            std::stable_sort(result.detections.begin(), result.detections.end(), [](const auto& a, const auto& b) { return a.confidence > b.confidence; });
            result.detections.resize(m_config.maxSignals);
            std::sort(result.detections.begin(), result.detections.end(), [](const auto& a, const auto& b) { return a.startFrequencyHz < b.startFrequencyHz; });
        }
        if (allCandidates.size() > m_config.maxSignals) {
            result.diagnostics.truncatedCount += allCandidates.size() - m_config.maxSignals;
            std::stable_sort(allCandidates.begin(), allCandidates.end(), [](const auto& a, const auto& b) {
                return a.confidence > b.confidence;
            });
            allCandidates.resize(m_config.maxSignals);
            std::sort(allCandidates.begin(), allCandidates.end(), [](const auto& a, const auto& b) {
                return a.startFrequencyHz < b.startFrequencyHz;
            });
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
            result.stage = isCancelled() ? DetectionStage::Cancelled : DetectionStage::Error;
            result.diagnostics.message = isCancelled() ? "Signal tracking cancelled." : "Signal tracking failed.";
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
        auto nextChannelAggregator = m_channelAggregator;
        if (m_config.channelAggregation.enabled) {
            std::vector<ChannelCandidate> channelCandidates;
            channelCandidates.reserve(allCandidates.size());
            for (const auto& candidate : allCandidates) {
                const bool passedCnr = candidate.snrDb >= m_config.refine.cnrThresholdDb;
                channelCandidates.push_back({candidate, passedCnr});
                result.channelCandidates.push_back({candidate, passedCnr});
            }
            ChannelEvidence evidence;
            const auto aggregationBegin = Clock::now();
            if (!nextChannelAggregator.update(frame, m_accumulator.average(), m_accumulator.maximum(),
                                              channelCandidates, result.trackedDetections,
                                              m_config.channelAggregation, m_config.refine.cnrThresholdDb,
                                              result.diagnostics.truncatedCount == 0,
                                              result.channelDetections, &evidence, cancelled)) {
                m_accumulator.rollback();
                result.detections.clear(); result.trackedDetections.clear(); result.channelDetections.clear();
                result.channelCandidates.clear();
                result.stage = isCancelled() ? DetectionStage::Cancelled : DetectionStage::Error;
                result.diagnostics.message = isCancelled() ? "Channel aggregation cancelled."
                                                           : "Channel aggregation failed.";
                return finish();
            }
            result.diagnostics.channelAggregationTimeMs = elapsed(aggregationBegin);
            result.channelAggregationApplied = true;
            result.channelEvidenceUnitWidthHz = evidence.unitWidthHz;
            result.channelEvidenceHistoryRows = evidence.historyRows;
            result.channelNoiseFloorDbm = std::move(evidence.noiseFloorDbm);
            result.channelEvidenceKnown = std::move(evidence.known);
            result.channelOccupancyMask = std::move(evidence.occupied);
            result.channelGroupingDiagnostics = std::move(evidence.groupingDiagnostics);
            result.diagnostics.aggregateCount = static_cast<std::size_t>(std::count_if(
                result.channelDetections.begin(), result.channelDetections.end(),
                [](const auto& signal) { return signal.aggregate &&
                    signal.observationState == ObservationState::Observed; }));
            result.diagnostics.pendingChannelCount = static_cast<std::size_t>(std::count_if(
                result.channelDetections.begin(), result.channelDetections.end(),
                [](const auto& signal) { return signal.observationState == ObservationState::TemporarilyUnobserved; }));
            std::vector<bool> represented(channelCandidates.size(), false);
            for (const auto& channel : result.channelDetections)
                for (const auto& reference : channel.contributors)
                    if (reference.sequence == frame.sequence && reference.candidateIndex < represented.size())
                        represented[reference.candidateIndex] = true;
            result.diagnostics.channelRejectedCount = static_cast<std::size_t>(
                std::count(represented.begin(), represented.end(), false));
        }
        if (isCancelled()) {
            m_accumulator.rollback();
            result.detections.clear(); result.trackedDetections.clear(); result.channelDetections.clear();
            result.channelCandidates.clear();
            result.channelNoiseFloorDbm.clear(); result.channelEvidenceKnown.clear();
            result.channelOccupancyMask.clear();
            result.channelGroupingDiagnostics.clear();
            result.stage = DetectionStage::Cancelled;
            return finish();
        }
        result.trackingApplied = true;
        m_tracker = std::move(nextTracker);
        if (m_config.channelAggregation.enabled)
            m_channelAggregator = std::move(nextChannelAggregator);
        m_accumulator.commit();
        m_lastSequence = frame.sequence; m_lastTimestamp = frame.timestampNs;
        result.diagnostics.postprocessTimeMs += elapsed(postBegin);
        result.stage = m_accumulator.count() < m_config.accumulator.frames ? DetectionStage::Accumulating : DetectionStage::Completed;
    } catch (const std::exception& e) {
        m_accumulator.rollback();
        result.detections.clear(); result.trackedDetections.clear(); result.channelDetections.clear();
        result.channelCandidates.clear(); result.channelNoiseFloorDbm.clear(); result.channelEvidenceKnown.clear();
        result.channelOccupancyMask.clear(); result.channelGroupingDiagnostics.clear();
        result.stage = DetectionStage::Error;
        try { result.diagnostics.message = e.what(); }
        catch (...) { result.diagnostics.message.clear(); }
    } catch (...) {
        m_accumulator.rollback();
        result.detections.clear(); result.trackedDetections.clear(); result.channelDetections.clear();
        result.channelCandidates.clear(); result.channelNoiseFloorDbm.clear(); result.channelEvidenceKnown.clear();
        result.channelOccupancyMask.clear(); result.channelGroupingDiagnostics.clear();
        result.stage = DetectionStage::Error;
        try { result.diagnostics.message = "Unknown detection exception."; }
        catch (...) { result.diagnostics.message.clear(); }
    }
    return finish();
}

void DetectionEngine::reset(std::uint64_t generation)
{
    m_generation = generation ? generation : m_generation + 1;
    if (++m_trackingSegment == 0) ++m_trackingSegment;
    m_accumulator.reset(m_config.accumulator.frames);
    m_tracker.reset();
    m_channelAggregator.reset();
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
