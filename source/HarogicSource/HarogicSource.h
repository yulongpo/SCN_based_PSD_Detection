#pragma once

#include "../detail/SyntheticSpectrumSource.h"

namespace scn::source
{

class HarogicSource final : public detail::SyntheticSpectrumSource
{
public:
    HarogicSource();

protected:
    void addProfilePeaks(std::vector<float>& values) const override;
};

} // namespace scn::source
