#pragma once
#include "IFfscnBackend.h"
#include <cstddef>
#include <vector>

namespace scn::algorithm
{
struct FfscnCandidate
{
    double beginBin = 0.0;
    double endBin = 0.0;
    float confidence = 0.0F;
    std::size_t peakIndex = 0;
};

std::vector<FfscnCandidate> decodeFfscn(const FfscnModelOutput& output,
                                         const FfscnConfig& config);
std::vector<FfscnCandidate> suppressFfscnCandidates(std::vector<FfscnCandidate> candidates,
                                                    float iouThreshold,
                                                    std::size_t capacity);
} // namespace scn::algorithm
