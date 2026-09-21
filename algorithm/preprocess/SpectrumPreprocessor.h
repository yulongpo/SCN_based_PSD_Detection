#pragma once
#include "../types/SpectrumTypes.h"
#include <string>

namespace scn::algorithm
{
struct SpectrumWindow { std::size_t start = 0; std::size_t length = 0; };
bool validateFrame(const SpectrumFrame& frame, std::string& error);
bool sameGeometry(const SpectrumFrame& a, const SpectrumFrame& b);
std::vector<SpectrumWindow> makeWindows(std::size_t count, std::size_t length, std::size_t step);
void normalizeWindow(const std::vector<float>& spectrum, SpectrumWindow window,
                     std::size_t inputLength, std::vector<float>& normalized);
}
