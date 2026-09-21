#pragma once

#include "../DetectionConfig.h"
#include <memory>
#include <string>
#include <vector>

namespace scn::algorithm
{
struct ScnModelOutput
{
    std::vector<float> heatmap;
    std::vector<float> bandwidth;
    std::vector<float> offset;
};

// Owned and called by the detection thread only. No Qt/CUDA types cross this API.
class IScnBackend
{
public:
    virtual ~IScnBackend() = default;
    virtual bool initialize(const DetectorConfig& config, std::string& error) = 0;
    virtual bool infer(const std::vector<float>& normalized, ScnModelOutput& output,
                       std::string& error) = 0;
    virtual std::string modelInfo() const = 0;
};
std::unique_ptr<IScnBackend> createTensorRtScnBackend();
} // namespace scn::algorithm
