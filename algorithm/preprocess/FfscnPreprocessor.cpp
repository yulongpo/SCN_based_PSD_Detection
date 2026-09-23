#include "FfscnPreprocessor.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace scn::algorithm
{
std::size_t ffscnInputLengthFor(std::size_t count)
{
    if (count == 0) throw std::invalid_argument("FFSCN cannot interpolate an empty spectrum.");
    constexpr std::size_t widths[]{1U << 13, 1U << 14, 1U << 15, 1U << 16, 1U << 17};
    std::size_t best = widths[0];
    auto distance = count > best ? count - best : best - count;
    for (const auto candidate : widths) {
        const auto candidateDistance = count > candidate ? count - candidate : candidate - count;
        // Prefer the larger model grid on an exact tie.
        if (candidateDistance <= distance) { best = candidate; distance = candidateDistance; }
    }
    return best;
}

std::vector<FfscnFrequencyWindow> makeFfscnWindows(const SpectrumFrame& frame,
                                                    std::size_t maximumLength,
                                                    std::size_t step)
{
    if (maximumLength != (std::size_t{1} << 17) || step != (std::size_t{1} << 16) ||
        !frame.isValid()) throw std::invalid_argument("Invalid FFSCN window geometry.");
    const auto count = frame.powerDb.size();
    std::vector<FfscnFrequencyWindow> result;
    if (count <= maximumLength) {
        const auto width = ffscnInputLengthFor(count);
        const double span = frame.binWidthHz * static_cast<double>(count);
        result.push_back({0, width, frame.startFrequencyHz, span / static_cast<double>(width)});
        return result;
    }
    std::size_t lastStart = (std::numeric_limits<std::size_t>::max)();
    for (std::size_t start = 0;; start += step) {
        if (start + maximumLength >= count) {
            start = count - maximumLength;
            if (start != lastStart)
                result.push_back({start, maximumLength,
                    frame.startFrequencyHz + static_cast<double>(start) * frame.binWidthHz,
                    frame.binWidthHz});
            break;
        }
        result.push_back({start, maximumLength,
            frame.startFrequencyHz + static_cast<double>(start) * frame.binWidthHz,
            frame.binWidthHz});
        lastStart = start;
    }
    return result;
}

void prepareFfscnInput(const std::vector<SpectrumFrame>& frames,
                       const FfscnFrequencyWindow& window,
                       std::vector<float>& normalized)
{
    if (frames.size() != 10 || window.inputLength < (std::size_t{1} << 13) ||
        window.inputLength > (std::size_t{1} << 17) ||
        (window.inputLength & (window.inputLength - 1)) != 0)
        throw std::invalid_argument("FFSCN needs exactly ten frames and a power-of-two frequency width in [8192,131072].");
    const auto width = window.inputLength;
    if (width > std::numeric_limits<std::size_t>::max() / frames.size())
        throw std::length_error("FFSCN input matrix is too large.");
    normalized.resize(frames.size() * width);
    double sum = 0.0;
    double squareSum = 0.0;
    for (std::size_t row = 0; row < frames.size(); ++row) {
        const auto& frame = frames[row];
        if (!frame.isValid()) throw std::invalid_argument("Invalid frame in FFSCN temporal window.");
        const std::size_t sourceLength = frame.powerDb.size();
        if (window.start > sourceLength || (sourceLength > (std::size_t{1} << 17) &&
            width > sourceLength - window.start))
            throw std::invalid_argument("FFSCN frequency window exceeds source spectrum.");
        auto* dst = normalized.data() + row * width;
        if (sourceLength > (std::size_t{1} << 17)) {
            if (window.start + width > sourceLength) throw std::invalid_argument("Unaligned FFSCN tail window.");
            std::copy_n(frame.powerDb.data() + window.start, width, dst);
        } else if (sourceLength == width) {
            std::copy(frame.powerDb.begin(), frame.powerDb.end(), dst);
        } else {
            // Linear interpolation over the original frequency-bin extent. This
            // preserves the covered band while adapting the grid to the closest
            // supported 2^N width (13 <= N <= 17).
            const double ratio = static_cast<double>(sourceLength) / static_cast<double>(width);
            for (std::size_t i = 0; i < width; ++i) {
                const double position = static_cast<double>(i) * ratio;
                const auto left = (std::min)(static_cast<std::size_t>(position), sourceLength - 1);
                const auto right = (std::min)(left + 1, sourceLength - 1);
                const double fraction = position - static_cast<double>(left);
                dst[i] = static_cast<float>(frame.powerDb[left] * (1.0 - fraction) +
                                            frame.powerDb[right] * fraction);
            }
        }
        for (std::size_t i = 0; i < width; ++i) {
            const double value = dst[i];
            if (!std::isfinite(value)) throw std::invalid_argument("FFSCN input contains NaN/Inf.");
            sum += value;
            squareSum += value * value;
        }
    }
    const double count = static_cast<double>(normalized.size());
    const double mean = sum / count;
    const double variance = normalized.size() > 1
        ? (squareSum - sum * mean) / static_cast<double>(normalized.size() - 1) : 0.0;
    const double deviation = std::sqrt((std::max)(0.0, variance));
    if (!std::isfinite(mean) || !std::isfinite(deviation))
        throw std::invalid_argument("FFSCN normalization produced non-finite statistics.");
    if (deviation <= std::numeric_limits<float>::epsilon()) {
        std::fill(normalized.begin(), normalized.end(), 0.0F);
        return;
    }
    for (auto& value : normalized) value = static_cast<float>((static_cast<double>(value) - mean) / deviation);
}
} // namespace scn::algorithm
