#pragma once

#include "../DetectionConfig.h"
#include "../types/DetectionTypes.h"
#include "../types/SpectrumTypes.h"

namespace scn::algorithm
{

class DetectionEngine
{
public:
    DetectionEngine() = default;

    bool initialize(const DetectionConfig& config);
    DetectionResult process(const SpectrumFrame& frame);
    void reset();
    ConfigApplyResult updateConfig(const DetectionConfig& config);

    [[nodiscard]] const DetectionConfig& config() const noexcept { return m_config; }

private:
    DetectionConfig m_config{};
    bool m_initialized = false;
};

} // namespace scn::algorithm
