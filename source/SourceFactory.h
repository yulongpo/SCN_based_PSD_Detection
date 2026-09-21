#pragma once

#include "ISpectrumSource.h"

#include <memory>

namespace scn::source
{

std::unique_ptr<ISpectrumSource> createSource(algorithm::SourceKind kind);

} // namespace scn::source
