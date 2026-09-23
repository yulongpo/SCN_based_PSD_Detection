#pragma once

#include "../DetectionConfig.h"
#include "../types/DetectionTypes.h"
#include "../types/SpectrumTypes.h"
#include "../accumulation/TemporalAccumulator.h"
#include "../tracking/SignalTracker.h"
#include "../diagnostics/DetectionObserver.h"
#include "../detector/IFfscnBackend.h"
#include <deque>
#include <functional>

namespace scn::algorithm
{

class DetectionEngine
{
public:
    explicit DetectionEngine(std::unique_ptr<IScnBackend> backend = {},
                             std::unique_ptr<IFfscnBackend> ffscnBackend = {});

    bool initialize(const DetectionConfig& config);
    DetectionResult process(const SpectrumFrame& frame,
                            const std::function<bool()>& cancelled = {});
    void reset(std::uint64_t generation = 0);
    ConfigApplyResult updateConfig(const DetectionConfig& config);
    void setObserver(DetectionObserver* observer) noexcept { m_observer = observer; }
    const std::string& lastError() const noexcept { return m_lastError; }
    bool initialized() const noexcept { return m_initialized; }
    std::string modelInfo() const;

    [[nodiscard]] const DetectionConfig& config() const noexcept { return m_config; }

private:
    DetectionResult processFfscn(const SpectrumFrame& frame,
                                 const std::function<bool()>& cancelled);
    DetectionConfig m_config{};
    bool m_initialized = false;
    std::unique_ptr<IScnBackend> m_backend;
    std::unique_ptr<IFfscnBackend> m_ffscnBackend;
    std::deque<SpectrumFrame> m_ffscnFrames;
    std::vector<float> m_ffscnNormalized;
    FfscnModelOutput m_ffscnOutput;
    TemporalAccumulator m_accumulator;
    SignalTracker m_tracker;
    DetectionObserver* m_observer = nullptr;
    std::string m_lastError;
    SpectrumFrame m_geometry;
    std::size_t m_geometryCount = 0;
    bool m_hasGeometry = false;
    std::uint64_t m_generation = 0, m_configVersion = 0, m_lastSequence = 0;
    std::uint64_t m_trackingSegment = 0;
    std::int64_t m_lastTimestamp = 0;
    std::vector<float> m_normalized;
    ScnModelOutput m_modelOutput;
};

} // namespace scn::algorithm
