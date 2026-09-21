#include "ScnDecoder.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace scn::algorithm
{
std::vector<ScnCandidate> decodeScn(const ScnModelOutput& out, const DetectorConfig& c)
{
    const auto n = out.heatmap.size();
    if (n != c.inputLength / 4 || out.bandwidth.size() != n || out.offset.size() != n)
        throw std::runtime_error("SCN output must contain three stride-4 tensors of equal length.");
    std::vector<ScnCandidate> peaks;
    for (std::size_t i = 0; i < n; ++i) {
        const auto score = out.heatmap[i];
        if (!std::isfinite(score) || score < c.confidenceThreshold) continue;
        float local = score;
        if (i) local = std::max(local, out.heatmap[i - 1]);
        if (i + 1 < n) local = std::max(local, out.heatmap[i + 1]);
        if (std::abs(score - local) >= 1e-6F) continue;
        // Rank peaks before validating widths to match reference TopK ordering.
        const double center = (static_cast<double>(i) + out.offset[i]) * 4.0;
        const double half = static_cast<double>(out.bandwidth[i]) * 2.0;
        peaks.push_back({center - half, center + half, score, i});
    }
    std::sort(peaks.begin(), peaks.end(), [](const auto& a, const auto& b) {
        return a.confidence != b.confidence ? a.confidence > b.confidence : a.peakIndex < b.peakIndex;
    });
    if (peaks.size() > c.topK) peaks.resize(c.topK);
    std::vector<ScnCandidate> kept;
    for (const auto& p : peaks) {
        if (!std::isfinite(p.beginBin) || !std::isfinite(p.endBin) || p.endBin <= p.beginBin) continue;
        bool suppressed = false;
        for (const auto& old : kept) {
            const double intersection = std::max(0.0, std::min(p.endBin, old.endBin) - std::max(p.beginBin, old.beginBin));
            const double united = (p.endBin - p.beginBin) + (old.endBin - old.beginBin) - intersection;
            if (united > 0 && intersection / united >= c.nmsIou) { suppressed = true; break; }
        }
        if (!suppressed) kept.push_back(p);
        if (kept.size() >= c.maxCandidatesPerWindow) break;
    }
    return kept;
}
}
