#pragma once

#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace scn::common
{

using FrequencyHz = std::int64_t;

inline bool toIntegerHz(long double value, FrequencyHz& result) noexcept
{
    if (!std::isfinite(value)) return false;
    const long double rounded = std::round(value);
    const long double signedLimit = std::ldexp(1.0L, 63);
    if (rounded < -signedLimit || rounded >= signedLimit) {
        return false;
    }
    result = static_cast<FrequencyHz>(rounded);
    return true;
}

inline bool toIntegerHz(double value, FrequencyHz& result) noexcept
{
    return toIntegerHz(static_cast<long double>(value), result);
}

inline bool parseFrequencyHz(std::string_view text, FrequencyHz& result)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    if (text.empty()) return false;

    long double multiplier = 1.0L;
    std::size_t suffixLength = 0;
    const auto endsWithInsensitive = [text](std::string_view suffix) {
        if (text.size() < suffix.size()) return false;
        const auto offset = text.size() - suffix.size();
        for (std::size_t i = 0; i < suffix.size(); ++i) {
            const auto lhs = static_cast<unsigned char>(text[offset + i]);
            const auto rhs = static_cast<unsigned char>(suffix[i]);
            if (std::tolower(lhs) != std::tolower(rhs)) return false;
        }
        return true;
    };

    if (endsWithInsensitive("ghz")) { multiplier = 1.0e9L; suffixLength = 3; }
    else if (endsWithInsensitive("mhz")) { multiplier = 1.0e6L; suffixLength = 3; }
    else if (endsWithInsensitive("khz")) { multiplier = 1.0e3L; suffixLength = 3; }
    else if (endsWithInsensitive("hz")) { suffixLength = 2; }
    else if (endsWithInsensitive("g")) { multiplier = 1.0e9L; suffixLength = 1; }
    else if (endsWithInsensitive("m")) { multiplier = 1.0e6L; suffixLength = 1; }
    else if (endsWithInsensitive("k")) { multiplier = 1.0e3L; suffixLength = 1; }
    else if (endsWithInsensitive("h")) { suffixLength = 1; }

    auto numeric = text.substr(0, text.size() - suffixLength);
    while (!numeric.empty() && std::isspace(static_cast<unsigned char>(numeric.back()))) numeric.remove_suffix(1);
    while (!numeric.empty() && std::isspace(static_cast<unsigned char>(numeric.front()))) numeric.remove_prefix(1);
    if (numeric.empty()) return false;

    const std::string number(numeric);
    char* end = nullptr;
    const long double value = std::strtold(number.c_str(), &end);
    if (end == number.c_str() || *end != '\0' || !std::isfinite(value)) return false;
    return toIntegerHz(value * multiplier, result);
}

inline std::string formatFrequencyHz(FrequencyHz hz)
{
    const long double magnitude = std::fabs(static_cast<long double>(hz));
    long double scale = 1.0L;
    int decimals = 0;
    const char* unit = "Hz";
    if (magnitude >= 1.0e9L) { scale = 1.0e9L; decimals = 9; unit = "GHz"; }
    else if (magnitude >= 1.0e6L) { scale = 1.0e6L; decimals = 6; unit = "MHz"; }
    else if (magnitude >= 1.0e3L) { scale = 1.0e3L; decimals = 3; unit = "kHz"; }

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(decimals)
           << static_cast<long double>(hz) / scale;
    std::string value = stream.str();
    if (decimals > 0) {
        while (!value.empty() && value.back() == '0') value.pop_back();
        if (!value.empty() && value.back() == '.') value.pop_back();
    }
    return value + " " + unit;
}

inline std::string formatFrequencyHz(int hz)
{
    return formatFrequencyHz(static_cast<FrequencyHz>(hz));
}

inline std::string formatFrequencyHz(double hz)
{
    FrequencyHz rounded = 0;
    if (!toIntegerHz(hz, rounded)) return "—";
    return formatFrequencyHz(rounded);
}

} // namespace scn::common
