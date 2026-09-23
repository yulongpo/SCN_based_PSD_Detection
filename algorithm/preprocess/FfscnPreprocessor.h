#pragma once

#include "../types/SpectrumTypes.h"
#include <cstddef>
#include <vector>

namespace scn::algorithm
{
struct FfscnFrequencyWindow
{
    std::size_t start = 0;
    std::size_t inputLength = 0;
    double startFrequencyHz = 0.0;
    double binWidthHz = 0.0;
};

std::size_t ffscnInputLengthFor(std::size_t sourceCount);
std::vector<FfscnFrequencyWindow> makeFfscnWindows(const SpectrumFrame& frame,
                                                    std::size_t maximumLength = 131072,
                                                    std::size_t step = 65536);
void prepareFfscnInput(const std::vector<SpectrumFrame>& chronologicalFrames,
                       const FfscnFrequencyWindow& window,
                       std::vector<float>& normalized);
} // namespace scn::algorithm
