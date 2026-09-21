#pragma once

#include <cstddef>

namespace scn::algorithm
{

struct DetectionConfig
{
    bool enabled = true;
    std::size_t maxSignals = 128;
};

enum class ConfigApplyResult
{
    Applied,
    RequiresReset,
    Invalid
};

} // namespace scn::algorithm
