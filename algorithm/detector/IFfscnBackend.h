#pragma once

#include "../DetectionConfig.h"
#include <memory>
#include <vector>
#include <string>

namespace scn::algorithm
{
struct FfscnModelOutput
{
    std::vector<float> heatmap;
    std::vector<float> bandwidth;
    std::vector<float> offset;
    std::size_t inputLength = 0;
};

// Detection-thread-owned runtime. Input is row-major [1,1,10,N], already
// normalized using the complete time-frequency matrix's sample standard deviation.
class IFfscnBackend
{
public:
    virtual ~IFfscnBackend() = default;
    virtual bool initialize(const FfscnConfig& config, std::string& error) = 0;
    virtual bool infer(const std::vector<float>& normalized, std::size_t inputLength,
                       FfscnModelOutput& output, std::string& error) = 0;
    virtual std::string modelInfo() const = 0;
};

std::unique_ptr<IFfscnBackend> createTensorRtFfscnBackend();
} // namespace scn::algorithm
