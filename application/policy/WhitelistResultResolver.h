#pragma once

#include "PolicyTypes.h"

#include <vector>

namespace scn::application::policy
{

class WhitelistResultResolver final
{
public:
    [[nodiscard]] std::vector<PolicySignal> resolve(
        const std::vector<algorithm::DetectedSignal>& rawSignals,
        const std::vector<WhitelistEntry>& whitelists) const;
};

} // namespace scn::application::policy
