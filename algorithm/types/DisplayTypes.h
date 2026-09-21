#pragma once

#include "DetectionTypes.h"
#include "SpectrumTypes.h"

#include <cstdint>
#include <memory>

namespace scn::algorithm
{

struct DisplaySnapshot
{
    SpectrumFrame frame;
    DetectionResult detection;
    std::uint64_t droppedFrames = 0;
    bool running = false;
};

using DisplaySnapshotPtr = std::shared_ptr<const DisplaySnapshot>;

} // namespace scn::algorithm
