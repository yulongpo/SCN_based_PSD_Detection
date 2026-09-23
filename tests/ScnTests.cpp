#include "../algorithm/DetectionEngine/DetectionEngine.h"
#include "../algorithm/preprocess/SpectrumPreprocessor.h"
#include "../algorithm/preprocess/FfscnPreprocessor.h"
#include "../algorithm/detector/FfscnDecoder.h"
#include "../algorithm/refine/CnrRefiner.h"
#include "../algorithm/fusion/SignalFusion.h"
#include "../algorithm/detector/ScnSha256.h"
#include "../application/SessionPipeline.h"
#include "../source/FileSource/FileSource.h"
#include "../runtime/BoundedChannel.h"
#include "../common/Frequency.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " CHECK " #expression); } while(false)
namespace
{
using namespace scn::algorithm;
struct Gate
{
    std::mutex mutex; std::condition_variable cv;
    unsigned gateCount = 2, enteredCount = 0, releasedThrough = 0;
};
class FakeBackend final : public IScnBackend
{
public:
    explicit FakeBackend(std::shared_ptr<Gate> gate = {}) : m_gate(std::move(gate)) {}
    bool initialize(const DetectorConfig&, std::string& error) override { error.clear(); return true; }
    bool infer(const std::vector<float>& input, ScnModelOutput& out, std::string&) override
    {
        CHECK(input.size() == 32768);
        if (m_gate) {
            std::unique_lock<std::mutex> lock(m_gate->mutex);
            if (m_gate->enteredCount < m_gate->gateCount) {
                const auto gateIndex = ++m_gate->enteredCount;
                m_gate->cv.notify_all();
                if (!m_gate->cv.wait_for(lock, std::chrono::seconds(5), [&] {
                    return m_gate->releasedThrough >= gateIndex;
                })) return false;
            }
        }
        out.heatmap.assign(8192, 0); out.bandwidth.assign(8192, 0); out.offset.assign(8192, 0);
        out.heatmap[100] = .9F; out.bandwidth[100] = 20; // [360,440) bins.
        return true;
    }
    std::string modelInfo() const override { return "test-only deterministic backend"; }
private:
    std::shared_ptr<Gate> m_gate;
};
class FakeFfscnBackend final : public IFfscnBackend
{
public:
    bool initialize(const FfscnConfig&, std::string& error) override { error.clear(); return true; }
    bool infer(const std::vector<float>& input, std::size_t width,
               FfscnModelOutput& output, std::string& error) override
    {
        CHECK(input.size() == width * 10);
        widths.push_back(width);
        error.clear(); output = {}; output.inputLength = width;
        output.heatmap.assign(width / 4, 0.0F);
        output.bandwidth.assign(width / 4, 0.0F);
        output.offset.assign(width / 4, 0.0F);
        output.heatmap[100] = 0.95F; output.bandwidth[100] = 20.0F;
        return true;
    }
    std::string modelInfo() const override { return "test-only FFSCN backend"; }
    std::vector<std::size_t> widths;
};
SpectrumFrame frame(std::uint64_t sequence, std::size_t length = 512)
{
    SpectrumFrame f;
    f.sequence = sequence; f.timestampNs = static_cast<std::int64_t>(sequence * 10000000);
    f.startFrequencyHz = 100; f.binWidthHz = 2; f.referenceLevelDbm = -20;
    f.sourceName = "TEST"; f.powerDb.assign(length, -100);
    if (length >= 440) std::fill(f.powerDb.begin() + 360, f.powerDb.begin() + 440, -50.0F);
    return f;
}
SpectrumFrame shortSpectrumFrame(std::uint64_t sequence)
{
    auto value = frame(sequence, 10000);
    std::fill(value.powerDb.begin(), value.powerDb.end(), -100.0F);
    return value;
}
DetectedSignal signal(double first, double last)
{
    DetectedSignal s; s.startFrequencyHz = first; s.endFrequencyHz = last;
    s.centerFrequencyHz = first + (last - first) / 2; s.bandwidthHz = last - first;
    s.signalLevelDbm = -50; s.noiseLevelDbm = -100; s.confidence = .9F; return s;
}
void algorithmTests()
{
    std::string error;
    auto f = frame(1);
    CHECK(validateFrame(f, error));
    f.powerDb[0] = std::numeric_limits<float>::quiet_NaN(); CHECK(!validateFrame(f, error));
    f = frame(1); f.startFrequencyHz = 1e16; f.binWidthHz = .1; CHECK(!validateFrame(f, error));
    CHECK(makeWindows(1, 32768, 16384).size() == 1);
    for (const auto n : {32767U, 32768U, 32769U, 202242U}) {
        const auto windows = makeWindows(n, 32768, 16384);
        CHECK(windows.front().start == 0);
        CHECK(windows.back().start + windows.back().length == n);
        for (std::size_t i = 1; i < windows.size(); ++i) CHECK(windows[i].start <= windows[i-1].start + windows[i-1].length);
    }
    CHECK(makeWindows(202242, 32768, 16384).size() == 12);
    std::vector<float> normalized;
    normalizeWindow({-80, -20}, {0, 2}, 32768, normalized);
    CHECK(normalized[0] == 0 && normalized[1] == 1 && normalized.back() == 0);
    normalizeWindow({-80}, {0, 1}, 32768, normalized);
    CHECK(std::all_of(normalized.begin(), normalized.end(), [](float p) { return p == 0; }));

    CHECK(ffscnInputLengthFor(1000) == 8192);
    CHECK(ffscnInputLengthFor(10000) == 8192);
    CHECK(ffscnInputLengthFor(13000) == 16384);
    CHECK(ffscnInputLengthFor(12288) == 16384); // nearest-width tie rounds upward
    auto shortSpectrum = frame(1, 10000);
    shortSpectrum.powerDb.assign(10000, -100.0F);
    auto ffWindows = makeFfscnWindows(shortSpectrum);
    CHECK(ffWindows.size() == 1 && ffWindows[0].inputLength == 8192);
    CHECK(std::abs(ffWindows[0].binWidthHz * ffWindows[0].inputLength - shortSpectrum.binWidthHz * 10000) < 1e-8);
    std::vector<SpectrumFrame> tenRows;
    for (std::uint64_t i = 0; i < 10; ++i) {
        auto row = shortSpectrum;
        row.sequence = i + 1; row.timestampNs = static_cast<std::int64_t>(i + 1) * 1000;
        std::fill(row.powerDb.begin(), row.powerDb.end(), static_cast<float>(i + 1));
        tenRows.push_back(std::move(row));
    }
    prepareFfscnInput(tenRows, ffWindows[0], normalized);
    CHECK(normalized.size() == 10 * 8192);
    CHECK(normalized[0] < 0 && normalized[8191] == normalized[0]);
    CHECK(normalized[9 * 8192] > 0 && std::abs(normalized[9 * 8192] + normalized[0]) < 1e-5F);
    CHECK(makeFfscnWindows(frame(1, 200000)).back().start + 131072 == 200000);

    FfscnConfig ffConfig;
    FfscnModelOutput ffOutput;
    ffOutput.inputLength = 8192;
    ffOutput.heatmap.assign(2048, 0); ffOutput.bandwidth.assign(2048, 0); ffOutput.offset.assign(2048, 0);
    ffOutput.heatmap[10] = 0.7F; // Confidence comparison is strict.
    ffOutput.heatmap[100] = 0.95F; ffOutput.bandwidth[100] = 2.0F; ffOutput.offset[100] = 0.5F;
    const auto ffCandidates = decodeFfscn(ffOutput, ffConfig);
    CHECK(ffCandidates.size() == 1 && ffCandidates[0].beginBin == 398 && ffCandidates[0].endBin == 406);
    const auto nmsCandidates = suppressFfscnCandidates({{0,10,.9F,0},{5,15,.8F,1},{11,20,.7F,2}}, .3F, 10);
    CHECK(nmsCandidates.size() == 2 && nmsCandidates[0].peakIndex == 0 && nmsCandidates[1].peakIndex == 2);

    TemporalAccumulator acc; acc.reset(16);
    for (int i = 1; i <= 17; ++i) { auto item = frame(i, 1); item.powerDb[0] = static_cast<float>(i); acc.push(item); }
    CHECK(acc.count() == 16 && acc.firstSequence() == 2);
    CHECK(acc.average()[0] == 9.5F && acc.maximum()[0] == 17);
    acc.rollback(); CHECK(acc.count() == 16 && acc.average()[0] == 8.5F && acc.maximum()[0] == 16);
    acc.reset(2);
    f = frame(1, 1); f.powerDb[0] = 100; acc.push(f); acc.commit();
    f.sequence = 2; f.powerDb[0] = 1; acc.push(f); acc.commit();
    f.sequence = 3; f.powerDb[0] = 2; acc.push(f); CHECK(acc.maximum()[0] == 2); acc.rollback(); CHECK(acc.maximum()[0] == 100);

    ScnModelOutput out;
    out.heatmap.assign(8192, 0); out.bandwidth.assign(8192, 0); out.offset.assign(8192, 0);
    out.heatmap[100] = .9F; out.bandwidth[100] = 20;
    out.heatmap[105] = .8F; out.bandwidth[105] = 20;
    auto candidates = decodeScn(out, {}); CHECK(candidates.size() == 1 && candidates[0].beginBin == 360);
    f = frame(1);
    auto refined = refineCnr(f.powerDb, {0,512}, candidates, f, SpectrumBranch::Average, 3);
    CHECK(refined.size() == 1 && refined[0].startFrequencyHz == 820 && refined[0].endFrequencyHz == 980);
    CHECK(refined[0].snrDb == 50);
    CHECK(refineCnr(f.powerDb, {0,512}, {{800,900,.9F,200}}, f, SpectrumBranch::Average, 3).empty());
    CHECK(refineCnr({-80}, {0,1}, {{0,1,.9F,0}}, frame(1,1), SpectrumBranch::Average, 3).empty());
    CHECK(refineCnr({-80}, {0,1}, {{0,1,.9F,0}}, frame(1,1), SpectrumBranch::Average, 0).size() == 1);

    auto fakeFfscn = std::make_unique<FakeFfscnBackend>();
    auto* fakeFfscnPtr = fakeFfscn.get();
    DetectionEngine ffEngine(std::make_unique<FakeBackend>(), std::move(fakeFfscn));
    DetectionConfig ffEngineConfig;
    ffEngineConfig.backend = DetectionBackend::Ffscn;
    ffEngineConfig.refine.cnrThresholdDb = -1000.0F;
    CHECK(ffEngine.initialize(ffEngineConfig));
    DetectionResult ffResult;
    for (std::uint64_t i = 1; i <= 9; ++i) {
        ffResult = ffEngine.process(shortSpectrumFrame(i));
        CHECK(ffResult.stage == DetectionStage::WarmingUp && ffResult.accumulatedFrames == i);
        CHECK(ffResult.detections.empty() && !ffResult.trackingApplied);
    }
    ffResult = ffEngine.process(shortSpectrumFrame(10));
    CHECK(ffResult.stage == DetectionStage::Completed && ffResult.backend == DetectionBackendId::Ffscn);
    CHECK(ffResult.firstSequence == 1 && ffResult.windowStartTimestampNs < ffResult.windowEndTimestampNs);
    CHECK(ffResult.trackingApplied && !ffResult.detections.empty());
    CHECK(fakeFfscnPtr->widths.size() == 1 && fakeFfscnPtr->widths[0] == 8192);
    auto a = signal(0,100), b = signal(50,150); b.branch = SpectrumBranch::Maximum;
    auto fused = fuseSignals({a,b}, {}); CHECK(fused.size() == 1 && fused[0].endFrequencyHz == 150 && fused[0].branch == SpectrumBranch::Both);
    CHECK(fuseSignals({signal(0,10),signal(11,20)}, {}).size() == 2);
    FusionConfig gap; gap.gapHz = 1; CHECK(fuseSignals({signal(0,10),signal(11,20)}, gap).size() == 1);
    // Intentional reference policy: compare sorted intervals with the last merged group.
    CHECK(fuseSignals({signal(0,100), signal(91,191), signal(92,101)}, {}).size() == 2);
    a.signalLevelDbm = 3e38F; a.noiseLevelDbm = 2e38F;
    b.signalLevelDbm = -1.6875e38F; b.noiseLevelDbm = -2.6875e38F;
    bool cnrOverflow = false;
    try { (void)fuseSignals({a,b}, {}); } catch (const std::overflow_error&) { cnrOverflow = true; }
    CHECK(cnrOverflow);

    SignalTracker tracker;
    std::vector<DetectedSignal> observation{signal(0,10)};
    tracker.update(observation, 0, {}, 1); const auto firstId = observation[0].id;
    observation = {signal(100,110)}; tracker.update(observation, 0, {}, 1); const auto secondId = observation[0].id;
    CHECK(firstId != secondId);
    observation = {signal(100,110)}; tracker.update(observation, 1, {}, 1);
    CHECK(observation[0].id == secondId && observation[0].occurrenceCount == 2);
    observation = {signal(100,110)}; tracker.update(observation, 1000000002LL, {}, 1); CHECK(observation[0].id != secondId);

    // Short-window median + EMA suppress alternating edge noise without
    // delaying the first observation.
    tracker.reset();
    TrackerConfig stableConfig;
    std::vector<DetectionResult::TrackedDetection> tracked;
    auto gridFrame = [](std::uint64_t sequence) {
        auto value = frame(sequence, 512);
        value.startFrequencyHz = 0.0;
        value.binWidthHz = 1.0;
        value.powerDb.assign(512, -90.0F);
        value.timestampNs = static_cast<std::int64_t>(sequence) * 10000000;
        return value;
    };
    auto stableFrame = gridFrame(1);
    observation = {signal(100, 200)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    const auto stableId = tracked[0].stable.id;
    CHECK(tracked[0].raw.startFrequencyHz == 100 && tracked[0].stable.startFrequencyHz == 100);
    double rawCenterSquared = 0.0, stableCenterSquared = 0.0;
    for (std::uint64_t sequence = 2; sequence <= 9; ++sequence) {
        const double delta = sequence % 2 ? 2.0 : -2.0;
        stableFrame = gridFrame(sequence);
        observation = {signal(100 + delta, 200 + delta)};
        CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
        CHECK(tracked[0].stable.id == stableId);
        const double rawDelta = tracked[0].raw.centerFrequencyHz - 150.0;
        const double stableDelta = tracked[0].stable.centerFrequencyHz - 150.0;
        rawCenterSquared += rawDelta * rawDelta;
        stableCenterSquared += stableDelta * stableDelta;
    }
    CHECK(stableCenterSquared <= rawCenterSquared * 0.25);

    // A tenfold contraction is held for two distinct frames and accepted on 3.
    tracker.reset();
    stableFrame = gridFrame(1); observation = {signal(100, 200)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    const auto jumpId = tracked[0].stable.id;
    for (std::uint64_t sequence = 2; sequence <= 3; ++sequence) {
        stableFrame = gridFrame(sequence); observation = {signal(120, 130)};
        CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
        CHECK(tracked[0].stable.id == jumpId);
        CHECK(tracked[0].stable.startFrequencyHz == 100);
        CHECK(tracked[0].boundaryState == BoundaryState::PendingChange);
        CHECK(tracked[0].pendingCount == sequence - 1);
    }
    stableFrame = gridFrame(4); observation = {signal(120, 130)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    CHECK(tracked[0].stable.id == jumpId && tracked[0].stable.startFrequencyHz == 120);
    CHECK(tracked[0].boundaryState == BoundaryState::Stable);

    // Returning to the established band cancels a one-frame jump candidate.
    tracker.reset();
    stableFrame = gridFrame(1); observation = {signal(100, 200)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    stableFrame = gridFrame(2); observation = {signal(120, 130)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    const auto recoveryId = tracked[0].stable.id;
    stableFrame = gridFrame(3); observation = {signal(100, 200)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    CHECK(tracked[0].stable.id == recoveryId && tracked[0].stable.startFrequencyHz == 100);
    CHECK(tracked[0].boundaryState == BoundaryState::Stable);

    // Split/merge-style non-unique jumps are not forced onto either old ID.
    tracker.reset();
    stableFrame = gridFrame(1); observation = {signal(80, 180), signal(120, 220)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    stableFrame = gridFrame(2); observation = {signal(100, 200)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    CHECK(tracked.size() == 1 && tracked[0].boundaryState == BoundaryState::Ambiguous);
    CHECK(tracked[0].diagnostic.find("不唯一") != std::string::npos);

    // A cancelled candidate update does not mutate identity/history state.
    tracker.reset();
    stableFrame = gridFrame(1); observation = {signal(100, 200)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    stableFrame = gridFrame(2); observation = {signal(120, 130)};
    CHECK(!tracker.update(observation, tracked, stableFrame, stableConfig, 8, [] { return true; }));
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    CHECK(tracked[0].boundaryState == BoundaryState::PendingChange && tracked[0].pendingCount == 1);

    // Frame gaps and inconsistent candidate bands restart confirmation.
    tracker.reset();
    stableFrame = gridFrame(1); observation = {signal(100, 200)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    stableFrame = gridFrame(2); observation = {signal(120, 130)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    stableFrame = gridFrame(4); observation = {signal(120, 130)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    CHECK(tracked[0].pendingCount == 1);
    stableFrame = gridFrame(5); observation = {signal(125, 135)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    CHECK(tracked[0].pendingCount == 1);
    stableFrame = gridFrame(6); observation.clear();
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    stableFrame = gridFrame(7); observation = {signal(125, 135)};
    CHECK(tracker.update(observation, tracked, stableFrame, stableConfig, 8));
    CHECK(tracked[0].pendingCount == 1);

    // More than one 64-edge batch per observation: identical dense intervals
    // require deterministic ID-order matching all the way through a refill.
    tracker.reset();
    std::vector<DetectedSignal> dense(96, signal(0,100));
    CHECK(tracker.update(dense, 0, {}, 96));
    std::vector<DetectedSignal> current(96, signal(0,100));
    int cancelChecks = 0;
    CHECK(!tracker.update(current, 1, {}, 96, [&] { return ++cancelChecks >= 50; }));
    CHECK(std::all_of(current.begin(), current.end(), [](const auto& s) { return s.id == 0 && s.occurrenceCount == 0; }));
    CHECK(tracker.update(current, 1, {}, 96));
    for (std::size_t i = 0; i < current.size(); ++i)
        CHECK(current[i].id == dense[i].id && current[i].occurrenceCount == 2);

    DetectionEngine engine(std::make_unique<FakeBackend>()); DetectionConfig config;
    CHECK(engine.initialize(config));
    auto result = engine.process(frame(1));
    CHECK(result.stage == DetectionStage::Accumulating && result.accumulatedFrames == 1 && result.detections.size() == 1);
    CHECK(result.trackedDetections.size() == 1);
    CHECK(result.trackedDetections[0].raw.startFrequencyHz == result.detections[0].startFrequencyHz);
    CHECK(result.trackedDetections[0].stable.startFrequencyHz == result.detections[0].startFrequencyHz);
    CHECK(result.trackedDetections[0].stable.signalLevelDbm == -50.0F);
    CHECK(result.trackedDetections[0].stable.snrDb == 50.0F);
    CHECK(result.trackedDetections[0].measurementBranch == SpectrumBranch::Average);
    CHECK(result.referenceLevelDbm == -20 && result.sourceName == "TEST");
    const auto id = result.detections[0].id;
    CHECK(engine.updateConfig(config) == ConfigApplyResult::Applied);
    result = engine.process(frame(2)); CHECK(result.detections[0].id == id && result.detections[0].occurrenceCount == 2);
    int checks = 0;
    result = engine.process(frame(3), [&] { return ++checks == 2; }); CHECK(result.stage == DetectionStage::Cancelled);
    result = engine.process(frame(3)); CHECK(result.accumulatedFrames == 3 && result.detections[0].occurrenceCount == 3);
    for (int i = 4; i <= 17; ++i) result = engine.process(frame(i));
    CHECK(result.stage == DetectionStage::Completed && result.accumulatedFrames == 16 && result.firstSequence == 2);
    engine.reset(); result = engine.process(frame(1)); CHECK(result.accumulatedFrames == 1 && result.detections[0].occurrenceCount == 1);
    auto invalid = config; invalid.accumulator.frames = 0; CHECK(engine.updateConfig(invalid) == ConfigApplyResult::Invalid);
    auto changed = config; changed.detector.modelPath = "another.engine";
    CHECK(engine.updateConfig(changed) == ConfigApplyResult::RequiresRestart && engine.config().detector.modelPath == config.detector.modelPath);
    config.accumulator.frames = 2; CHECK(engine.updateConfig(config) == ConfigApplyResult::RequiresReset);
    result = engine.process(frame(2)); CHECK(result.accumulatedFrames == 1);
    struct FailedExport final : DetectionObserver {
        void fused(const DetectionResult&) override { throw std::runtime_error("simulated diagnostic write failure"); }
    } failedExport;
    engine.setObserver(&failedExport);
    result = engine.process(frame(3));
    CHECK(result.stage == DetectionStage::Completed && result.diagnostics.exportFailed && !result.diagnostics.diagnosticError.empty());
    CHECK(result.detections[0].occurrenceCount == 2);
    engine.setObserver(nullptr);
    result = engine.process(frame(4));
    CHECK(result.stage == DetectionStage::Completed && !result.diagnostics.exportFailed && result.detections[0].occurrenceCount == 3);
    auto rawMode = engine.config();
    rawMode.tracker.boundaryStabilityEnabled = false;
    CHECK(engine.updateConfig(rawMode) == ConfigApplyResult::RequiresReset);
    result = engine.process(frame(5));
    CHECK(result.stage == DetectionStage::Accumulating && result.trackedDetections.size() == 1);
    CHECK(result.trackedDetections[0].boundaryState == BoundaryState::Disabled);
    CHECK(result.trackedDetections[0].stable.startFrequencyHz == result.trackedDetections[0].raw.startFrequencyHz);
}
void channelTests()
{
    scn::runtime::BoundedChannel<int> queue(2);
    CHECK(queue.tryPush(1, false) && queue.tryPush(2, false)); CHECK(!queue.tryPush(3, false));
    CHECK(queue.tryPush(3, true) && queue.dropped() == 1);
    int value = 0; CHECK(queue.waitPop(value, [] { return false; }) && value == 2);
    CHECK(queue.waitPop(value, [] { return false; }) && value == 3);
    auto pending = std::async(std::launch::async, [&] { int next; return queue.waitPop(next, [] { return false; }); });
    queue.close(); CHECK(pending.wait_for(std::chrono::seconds(2)) == std::future_status::ready && !pending.get());
}
void pipelineTests()
{
    auto gate = std::make_shared<Gate>();
    scn::application::SessionPipeline pipeline([](auto, auto, const auto&) {}, std::make_unique<FakeBackend>(gate));
    CHECK(pipeline.configureDetection({}) == ConfigApplyResult::Applied);
    const auto oldEpoch = pipeline.newEpoch();
    CHECK(pipeline.submit(std::make_shared<const SpectrumFrame>(frame(1)), oldEpoch, false));
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        CHECK(gate->cv.wait_for(lock, std::chrono::seconds(3), [&] { return gate->enteredCount == 1; }));
    }
    const auto epoch = pipeline.newEpoch();
    CHECK(pipeline.newEpoch(oldEpoch) == 0 && pipeline.generation == epoch);
    CHECK(!pipeline.setActive(oldEpoch, true));
    CHECK(pipeline.submit(std::make_shared<const SpectrumFrame>(frame(2)), epoch, false));
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        gate->releasedThrough = 1; gate->cv.notify_all();
        CHECK(gate->cv.wait_for(lock, std::chrono::seconds(3), [&] { return gate->enteredCount == 2; }));
    }
    { std::lock_guard<std::mutex> lock(pipeline.mutex); CHECK(!pipeline.latestResult); }
    CHECK(!pipeline.drained(epoch)); // Old completion cannot advance the new epoch.
    { std::lock_guard<std::mutex> lock(gate->mutex); gate->releasedThrough = 2; gate->cv.notify_all(); }
    {
        std::unique_lock<std::mutex> lock(pipeline.mutex);
        CHECK(pipeline.changed.wait_for(lock, std::chrono::seconds(5), [&] {
            return pipeline.latestResult && pipeline.latestResult->generation == epoch && pipeline.latestResult->sequence == 2;
        }));
        CHECK(pipeline.latestResult->accumulatedFrames == 1);
    }
    const auto version = pipeline.configVersion.load();
    CHECK(pipeline.configureDetection({}) == ConfigApplyResult::Applied && pipeline.configVersion == version);
    CHECK(pipeline.drained(epoch));
    auto threshold = DetectionConfig{}; threshold.refine.cnrThresholdDb = 4;
    CHECK(pipeline.configureDetection(threshold) == ConfigApplyResult::Applied);
    CHECK(pipeline.drained(epoch)); // EOF completion survives display-result invalidation.
    { std::lock_guard<std::mutex> lock(pipeline.mutex); CHECK(!pipeline.latestResult); }
    const auto appliedVersion = pipeline.configVersion.load();
    pipeline.active = true;
    auto changed = DetectionConfig{}; changed.detector.deviceIndex = 1;
    CHECK(pipeline.configureDetection(changed) == ConfigApplyResult::RequiresRestart && pipeline.configVersion == appliedVersion);
    changed = threshold; changed.enabled = false;
    CHECK(pipeline.configureDetection(changed) == ConfigApplyResult::RequiresRestart && pipeline.configVersion == appliedVersion);
    const auto controlEpoch = pipeline.controlGeneration.load();
    const auto loopEpoch = pipeline.newEpoch(epoch);
    CHECK(loopEpoch != 0 && pipeline.controlGeneration == controlEpoch && pipeline.active);
    CHECK(pipeline.newEpoch() != 0 && pipeline.controlGeneration != controlEpoch && !pipeline.active);
    pipeline.shutdown(); CHECK(pipeline.closing);
}
void sourceTests()
{
    const auto name = "scn_test_" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto dir = std::filesystem::temp_directory_path() / name;
    CHECK(std::filesystem::create_directory(dir));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{dir};
    const auto path = dir / "fixture_Fc=1000_Bw=512_Rbw=1_Reflevel=-20.5_SpectrumLen=512.dat";
    {
        std::ofstream file(path, std::ios::binary);
        for (int i = 0; i < 2; ++i) { const auto f = frame(i); file.write(reinterpret_cast<const char*>(f.powerDb.data()), f.powerDb.size()*sizeof(float)); }
        CHECK(file.good());
    }
    scn::source::FileSource source; scn::source::SourceConfig config; std::string error;
    config.filePath = path.u8string(); config.frameRateHz = 40; config.loopFile = true;
    CHECK(source.open(config, error) && source.start() && source.frameCount() == 2);
    SpectrumFrame first, second, loop;
    CHECK(source.read(first) && source.read(second) && source.read(loop));
    CHECK(first.startFrequencyHz == 744 && first.binWidthHz == 1 && first.endFrequencyHz() == 1256);
    CHECK(first.referenceLevelDbm == -20.5 && first.timestampNs == 0 && second.timestampNs == 25000000 && loop.timestampNs == 0);
    CHECK(loop.sequence > second.sequence);
    CHECK(source.seekFrame(1, error)); CHECK(source.read(second) && second.timestampNs == 25000000 && second.sequence == 1);
    CHECK(!source.seekFrame(2, error));
}
void hashTests()
{
    CHECK(scn::algorithm::detail::sha256(nullptr, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string abc = "abc";
    CHECK(scn::algorithm::detail::sha256(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
void frequencyTests()
{
    using scn::common::FrequencyHz;
    FrequencyHz value = 0;
    CHECK(scn::common::parseFrequencyHz("2.4GHz", value) && value == 2400000000LL);
    CHECK(scn::common::parseFrequencyHz(" 2.4 ghz ", value) && value == 2400000000LL);
    CHECK(scn::common::parseFrequencyHz("2400M", value) && value == 2400000000LL);
    CHECK(scn::common::parseFrequencyHz("50KHZ", value) && value == 50000);
    CHECK(scn::common::parseFrequencyHz("9k", value) && value == 9000);
    CHECK(scn::common::parseFrequencyHz("50", value) && value == 50);
    CHECK(scn::common::parseFrequencyHz("1.5 Hz", value) && value == 2);
    CHECK(scn::common::parseFrequencyHz("-1.5Hz", value) && value == -2);
    CHECK(!scn::common::parseFrequencyHz("12xyz", value));
    CHECK(!scn::common::parseFrequencyHz("nan Hz", value));
    CHECK(!scn::common::parseFrequencyHz("1e309GHz", value));
    CHECK(!scn::common::toIntegerHz(std::ldexp(1.0L, 63), value));
    CHECK(scn::common::formatFrequencyHz(999) == "999 Hz");
    CHECK(scn::common::formatFrequencyHz(1001) == "1.001 kHz");
    CHECK(scn::common::formatFrequencyHz(1500000) == "1.5 MHz");
    CHECK(scn::common::formatFrequencyHz(2400000000LL) == "2.4 GHz");
    CHECK(scn::common::formatFrequencyHz(1000000001.0) == "1.000000001 GHz");
}
}

// Test executable only: resolves both runtime factories without linking/loading CUDA.
// Production executables use the TensorRT backend factories from the algorithm library.
namespace scn::algorithm
{
std::unique_ptr<IScnBackend> createTensorRtScnBackend() { return std::make_unique<FakeBackend>(); }
std::unique_ptr<IFfscnBackend> createTensorRtFfscnBackend() { return std::make_unique<FakeFfscnBackend>(); }
}
int main(int argc, char** argv)
{
    try {
        const std::string group = argc > 1 ? argv[1] : "all";
        if (group == "algorithm" || group == "all") algorithmTests();
        if (group == "channel" || group == "all") channelTests();
        if (group == "pipeline" || group == "all") pipelineTests();
        if (group == "source" || group == "all") sourceTests();
        if (group == "hash" || group == "all") hashTests();
        if (group == "frequency" || group == "all") frequencyTests();
        std::cout << group << ": passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
