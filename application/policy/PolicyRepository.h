#pragma once

#include "PolicyTypes.h"

#include <string>

namespace scn::application::policy
{

class PolicyRepository final
{
public:
    static std::string defaultPath();
    static std::string historyPath();
    static bool load(const std::string& path, PolicyConfig& config, std::string& error);
    static bool save(const std::string& path, const PolicyConfig& config, std::string& error);
    static bool validate(const PolicyConfig& config, std::string& error);
};

} // namespace scn::application::policy
