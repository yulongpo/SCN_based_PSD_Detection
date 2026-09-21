#pragma once

#include "../detector/ScnDecoder.h"
#include "../types/DetectionTypes.h"
#include "../types/SpectrumTypes.h"

namespace scn::algorithm
{
// Opt-in synchronous taps on the algorithm thread. References are callback-scoped.
class DetectionObserver
{
public:
    virtual ~DetectionObserver() = default;
    virtual void accumulated(const SpectrumFrame&, const std::vector<float>&,
                             const std::vector<float>&) {}
    virtual void window(std::uint64_t, SpectrumBranch, std::size_t, std::size_t,
                        const std::vector<float>&, const ScnModelOutput&,
                        const std::vector<ScnCandidate>&,
                        const std::vector<DetectedSignal>&) {}
    virtual void fused(const DetectionResult&) {}
};
} // namespace scn::algorithm
