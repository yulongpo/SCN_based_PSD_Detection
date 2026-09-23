#pragma once

#include <string>

namespace scn::application
{

struct RecordingConfig
{
    bool enabled = false;
    std::string directory;
};

} // namespace scn::application
