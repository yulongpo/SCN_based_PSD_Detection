#pragma once
#include "IScnBackend.h"

namespace scn::algorithm
{
struct ScnCandidate { double beginBin = 0; double endBin = 0; float confidence = 0; std::size_t peakIndex = 0; };
std::vector<ScnCandidate> decodeScn(const ScnModelOutput& output, const DetectorConfig& config);
}
