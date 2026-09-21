#pragma once

#include "../detail/SyntheticSpectrumSource.h"

namespace scn::source
{

class BB60CSource final : public detail::SyntheticSpectrumSource
{
public:
    BB60CSource();

protected:
    void addProfilePeaks(std::vector<float>& values) const override;
};

} // namespace scn::source
