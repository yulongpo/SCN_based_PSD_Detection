#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace scn::algorithm
{

enum class DetectionStage
{
    Bypassed,
    Completed
};

struct DetectedSignal
{
    std::int64_t id = 0;
    double startFrequencyHz = 0.0;
    double endFrequencyHz = 0.0;
    double centerFrequencyHz = 0.0;
    double bandwidthHz = 0.0;
    float confidence = 0.0F;
    float snrDb = 0.0F;
};

struct DetectionDiagnostics
{
    double processingTimeMs = 0.0;
    std::string message;
};

struct DetectionResult
{
    std::uint64_t sequence = 0;
    DetectionStage stage = DetectionStage::Bypassed;
    // Do not name this member `signals`: Qt defines that token as a keyword macro.
    std::vector<DetectedSignal> detections;
    DetectionDiagnostics diagnostics;
};

} // namespace scn::algorithm
