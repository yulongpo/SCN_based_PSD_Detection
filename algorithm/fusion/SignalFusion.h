#pragma once
#include "../DetectionConfig.h"
#include "../types/DetectionTypes.h"
namespace scn::algorithm
{
std::vector<DetectedSignal> fuseSignals(std::vector<DetectedSignal> detections, const FusionConfig& config);
}
