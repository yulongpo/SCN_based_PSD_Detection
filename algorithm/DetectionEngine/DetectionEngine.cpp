#include "DetectionEngine.h"

#include <chrono>

namespace scn::algorithm
{

bool DetectionEngine::initialize(const DetectionConfig& config)
{
    if (config.maxSignals == 0) return false;
    m_config = config;
    m_initialized = true;
    return true;
}

DetectionResult DetectionEngine::process(const SpectrumFrame& frame)
{
    const auto begin = std::chrono::steady_clock::now();
    DetectionResult result;
    result.sequence = frame.sequence;
    result.stage = m_initialized && m_config.enabled
        ? DetectionStage::Completed
        : DetectionStage::Bypassed;
    result.diagnostics.message =
        "DetectionEngine interface-only implementation; no detector is enabled.";
    const auto end = std::chrono::steady_clock::now();
    result.diagnostics.processingTimeMs =
        std::chrono::duration<double, std::milli>(end - begin).count();
    return result;
}

void DetectionEngine::reset()
{
}

ConfigApplyResult DetectionEngine::updateConfig(const DetectionConfig& config)
{
    if (config.maxSignals == 0) return ConfigApplyResult::Invalid;
    m_config = config;
    return ConfigApplyResult::Applied;
}

} // namespace scn::algorithm
