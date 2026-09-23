#pragma once
#include "../detector/ScnDecoder.h"
#include "../preprocess/SpectrumPreprocessor.h"
#include "../types/DetectionTypes.h"

namespace scn::algorithm
{
std::vector<DetectedSignal> refineCnr(const std::vector<float>& spectrum,
    SpectrumWindow window, const std::vector<ScnCandidate>& candidates,
    const SpectrumFrame& frame, SpectrumBranch branch, float thresholdDb);

std::vector<double> makePowerPrefix(const std::vector<float>& spectrum);
DetectedSignal remeasureBand(const std::vector<double>& prefix,
    const SpectrumFrame& frame, const DetectedSignal& band, SpectrumBranch branch);
}
