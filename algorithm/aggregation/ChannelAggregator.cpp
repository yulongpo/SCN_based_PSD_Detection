#include "ChannelAggregator.h"

#include "../refine/CnrRefiner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <tuple>

namespace scn::algorithm
{
namespace
{
struct SpectrumEvidenceWork
{
    double unitHz = 1.0;
    std::vector<float> level;
    std::vector<double> floor;
    std::vector<std::uint8_t> known;
    std::vector<std::uint8_t> occupied;
};

std::size_t cellIndex(double frequencyHz, const SpectrumFrame& frame, double unitHz,
                     std::size_t cellCount)
{
    const double position = (frequencyHz - frame.startFrequencyHz) / unitHz;
    return static_cast<std::size_t>(std::clamp(std::floor(position), 0.0,
                                               static_cast<double>(cellCount - 1)));
}

double overlapWidth(const DetectedSignal& a, const DetectedSignal& b)
{
    return std::max(0.0, std::min(a.endFrequencyHz, b.endFrequencyHz) -
                         std::max(a.startFrequencyHz, b.startFrequencyHz));
}

double intervalIou(const DetectedSignal& a, const DetectedSignal& b)
{
    const double overlap = overlapWidth(a, b);
    const double united = a.bandwidthHz + b.bandwidthHz - overlap;
    return united > 0.0 ? overlap / united : 0.0;
}

double percentile(std::vector<float>& values, double fraction)
{
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    const auto index = static_cast<std::size_t>(std::floor(fraction * (values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

double median(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    if (values.size() % 2) return *middle;
    const auto upper = *middle;
    return (upper + *std::max_element(values.begin(), middle)) * 0.5;
}

struct BinRange
{
    std::size_t first = 0;
    std::size_t last = 0;
};

struct RemeasuredBand
{
    DetectedSignal signal;
    bool hasNoiseSamples = false;
};

RemeasuredBand remeasureExcludingCandidates(const std::vector<double>& prefix,
                                             const SpectrumFrame& frame,
                                             const DetectedSignal& band,
                                             SpectrumBranch branch,
                                             const std::vector<BinRange>& excluded)
{
    RemeasuredBand result{band, false};
    result.signal.branch = branch;
    if (frame.powerDb.empty() || prefix.size() != frame.powerDb.size() + 1 ||
        !(frame.binWidthHz > 0.0) || !(band.endFrequencyHz > band.startFrequencyHz)) return result;
    const auto count = frame.powerDb.size();
    auto first = static_cast<std::size_t>(std::clamp(
        std::floor((band.startFrequencyHz - frame.startFrequencyHz) / frame.binWidthHz + 1e-10),
        0.0, static_cast<double>(count)));
    auto last = static_cast<std::size_t>(std::clamp(
        std::ceil((band.endFrequencyHz - frame.startFrequencyHz) / frame.binWidthHz - 1e-10),
        0.0, static_cast<double>(count)));
    if (last <= first) {
        if (first >= count) first = count - 1;
        last = first + 1;
    }
    const auto width = last - first;
    const auto trim = static_cast<std::size_t>(std::round(width * 0.2));
    const auto signalFirst = std::min(last - 1, first + trim);
    const auto signalLast = std::max(signalFirst + 1, last - trim);
    const auto mean = [&prefix](std::size_t begin, std::size_t end) {
        return end > begin ? (prefix[end] - prefix[begin]) / static_cast<double>(end - begin) : 0.0;
    };
    const double signalDbm = mean(signalFirst, signalLast);
    const auto noiseWidth = std::max<std::size_t>(1, width / 10);
    const auto left = first - std::min(first, noiseWidth);
    const auto right = last + std::min(count - last, noiseWidth);
    double noiseSum = 0.0;
    std::size_t noiseCount = 0;
    const auto addUnexcluded = [&](std::size_t begin, std::size_t end) {
        if (end <= begin) return;
        double sum = prefix[end] - prefix[begin];
        std::size_t samples = end - begin;
        auto it = std::lower_bound(excluded.begin(), excluded.end(), begin,
            [](const BinRange& range, std::size_t position) { return range.last <= position; });
        for (; it != excluded.end() && it->first < end; ++it) {
            const auto overlapFirst = std::max(begin, it->first);
            const auto overlapLast = std::min(end, it->last);
            if (overlapLast <= overlapFirst) continue;
            sum -= prefix[overlapLast] - prefix[overlapFirst];
            samples -= overlapLast - overlapFirst;
        }
        noiseSum += sum;
        noiseCount += samples;
    };
    addUnexcluded(left, first);
    addUnexcluded(last, right);
    const double noiseDbm = noiseCount ? noiseSum / static_cast<double>(noiseCount) : signalDbm;
    const double cnrDb = noiseCount ? signalDbm - noiseDbm : 0.0;
    if (std::isfinite(signalDbm) && std::isfinite(noiseDbm) && std::isfinite(cnrDb)) {
        result.signal.signalLevelDbm = static_cast<float>(signalDbm);
        result.signal.noiseLevelDbm = static_cast<float>(noiseDbm);
        result.signal.snrDb = static_cast<float>(cnrDb);
        result.hasNoiseSamples = noiseCount != 0;
    }
    return result;
}

SpectrumEvidenceWork estimateEvidence(const SpectrumFrame& frame,
                                     const std::vector<ChannelCandidate>& candidates,
                                     const ChannelAggregationConfig& config,
                                     const std::function<bool()>& cancelled)
{
    SpectrumEvidenceWork work;
    work.unitHz = std::max({frame.resolutionBandwidthHz, 4.0 * frame.binWidthHz, 25'000.0});
    const auto cellCount = static_cast<std::size_t>(std::max(1.0,
        std::ceil(frame.binWidthHz * frame.powerDb.size() / work.unitHz)));
    work.level.assign(cellCount, -std::numeric_limits<float>::infinity());
    std::vector<float> cellSamples;
    cellSamples.reserve(static_cast<std::size_t>(std::ceil(work.unitHz / frame.binWidthHz)) + 2);
    for (std::size_t cell = 0; cell < cellCount; ++cell) {
        if ((cell & 0xFFFU) == 0 && cancelled && cancelled())
            throw std::runtime_error("Channel aggregation cancelled.");
        const auto first = std::min(frame.powerDb.size(), static_cast<std::size_t>(
            std::ceil(cell * work.unitHz / frame.binWidthHz)));
        const auto last = std::min(frame.powerDb.size(), static_cast<std::size_t>(
            std::ceil((cell + 1) * work.unitHz / frame.binWidthHz)));
        cellSamples.clear();
        for (auto bin = first; bin < last; ++bin) cellSamples.push_back(frame.powerDb[bin]);
        if (!cellSamples.empty()) work.level[cell] = static_cast<float>(percentile(cellSamples, 0.5));
    }

    std::vector<std::uint8_t> candidateMask(cellCount, 0);
    for (const auto& candidate : candidates) {
        if (candidate.signal.confidence < 0.4F) continue;
        auto first = cellIndex(candidate.signal.startFrequencyHz, frame, work.unitHz, cellCount);
        auto last = cellIndex(std::nextafter(candidate.signal.endFrequencyHz,
                                             candidate.signal.startFrequencyHz),
                              frame, work.unitHz, cellCount);
        const auto guard = static_cast<std::size_t>(std::ceil(2.0 * frame.resolutionBandwidthHz / work.unitHz));
        first = first > guard ? first - guard : 0;
        last = std::min(cellCount - 1, last + guard);
        std::fill(candidateMask.begin() + static_cast<std::ptrdiff_t>(first),
                  candidateMask.begin() + static_cast<std::ptrdiff_t>(last + 1), 1);
    }

    const auto anchorWidth = static_cast<std::size_t>(std::max(64.0,
        std::ceil(5'000'000.0 / work.unitHz)));
    const auto anchorStep = std::max<std::size_t>(1, anchorWidth / 2);
    std::vector<std::pair<double, double>> anchors;
    std::vector<float> samples;
    for (std::size_t begin = 0; begin < cellCount; begin += anchorStep) {
        if (cancelled && cancelled()) throw std::runtime_error("Channel aggregation cancelled.");
        const auto end = std::min(cellCount, begin + anchorWidth);
        samples.clear();
        for (std::size_t cell = begin; cell < end; ++cell)
            if (!candidateMask[cell] && std::isfinite(work.level[cell])) samples.push_back(work.level[cell]);
        if (samples.size() * 4 < (end - begin)) continue;
        const double center = (static_cast<double>(begin) + static_cast<double>(end) - 1.0) / 2.0;
        anchors.emplace_back(center, percentile(samples, 0.20));
    }
    work.floor.assign(cellCount, std::numeric_limits<double>::quiet_NaN());
    work.known.assign(cellCount, 0);
    const double maxAnchorDistance = static_cast<double>(anchorWidth) * 2.0;
    for (std::size_t cell = 0; cell < cellCount && !anchors.empty(); ++cell) {
        auto upper = std::lower_bound(anchors.begin(), anchors.end(), static_cast<double>(cell),
            [](const auto& item, double value) { return item.first < value; });
        if (upper == anchors.begin()) {
            if (upper->first - cell <= maxAnchorDistance) {
                work.floor[cell] = upper->second; work.known[cell] = 1;
            }
        } else if (upper == anchors.end()) {
            const auto& lower = anchors.back();
            if (cell - lower.first <= maxAnchorDistance) {
                work.floor[cell] = lower.second; work.known[cell] = 1;
            }
        } else {
            const auto& lower = *(upper - 1);
            if (upper->first - lower.first <= maxAnchorDistance * 2.0) {
                const double fraction = (cell - lower.first) / (upper->first - lower.first);
                work.floor[cell] = lower.second + fraction * (upper->second - lower.second);
                work.known[cell] = 1;
            }
        }
    }

    work.occupied.assign(cellCount, 0);
    std::vector<std::uint8_t> aboveLow(cellCount, 0);
    std::vector<std::uint8_t> aboveHigh(cellCount, 0);
    for (std::size_t cell = 0; cell < cellCount; ++cell) {
        if (!work.known[cell]) continue;
        const double excessDb = work.level[cell] - work.floor[cell];
        aboveLow[cell] = excessDb >= config.lowThresholdDb;
        aboveHigh[cell] = excessDb >= config.highThresholdDb;
    }
    for (std::size_t cell = 0; cell < cellCount;) {
        if (!aboveLow[cell]) { ++cell; continue; }
        const auto begin = cell;
        bool hasHighSeed = false;
        while (cell < cellCount && aboveLow[cell]) {
            hasHighSeed = hasHighSeed || aboveHigh[cell];
            ++cell;
        }
        if (hasHighSeed)
            std::fill(work.occupied.begin() + static_cast<std::ptrdiff_t>(begin),
                      work.occupied.begin() + static_cast<std::ptrdiff_t>(cell), 1);
    }
    // Occupancy is derived only from the current raw PSD and its estimated
    // noise floor. SCN candidates mask noise-floor estimation above, but must
    // never manufacture occupancy history (notably from the 16-frame maximum).
    return work;
}

std::size_t requiredSupport(std::size_t historySize, double minSupportRatio)
{
    return std::max<std::size_t>(2, static_cast<std::size_t>(
        std::ceil(minSupportRatio * static_cast<double>(historySize))));
}

double supportCoverage(std::size_t first, std::size_t last,
                      const std::vector<std::size_t>& support,
                      std::size_t historySize, double minSupportRatio,
                      std::size_t* unsupported = nullptr)
{
    if (last < first || support.empty()) return 0.0;
    last = std::min(last, support.size() - 1);
    first = std::min(first, last);
    const auto required = requiredSupport(historySize, minSupportRatio);
    std::size_t gaps = 0;
    for (std::size_t cell = first; cell <= last; ++cell)
        gaps += support[cell] < required;
    if (unsupported) *unsupported = gaps;
    const auto width = last - first + 1;
    return width ? 1.0 - static_cast<double>(gaps) / static_cast<double>(width) : 0.0;
}

bool persistentLowGap(std::size_t first, std::size_t last,
                      const std::deque<ChannelAggregator::HistoryRow>& history)
{
    if (last < first || last - first + 1 < 2 || history.size() < 3) return false;
    const auto recent = std::min<std::size_t>(3, history.size());
    for (std::size_t offset = 0; offset < recent; ++offset) {
        const auto& row = history[history.size() - 1 - offset];
        if (last >= row.known.size() || last >= row.occupied.size()) return false;
        for (std::size_t cell = first; cell <= last; ++cell)
            if (!row.known[cell] || row.occupied[cell]) return false;
    }
    return true;
}

bool hasPersistentGap(std::size_t first, std::size_t last,
                      const std::vector<std::size_t>& support,
                      const std::deque<ChannelAggregator::HistoryRow>& history,
                      std::size_t historySize, double minSupportRatio)
{
    if (last < first) return false;
    first = std::min(first, support.size() - 1);
    last = std::min(last, support.size() - 1);
    if (history.size() >= 3) {
        const auto recent = std::min<std::size_t>(3, history.size());
        const auto recentLow = [&](std::size_t index) {
            for (std::size_t offset = 0; offset < recent; ++offset) {
                const auto& row = history[history.size() - 1 - offset];
                if (index >= row.known.size() || index >= row.occupied.size() ||
                    !row.known[index] || row.occupied[index]) return false;
            }
            return true;
        };
        auto cell = first;
        while (cell <= last) {
            if (!recentLow(cell)) { ++cell; continue; }
            const auto begin = cell;
            while (cell <= last && recentLow(cell)) ++cell;
            if (cell - begin >= 2) return true;
        }
    }
    const auto required = requiredSupport(historySize, minSupportRatio);
    auto cell = first;
    while (cell <= last) {
        if (support[cell] >= required) { ++cell; continue; }
        const auto begin = cell;
        while (cell <= last && support[cell] < required) ++cell;
        const auto end = cell - 1;
        if (end - begin + 1 >= 2 && persistentLowGap(begin, end, history)) return true;
    }
    return false;
}

bool supportedBand(const DetectedSignal& band,
                   const SpectrumFrame& frame,
                   const std::deque<ChannelAggregator::HistoryRow>& history,
                   const std::vector<std::size_t>& support,
                   double unitHz, double minSupportRatio, double minCoverageRatio,
                   double& coverage)
{
    const auto count = history.empty() ? 0 : history.back().occupied.size();
    if (!count || !(band.bandwidthHz > 0.0)) return false;
    const auto first = cellIndex(band.startFrequencyHz, frame, unitHz, count);
    const auto last = cellIndex(std::nextafter(band.endFrequencyHz, band.startFrequencyHz), frame, unitHz, count);
    if (last <= first) return false;
    coverage = supportCoverage(first, last, support, history.size(), minSupportRatio);
    return coverage >= minCoverageRatio &&
           !hasPersistentGap(first, last, support, history, history.size(), minSupportRatio);
}

DetectedSignal unionBand(const std::vector<ChannelCandidate>& candidates,
                         const std::vector<std::size_t>& members,
                         std::vector<CandidateReference>& refs,
                         std::uint64_t sequence)
{
    const auto strongest = *std::max_element(members.begin(), members.end(), [&](auto lhs, auto rhs) {
        const auto& a = candidates[lhs].signal;
        const auto& b = candidates[rhs].signal;
        if (a.signalLevelDbm != b.signalLevelDbm) return a.signalLevelDbm < b.signalLevelDbm;
        if (a.confidence != b.confidence) return a.confidence < b.confidence;
        return lhs > rhs;
    });
    auto output = candidates[strongest].signal;
    output.startFrequencyHz = std::numeric_limits<double>::infinity();
    output.endFrequencyHz = -std::numeric_limits<double>::infinity();
    output.confidence = 0.0F;
    for (const auto index : members) {
        const auto& signal = candidates[index].signal;
        output.startFrequencyHz = std::min(output.startFrequencyHz, signal.startFrequencyHz);
        output.endFrequencyHz = std::max(output.endFrequencyHz, signal.endFrequencyHz);
        output.confidence = std::max(output.confidence, signal.confidence);
        refs.push_back({sequence, static_cast<std::uint32_t>(index)});
    }
    output.bandwidthHz = output.endFrequencyHz - output.startFrequencyHz;
    output.centerFrequencyHz = output.startFrequencyHz + output.bandwidthHz / 2.0;
    output.branch = SpectrumBranch::Both;
    return output;
}

} // namespace

void ChannelAggregator::reset()
{
    m_history.clear(); m_pending.clear(); m_channels.clear(); m_individuals.clear();
    m_nextBusinessId = -1; m_lastSequence = 0;
    m_lastTimestampNs = 0; m_frameIntervalsNs.clear(); m_previousObservationDefinitive = true;
}

bool ChannelAggregator::update(const SpectrumFrame& frame,
                               const std::vector<float>& averageSpectrum,
                               const std::vector<float>& maximumSpectrum,
                               const std::vector<ChannelCandidate>& candidates,
                               const std::vector<DetectionResult::TrackedDetection>& rawTracked,
                               const ChannelAggregationConfig& config,
                               float cnrThresholdDb,
                               bool definitiveObservation,
                               std::vector<DetectionResult::ChannelDetection>& output,
                               ChannelEvidence* evidence,
                               const std::function<bool()>& cancelled)
{
    output.clear();
    const auto checkCancelled = [&] {
        if (cancelled && cancelled()) throw std::runtime_error("Channel aggregation cancelled.");
    };
    try {
        checkCancelled();
        auto workHistory = m_history;
        auto workPending = m_pending;
        auto workChannels = m_channels;
        auto workIndividuals = m_individuals;
        auto nextId = m_nextBusinessId;
        const bool sequenceReset = m_lastSequence != 0 &&
            (frame.sequence <= m_lastSequence || frame.timestampNs < m_lastTimestampNs);
        if (sequenceReset) {
            workHistory.clear(); workPending.clear(); workChannels.clear(); workIndividuals.clear();
        }
        auto intervals = m_frameIntervalsNs;
        std::int64_t medianIntervalNs = 0;
        if (!intervals.empty()) {
            auto sorted = std::vector<std::int64_t>(intervals.begin(), intervals.end());
            const auto middle = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2);
            std::nth_element(sorted.begin(), middle, sorted.end());
            medianIntervalNs = *middle;
        }
        const auto maxContinuousGapNs = std::max<std::int64_t>(1'000'000'000LL,
            medianIntervalNs > 0 && medianIntervalNs <= std::numeric_limits<std::int64_t>::max() / 3
                ? medianIntervalNs * 3 : 1'000'000'000LL);
        const bool interruptedObservation = !sequenceReset && m_lastSequence != 0 &&
                                            !m_previousObservationDefinitive;
        const bool longDataGap = interruptedObservation || (!sequenceReset && m_lastTimestampNs > 0 &&
            frame.timestampNs > m_lastTimestampNs && frame.timestampNs - m_lastTimestampNs > maxContinuousGapNs);
        if (longDataGap || !definitiveObservation) {
            workPending.clear();
            for (auto& channel : workChannels) channel.splitCount = 0;
        }

        auto spectrum = estimateEvidence(frame, candidates, config, cancelled);
        const auto currentOccupancy = [&](double startHz, double endHz) {
            const auto first = cellIndex(startHz, frame, spectrum.unitHz, spectrum.known.size());
            const auto last = cellIndex(std::nextafter(endHz, startHz), frame, spectrum.unitHz,
                                        spectrum.known.size());
            std::size_t knownCount = 0, occupiedCount = 0;
            for (std::size_t cell = first; cell <= last; ++cell) {
                if (!spectrum.known[cell]) continue;
                ++knownCount;
                occupiedCount += spectrum.occupied[cell] != 0;
            }
            const double knownRatio = static_cast<double>(knownCount) /
                                      static_cast<double>(last - first + 1);
            const double occupiedRatio = knownCount
                ? static_cast<double>(occupiedCount) / static_cast<double>(knownCount) : 0.0;
            return std::pair{knownRatio, occupiedRatio};
        };
        HistoryRow row{frame.sequence, frame.timestampNs, spectrum.known, spectrum.occupied};
        if (definitiveObservation) workHistory.push_back(std::move(row));
        const auto maxHistory = std::size_t{16};
        while (workHistory.size() > maxHistory) workHistory.pop_front();
        while (!workHistory.empty() && config.historySeconds > 0.0 &&
               static_cast<long double>(frame.timestampNs) - workHistory.front().timestampNs >
                   config.historySeconds * 1e9L)
            workHistory.pop_front();
        std::vector<std::size_t> supportCounts(spectrum.occupied.size(), 0);
        for (const auto& historyRow : workHistory)
            for (std::size_t cell = 0; cell < supportCounts.size() && cell < historyRow.occupied.size(); ++cell)
                supportCounts[cell] += historyRow.occupied[cell] != 0;

        if (evidence) {
            evidence->unitWidthHz = spectrum.unitHz;
            evidence->noiseFloorDbm = spectrum.floor;
            evidence->known = spectrum.known;
            evidence->occupied = spectrum.occupied;
            evidence->historyRows = workHistory.size();
        }

        struct Component { std::vector<std::size_t> members; double start = 0.0; double end = 0.0; };
        std::vector<std::size_t> order(candidates.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](auto a, auto b) {
            return std::tie(candidates[a].signal.startFrequencyHz, candidates[a].signal.endFrequencyHz, a) <
                   std::tie(candidates[b].signal.startFrequencyHz, candidates[b].signal.endFrequencyHz, b);
        });
        std::vector<std::size_t> parent(candidates.size());
        std::iota(parent.begin(), parent.end(), 0);
        const auto root = [&](std::size_t index, auto&& self) -> std::size_t {
            return parent[index] == index ? index : parent[index] = self(parent[index], self);
        };
        std::vector<ChannelGroupingDiagnostic> groupingDiagnostics;
        const auto rejectedConnection = [&](std::size_t leftIndex, std::size_t rightIndex,
                                            double startHz, double endHz, const char* reason) {
            ChannelGroupingDiagnostic diagnostic;
            diagnostic.candidateIndices = {static_cast<std::uint32_t>(leftIndex),
                                           static_cast<std::uint32_t>(rightIndex)};
            diagnostic.startFrequencyHz = startHz;
            diagnostic.endFrequencyHz = endHz;
            diagnostic.disposition = reason;
            groupingDiagnostics.push_back(std::move(diagnostic));
        };
        std::vector<std::tuple<double, std::size_t, std::size_t>> edges;
        // Only adjacent frequency-ordered candidates may propose a connection.
        // Every DSU expansion is then rechecked over its full proposed union so
        // A-B and B-C cannot transitively bridge a persistent independent gap.
        for (std::size_t position = 1; position < order.size(); ++position) {
            checkCancelled();
            const auto i = order[position - 1], j = order[position];
            const auto& left = candidates[i].signal;
            const auto& right = candidates[j].signal;
            const double gap = std::max(0.0, right.startFrequencyHz - left.endFrequencyHz);
            const double combinedWidth = std::max(left.endFrequencyHz, right.endFrequencyHz) -
                                         std::min(left.startFrequencyHz, right.startFrequencyHz);
            if (combinedWidth > static_cast<double>(config.maximumAutomaticBandwidthHz)) {
                rejectedConnection(i, j, std::min(left.startFrequencyHz, right.startFrequencyHz),
                    std::max(left.endFrequencyHz, right.endFrequencyHz), "adjacent_union_exceeds_maximum_bandwidth");
                continue;
            }
            auto firstCell = cellIndex(std::min(left.endFrequencyHz, right.startFrequencyHz), frame,
                                       spectrum.unitHz, spectrum.occupied.size());
            auto lastCell = cellIndex(std::max(left.endFrequencyHz, right.startFrequencyHz), frame,
                                      spectrum.unitHz, spectrum.occupied.size());
            if (lastCell < firstCell) std::swap(firstCell, lastCell);
            const bool persistentGap = gap > 0.0 && hasPersistentGap(firstCell, lastCell,
                supportCounts, workHistory, workHistory.size(), config.minimumSupportRatio);
            const double shortHole = std::min(1'000'000.0, combinedWidth * 0.05);
            const bool gapSupported = gap <= shortHole ||
                (workHistory.size() >= 2 && supportCoverage(firstCell, lastCell, supportCounts,
                    workHistory.size(), config.minimumSupportRatio) >= 0.70);
            if (persistentGap) {
                rejectedConnection(i, j, left.endFrequencyHz, right.startFrequencyHz,
                                   "persistent_low_occupancy_gap");
            } else if (gapSupported) {
                edges.emplace_back(gap, i, j);
            } else {
                rejectedConnection(i, j, left.endFrequencyHz, right.startFrequencyHz,
                                   "insufficient_gap_occupancy_support");
            }
        }
        std::sort(edges.begin(), edges.end());
        std::vector<std::size_t> componentSize(candidates.size(), 1);
        std::vector<double> componentStart(candidates.size()), componentEnd(candidates.size());
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            componentStart[i] = candidates[i].signal.startFrequencyHz;
            componentEnd[i] = candidates[i].signal.endFrequencyHz;
        }
        for (const auto& edge : edges) {
            checkCancelled();
            const auto lhs = std::get<1>(edge), rhs = std::get<2>(edge);
            auto leftRoot = root(lhs, root), rightRoot = root(rhs, root);
            if (leftRoot == rightRoot) continue;
            const double start = std::min(componentStart[leftRoot], componentStart[rightRoot]);
            const double end = std::max(componentEnd[leftRoot], componentEnd[rightRoot]);
            if (end - start > static_cast<double>(config.maximumAutomaticBandwidthHz)) {
                rejectedConnection(lhs, rhs, start, end, "transitive_union_exceeds_maximum_bandwidth");
                continue;
            }
            const auto firstCell = cellIndex(start, frame, spectrum.unitHz, spectrum.occupied.size());
            const auto lastCell = cellIndex(std::nextafter(end, start), frame, spectrum.unitHz,
                                            spectrum.occupied.size());
            std::size_t unsupported = 0;
            const double coverage = supportCoverage(firstCell, lastCell, supportCounts,
                workHistory.size(), config.minimumSupportRatio, &unsupported);
            const auto cellWidth = lastCell >= firstCell ? lastCell - firstCell + 1 : 0;
            const bool persistentUnionGap = hasPersistentGap(firstCell, lastCell, supportCounts,
                workHistory, workHistory.size(), config.minimumSupportRatio);
            if (coverage < config.minimumCoverageRatio || !cellWidth ||
                static_cast<double>(unsupported) / static_cast<double>(cellWidth) > 0.20 ||
                persistentUnionGap) {
                rejectedConnection(lhs, rhs, start, end, persistentUnionGap
                    ? "transitive_union_has_persistent_low_gap"
                    : "transitive_union_occupancy_coverage_below_threshold");
                continue;
            }
            if (componentSize[leftRoot] < componentSize[rightRoot]) std::swap(leftRoot, rightRoot);
            parent[rightRoot] = leftRoot;
            componentSize[leftRoot] += componentSize[rightRoot];
            componentStart[leftRoot] = start; componentEnd[leftRoot] = end;
        }

        std::vector<Component> components;
        for (const auto index : order) {
            const auto representative = root(index, root);
            auto found = std::find_if(components.begin(), components.end(),
                [representative, &parent, &root](const Component& item) {
                    return !item.members.empty() && root(item.members.front(), root) == representative;
                });
            if (found == components.end()) {
                components.push_back({{index}, candidates[index].signal.startFrequencyHz,
                                      candidates[index].signal.endFrequencyHz});
            } else {
                found->members.push_back(index);
                found->start = std::min(found->start, candidates[index].signal.startFrequencyHz);
                found->end = std::max(found->end, candidates[index].signal.endFrequencyHz);
            }
        }
        const auto componentDiagnosticStart = groupingDiagnostics.size();
        groupingDiagnostics.reserve(groupingDiagnostics.size() + components.size());
        for (const auto& component : components) {
            ChannelGroupingDiagnostic diagnostic;
            diagnostic.startFrequencyHz = component.start;
            diagnostic.endFrequencyHz = component.end;
            diagnostic.disposition = component.members.size() < 2
                ? "single_candidate_retained" : "insufficient_history";
            for (const auto index : component.members)
                diagnostic.candidateIndices.push_back(static_cast<std::uint32_t>(index));
            groupingDiagnostics.push_back(std::move(diagnostic));
        }

        const auto& average = averageSpectrum.size() == frame.powerDb.size() ? averageSpectrum : frame.powerDb;
        const auto& maximum = maximumSpectrum.size() == frame.powerDb.size() ? maximumSpectrum : frame.powerDb;
        const auto averagePrefix = makePowerPrefix(average);
        const auto maximumPrefix = makePowerPrefix(maximum);
        std::vector<BinRange> excludedCandidateRanges;
        excludedCandidateRanges.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            const auto first = static_cast<std::size_t>(std::clamp(
                std::floor((candidate.signal.startFrequencyHz - frame.startFrequencyHz) / frame.binWidthHz),
                0.0, static_cast<double>(frame.powerDb.size())));
            const auto last = static_cast<std::size_t>(std::clamp(
                std::ceil((candidate.signal.endFrequencyHz - frame.startFrequencyHz) / frame.binWidthHz),
                0.0, static_cast<double>(frame.powerDb.size())));
            if (last > first) excludedCandidateRanges.push_back({first, last});
        }
        std::sort(excludedCandidateRanges.begin(), excludedCandidateRanges.end(),
            [](const BinRange& lhs, const BinRange& rhs) { return lhs.first < rhs.first; });
        std::vector<BinRange> mergedCandidateRanges;
        for (const auto& range : excludedCandidateRanges) {
            if (mergedCandidateRanges.empty() || range.first > mergedCandidateRanges.back().last)
                mergedCandidateRanges.push_back(range);
            else
                mergedCandidateRanges.back().last = std::max(mergedCandidateRanges.back().last, range.last);
        }
        std::vector<Proposal> proposals;
        if (config.enabled && workHistory.size() >= 2) {
            for (std::size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
                const auto& component = components[componentIndex];
                const auto diagnosticIndex = componentDiagnosticStart + componentIndex;
                auto& grouping = groupingDiagnostics[diagnosticIndex];
                if (component.members.size() < 2) continue;
                const double width = component.end - component.start;
                const double minimumWidth = std::max(1'000'000.0, 16.0 * spectrum.unitHz);
                if (width < minimumWidth) {
                    grouping.disposition = "below_minimum_automatic_bandwidth";
                    continue;
                }
                if (width > static_cast<double>(config.maximumAutomaticBandwidthHz)) {
                    grouping.disposition = "exceeds_maximum_automatic_bandwidth";
                    continue;
                }
                std::vector<CandidateReference> refs;
                auto band = unionBand(candidates, component.members, refs, frame.sequence);
                double coverage = 0.0;
                if (!supportedBand(band, frame, workHistory, supportCounts, spectrum.unitHz,
                                   config.minimumSupportRatio, config.minimumCoverageRatio, coverage)) {
                    grouping.occupancyCoverage = coverage;
                    const auto first = cellIndex(band.startFrequencyHz, frame, spectrum.unitHz,
                                                 spectrum.occupied.size());
                    const auto last = cellIndex(std::nextafter(band.endFrequencyHz, band.startFrequencyHz),
                                                frame, spectrum.unitHz, spectrum.occupied.size());
                    grouping.disposition = hasPersistentGap(first, last, supportCounts, workHistory,
                        workHistory.size(), config.minimumSupportRatio)
                        ? "persistent_low_occupancy_gap" : "historical_occupancy_coverage_below_threshold";
                    continue;
                }
                grouping.occupancyCoverage = coverage;
                const auto [currentKnownRatio, currentOccupiedRatio] =
                    currentOccupancy(band.startFrequencyHz, band.endFrequencyHz);
                grouping.currentKnownRatio = currentKnownRatio;
                grouping.currentOccupiedRatio = currentOccupiedRatio;
                // The detector's 16-frame maximum can retain an old signal.
                // Do not create or refresh a channel proposal unless this raw
                // PSD independently supports present occupancy in that band.
                if (currentKnownRatio < 0.5 || currentOccupiedRatio < 0.25) {
                    grouping.disposition = "current_raw_psd_does_not_support_occupancy";
                    continue;
                }
                std::size_t leftRows = 0, rightRows = 0;
                const auto firstCell = cellIndex(band.startFrequencyHz, frame, spectrum.unitHz, spectrum.occupied.size());
                const auto lastCell = cellIndex(std::nextafter(band.endFrequencyHz, band.startFrequencyHz),
                                                frame, spectrum.unitHz, spectrum.occupied.size());
                const auto midpoint = firstCell + (lastCell - firstCell) / 2;
                for (const auto& historyRow : workHistory) {
                    const auto leftActive = std::count(historyRow.occupied.begin() + static_cast<std::ptrdiff_t>(firstCell),
                                                       historyRow.occupied.begin() + static_cast<std::ptrdiff_t>(midpoint + 1), 1);
                    const auto rightActive = std::count(historyRow.occupied.begin() + static_cast<std::ptrdiff_t>(midpoint + 1),
                                                        historyRow.occupied.begin() + static_cast<std::ptrdiff_t>(lastCell + 1), 1);
                    if (leftActive) ++leftRows;
                    if (rightActive) ++rightRows;
                }
                if (leftRows < 2 || rightRows < 2) {
                    grouping.disposition = "insufficient_two_sided_history_support";
                    continue;
                }

                std::vector<const ChannelPrior*> matchingPriors;
                std::vector<const ChannelPrior*> overlappingPriors;
                for (const auto& prior : config.priors) {
                    if (!prior.enabled) continue;
                    if (band.endFrequencyHz >= static_cast<double>(prior.startFrequencyHz) &&
                        band.startFrequencyHz <= static_cast<double>(prior.endFrequencyHz)) {
                        overlappingPriors.push_back(&prior);
                        if (band.startFrequencyHz >= static_cast<double>(prior.startFrequencyHz) &&
                            band.endFrequencyHz <= static_cast<double>(prior.endFrequencyHz))
                            matchingPriors.push_back(&prior);
                    }
                }
                if (overlappingPriors.size() > 1 ||
                    (overlappingPriors.size() == 1 && matchingPriors.empty())) {
                    grouping.disposition = "conflicting_or_crossed_channel_prior";
                    continue;
                }

                const auto strongest = *std::max_element(component.members.begin(), component.members.end(),
                    [&](auto a, auto b) { return candidates[a].signal.confidence < candidates[b].signal.confidence; });
                const auto fromAverage = remeasureExcludingCandidates(averagePrefix, frame, band,
                    SpectrumBranch::Average, mergedCandidateRanges);
                const auto fromMaximum = remeasureExcludingCandidates(maximumPrefix, frame, band,
                    SpectrumBranch::Maximum, mergedCandidateRanges);
                if (fromMaximum.signal.signalLevelDbm > fromAverage.signal.signalLevelDbm)
                    band = fromMaximum.signal;
                else band = fromAverage.signal;
                band.branch = SpectrumBranch::Both;
                band.lastSeenNs = frame.timestampNs;
                const bool measurementValid = fromAverage.hasNoiseSamples || fromMaximum.hasNoiseSamples;
                if (measurementValid && cnrThresholdDb > 0.0F && band.snrDb < cnrThresholdDb) {
                    grouping.disposition = "full_band_cnr_below_threshold";
                    continue;
                }
                band.confidence = candidates[strongest].signal.confidence;
                std::vector<double> floors;
                for (std::size_t cell = firstCell; cell <= lastCell; ++cell)
                    if (spectrum.known[cell]) floors.push_back(spectrum.floor[cell]);
                Proposal proposal;
                proposal.measurement = band;
                proposal.contributors = std::move(refs);
                proposal.candidateCount = component.members.size();
                proposal.coverage = coverage;
                proposal.noiseFloorDbm = floors.empty() ? band.noiseLevelDbm :
                    std::accumulate(floors.begin(), floors.end(), 0.0) / floors.size();
                if (!matchingPriors.empty()) proposal.priorName = matchingPriors.front()->name;
                proposal.measurementValid = measurementValid;
                proposal.diagnosticIndex = diagnosticIndex;
                grouping.disposition = "awaiting_merge_confirmation";
                proposals.push_back(std::move(proposal));
            }
        }
        if (!config.enabled)
            for (auto& diagnostic : groupingDiagnostics)
                diagnostic.disposition = "channel_aggregation_disabled";
        if (!definitiveObservation) proposals.clear();

        std::vector<bool> proposalMatched(proposals.size(), false);
        std::vector<bool> channelMatched(workChannels.size(), false);
        std::vector<std::size_t> proposalBest(proposals.size(), workChannels.size());
        std::vector<std::size_t> proposalOptions(proposals.size(), 0);
        std::vector<std::size_t> channelOptions(workChannels.size(), 0);
        std::vector<bool> rawSuppressed(rawTracked.size(), false);
        std::vector<std::size_t> trackedCandidateIndex(rawTracked.size(), candidates.size());
        for (std::size_t ri = 0; ri < rawTracked.size(); ++ri) {
            const auto& raw = rawTracked[ri].stable;
            auto first = std::lower_bound(order.begin(), order.end(), raw.startFrequencyHz,
                [&](std::size_t index, double frequency) {
                    return candidates[index].signal.startFrequencyHz < frequency;
                });
            if (first != order.begin()) --first;
            double bestIou = 0.0;
            for (auto it = first; it != order.end() && candidates[*it].signal.startFrequencyHz < raw.endFrequencyHz; ++it) {
                const auto& candidate = candidates[*it].signal;
                const double overlap = overlapWidth(candidate, raw);
                const double united = candidate.bandwidthHz + raw.bandwidthHz - overlap;
                const double iou = united > 0.0 ? overlap / united : 0.0;
                if (iou > bestIou) {
                    bestIou = iou;
                    trackedCandidateIndex[ri] = *it;
                }
            }
        }
        std::vector<DetectedSignal> confirmedNewGroups;
        std::vector<std::pair<std::int64_t, std::int64_t>> splitParentsByRawTrack;
        for (std::size_t pi = 0; pi < proposals.size(); ++pi) {
            checkCancelled();
            double bestIou = 0.0;
            for (std::size_t ti = 0; ti < workChannels.size(); ++ti) {
                const double iou = intervalIou(proposals[pi].measurement, workChannels[ti].value.stable);
                if (iou < 0.6) continue;
                ++proposalOptions[pi];
                ++channelOptions[ti];
                if (iou > bestIou) { bestIou = iou; proposalBest[pi] = ti; }
            }
        }
        for (std::size_t pi = 0; pi < proposals.size(); ++pi) {
            const auto ti = proposalBest[pi];
            if (ti == workChannels.size() || proposalOptions[pi] != 1 || channelOptions[ti] != 1) continue;
            auto& track = workChannels[ti];
            channelMatched[ti] = proposalMatched[pi] = true;
            const auto identity = track.value.stable.id;
            const auto firstSeen = track.value.stable.firstSeenNs;
            const auto occurrences = track.value.stable.occurrenceCount + 1;
            const auto& observation = proposals[pi].measurement;
            track.boundaryHistory.emplace_back(observation.centerFrequencyHz, observation.bandwidthHz);
            while (track.boundaryHistory.size() > 5) track.boundaryHistory.pop_front();
            std::vector<double> centers, bandwidths;
            for (const auto& entry : track.boundaryHistory) {
                centers.push_back(entry.first);
                bandwidths.push_back(entry.second);
            }
            const double medianCenter = median(std::move(centers));
            const double medianBandwidth = median(std::move(bandwidths));
            if (!track.boundaryInitialized) {
                track.estimatedCenterHz = medianCenter;
                track.estimatedBandwidthHz = medianBandwidth;
                track.boundaryInitialized = true;
            } else {
                track.estimatedCenterHz = 0.65 * track.estimatedCenterHz + 0.35 * medianCenter;
                track.estimatedBandwidthHz = 0.65 * track.estimatedBandwidthHz + 0.35 * medianBandwidth;
            }
            const double domainStart = frame.startFrequencyHz;
            const double domainEnd = domainStart + frame.binWidthHz * frame.powerDb.size();
            double stableStart = std::clamp(track.estimatedCenterHz - track.estimatedBandwidthHz * 0.5,
                                            domainStart, domainEnd);
            double stableEnd = std::clamp(track.estimatedCenterHz + track.estimatedBandwidthHz * 0.5,
                                          domainStart, domainEnd);
            const auto binCount = static_cast<double>(frame.powerDb.size());
            const auto firstBin = std::clamp(std::floor((stableStart - domainStart) / frame.binWidthHz),
                                             0.0, std::max(0.0, binCount - 1.0));
            const auto lastBin = std::clamp(std::ceil((stableEnd - domainStart) / frame.binWidthHz),
                                            firstBin + 1.0, binCount);
            stableStart = domainStart + firstBin * frame.binWidthHz;
            stableEnd = domainStart + lastBin * frame.binWidthHz;
            auto stableBand = observation;
            stableBand.startFrequencyHz = stableStart;
            stableBand.endFrequencyHz = std::max(stableStart + frame.binWidthHz, stableEnd);
            stableBand.bandwidthHz = stableBand.endFrequencyHz - stableBand.startFrequencyHz;
            stableBand.centerFrequencyHz = stableBand.startFrequencyHz + stableBand.bandwidthHz * 0.5;
            const auto averageMeasure = remeasureExcludingCandidates(averagePrefix, frame, stableBand,
                SpectrumBranch::Average, mergedCandidateRanges);
            const auto maximumMeasure = remeasureExcludingCandidates(maximumPrefix, frame, stableBand,
                SpectrumBranch::Maximum, mergedCandidateRanges);
            const auto& selectedMeasure = maximumMeasure.signal.signalLevelDbm > averageMeasure.signal.signalLevelDbm
                ? maximumMeasure : averageMeasure;
            track.value.raw = observation;
            track.value.stable = selectedMeasure.signal;
            track.value.stable.branch = SpectrumBranch::Both;
            track.value.raw.id = track.value.stable.id = identity;
            track.value.stable.firstSeenNs = track.value.raw.firstSeenNs = firstSeen;
            track.value.stable.occurrenceCount = track.value.raw.occurrenceCount = occurrences;
            track.value.stable.lastSeenNs = track.value.raw.lastSeenNs = frame.timestampNs;
            track.value.aggregate = true;
            track.value.observationState = ObservationState::Observed;
            track.value.occupancyCoverage = proposals[pi].coverage;
            track.value.noiseFloorDbm = proposals[pi].noiseFloorDbm;
            track.value.priorName = proposals[pi].priorName;
            track.value.contributors = proposals[pi].contributors;
            track.value.measurementValid = proposals[pi].measurementValid && selectedMeasure.hasNoiseSamples;
            track.value.diagnostic = "信道聚合已确认；边界使用最近5次中位数与EMA稳定";
            track.missingCount = track.splitCount = 0;
            track.missingDurationNs = 0;
            track.lastObservedNs = frame.timestampNs;
            track.lastEvaluatedNs = frame.timestampNs;
            track.lastSequence = frame.sequence;
            output.push_back(track.value);
            auto& grouping = groupingDiagnostics[proposals[pi].diagnosticIndex];
            grouping.disposition = "confirmed_existing_channel";
            grouping.resultingChannelId = identity;
            for (std::size_t ri = 0; ri < rawTracked.size(); ++ri)
                if (overlapWidth(rawTracked[ri].stable, track.value.stable) > 0.0) rawSuppressed[ri] = true;
        }

        for (std::size_t pi = 0; pi < proposals.size(); ++pi) {
            if (proposalMatched[pi]) continue;
            auto found = std::find_if(workPending.begin(), workPending.end(), [&](const PendingProposal& pending) {
                return frame.sequence > pending.lastSequence &&
                       frame.timestampNs > pending.proposal.measurement.lastSeenNs &&
                       frame.timestampNs - pending.proposal.measurement.lastSeenNs <= maxContinuousGapNs &&
                       intervalIou(proposals[pi].measurement, pending.proposal.measurement) >= 0.6;
            });
            if (found == workPending.end()) {
                workPending.push_back({proposals[pi], 1, frame.sequence});
                continue;
            }
            found->proposal = proposals[pi];
            found->lastSequence = frame.sequence;
            ++found->hits;
            if (found->hits < config.mergeConfirmationCount) continue;
            TrackedChannel track;
            track.value.raw = proposals[pi].measurement;
            track.value.stable = proposals[pi].measurement;
            track.value.raw.id = track.value.stable.id = nextId--;
            track.value.aggregate = true;
            track.value.observationState = ObservationState::Observed;
            track.value.occupancyCoverage = proposals[pi].coverage;
            track.value.noiseFloorDbm = proposals[pi].noiseFloorDbm;
            track.value.priorName = proposals[pi].priorName;
            track.value.measurementValid = proposals[pi].measurementValid;
            track.value.contributors = proposals[pi].contributors;
            track.value.relatedChannelIds.clear();
            for (const auto& existing : workChannels)
                if (existing.value.stable.id != track.value.stable.id &&
                    overlapWidth(existing.value.stable, track.value.stable) > 0.0)
                    track.value.relatedChannelIds.push_back(existing.value.stable.id);
            for (const auto& existing : workIndividuals)
                if (overlapWidth(existing.value.stable, track.value.stable) > 0.0)
                    track.value.relatedChannelIds.push_back(existing.value.stable.id);
            std::sort(track.value.relatedChannelIds.begin(), track.value.relatedChannelIds.end());
            track.value.relatedChannelIds.erase(std::unique(track.value.relatedChannelIds.begin(),
                track.value.relatedChannelIds.end()), track.value.relatedChannelIds.end());
            track.value.diagnostic = "信道归并连续确认完成";
            track.value.raw.firstSeenNs = track.value.stable.firstSeenNs = frame.timestampNs;
            track.value.raw.lastSeenNs = track.value.stable.lastSeenNs = frame.timestampNs;
            track.value.raw.occurrenceCount = track.value.stable.occurrenceCount = 1;
            track.lastObservedNs = frame.timestampNs;
            track.lastEvaluatedNs = frame.timestampNs;
            track.lastSequence = frame.sequence;
            track.boundaryHistory.emplace_back(track.value.stable.centerFrequencyHz,
                                                track.value.stable.bandwidthHz);
            track.estimatedCenterHz = track.value.stable.centerFrequencyHz;
            track.estimatedBandwidthHz = track.value.stable.bandwidthHz;
            track.boundaryInitialized = true;
            auto& grouping = groupingDiagnostics[proposals[pi].diagnosticIndex];
            grouping.disposition = "confirmed_new_channel";
            grouping.resultingChannelId = track.value.stable.id;
            workChannels.push_back(track);
            channelMatched.push_back(true);
            confirmedNewGroups.push_back(track.value.stable);
            workPending.erase(found);
            for (std::size_t ri = 0; ri < rawTracked.size(); ++ri)
                if (overlapWidth(rawTracked[ri].stable, track.value.stable) > 0.0) rawSuppressed[ri] = true;
            workIndividuals.erase(std::remove_if(workIndividuals.begin(), workIndividuals.end(),
                [&](const TrackedIndividual& item) {
                    return overlapWidth(item.value.stable, track.value.stable) > 0.0;
                }), workIndividuals.end());
            output.push_back(track.value);
        }

        std::vector<std::int64_t> retiredChannelIds;
        for (std::size_t ti = workChannels.size(); ti-- > 0;) {
            const bool newlyCreated = std::any_of(confirmedNewGroups.begin(), confirmedNewGroups.end(),
                [&](const DetectedSignal& group) { return group.id == workChannels[ti].value.stable.id; });
            if (newlyCreated) continue;
            const bool absorbed = std::any_of(confirmedNewGroups.begin(), confirmedNewGroups.end(),
                [&](const DetectedSignal& group) {
                    return overlapWidth(group, workChannels[ti].value.stable) > 0.0;
                });
            if (!absorbed) continue;
            retiredChannelIds.push_back(workChannels[ti].value.stable.id);
            workChannels.erase(workChannels.begin() + static_cast<std::ptrdiff_t>(ti));
            channelMatched.erase(channelMatched.begin() + static_cast<std::ptrdiff_t>(ti));
        }
        if (!retiredChannelIds.empty())
            output.erase(std::remove_if(output.begin(), output.end(), [&](const auto& item) {
                return std::find(retiredChannelIds.begin(), retiredChannelIds.end(), item.stable.id) !=
                       retiredChannelIds.end();
            }), output.end());

        for (std::size_t ti = 0; ti < workChannels.size(); ++ti) {
            auto& track = workChannels[ti];
            if (!track.value.aggregate || channelMatched[ti]) continue;
            if (!definitiveObservation) {
                track.value.observationState = ObservationState::TemporarilyUnobserved;
                track.lastEvaluatedNs = frame.timestampNs;
                track.value.diagnostic = "当前结果不完整，信道状态未知";
                output.push_back(track.value);
                continue;
            }
            std::vector<std::size_t> overlappingRaw;
            for (std::size_t ri = 0; ri < rawTracked.size(); ++ri) {
                if (overlapWidth(rawTracked[ri].stable, track.value.stable) > 0.0) {
                    rawSuppressed[ri] = true;
                    overlappingRaw.push_back(ri);
                }
            }

            std::vector<const Component*> parts;
            for (const auto& component : components) {
                if (component.end > track.value.stable.startFrequencyHz &&
                    component.start < track.value.stable.endFrequencyHz)
                    parts.push_back(&component);
            }
            std::sort(parts.begin(), parts.end(), [](const Component* lhs, const Component* rhs) {
                return lhs->start < rhs->start;
            });
            bool persistentSplit = false;
            for (std::size_t part = 1; part < parts.size(); ++part) {
                const double gap = parts[part]->start - parts[part - 1]->end;
                if (gap < 2.0 * spectrum.unitHz) continue;
                const auto firstCell = cellIndex(parts[part - 1]->end, frame, spectrum.unitHz,
                                                 spectrum.occupied.size());
                const auto lastCell = cellIndex(parts[part]->start, frame, spectrum.unitHz,
                                                spectrum.occupied.size());
                if (hasPersistentGap(firstCell, lastCell, supportCounts, workHistory,
                                    workHistory.size(), config.minimumSupportRatio)) {
                    persistentSplit = true;
                    break;
                }
            }
            if (persistentSplit) {
                ++track.splitCount;
                if (track.splitCount < config.splitConfirmationCount) {
                    track.value.observationState = ObservationState::TemporarilyUnobserved;
                    track.value.missingCount = track.splitCount;
                    track.value.requiredMissingCount = config.splitConfirmationCount;
                    track.value.diagnostic = "信道结构变化待确认拆分";
                    output.push_back(track.value);
                    continue;
                }
                const auto parentId = track.value.stable.id;
                for (const auto rawIndex : overlappingRaw) {
                    rawSuppressed[rawIndex] = false;
                    splitParentsByRawTrack.emplace_back(rawTracked[rawIndex].stable.id, parentId);
                }
                workChannels.erase(workChannels.begin() + static_cast<std::ptrdiff_t>(ti));
                channelMatched.erase(channelMatched.begin() + static_cast<std::ptrdiff_t>(ti));
                --ti;
                continue;
            }
            track.splitCount = 0;

            const auto firstCell = cellIndex(track.value.stable.startFrequencyHz, frame,
                                             spectrum.unitHz, spectrum.occupied.size());
            const auto lastCell = cellIndex(std::nextafter(track.value.stable.endFrequencyHz,
                track.value.stable.startFrequencyHz), frame, spectrum.unitHz, spectrum.occupied.size());
            const auto [knownRatio, currentOccupiedRatio] = currentOccupancy(
                track.value.stable.startFrequencyHz, track.value.stable.endFrequencyHz);
            const double occupiedRatio = currentOccupiedRatio * knownRatio;
            const bool currentSignalEvidence = !overlappingRaw.empty() && knownRatio >= 0.5 &&
                                               currentOccupiedRatio >= 0.25;
            if (currentSignalEvidence) {
                const auto averageMeasure = remeasureExcludingCandidates(averagePrefix, frame,
                    track.value.stable, SpectrumBranch::Average, mergedCandidateRanges);
                const auto maximumMeasure = remeasureExcludingCandidates(maximumPrefix, frame,
                    track.value.stable, SpectrumBranch::Maximum, mergedCandidateRanges);
                const auto& selectedMeasure = maximumMeasure.signal.signalLevelDbm > averageMeasure.signal.signalLevelDbm
                    ? maximumMeasure : averageMeasure;
                auto measured = selectedMeasure.signal;
                const bool validMeasurement = selectedMeasure.hasNoiseSamples;
                if (!validMeasurement || cnrThresholdDb <= 0.0F || measured.snrDb >= cnrThresholdDb) {
                    const auto identity = track.value.stable.id;
                    const auto firstSeen = track.value.stable.firstSeenNs;
                    const auto oldCount = track.value.stable.occurrenceCount;
                    measured.id = identity;
                    measured.firstSeenNs = firstSeen;
                    measured.lastSeenNs = frame.timestampNs;
                    measured.occurrenceCount = oldCount + 1;
                    measured.branch = SpectrumBranch::Both;
                    track.value.raw = measured;
                    track.value.stable = measured;
                    track.value.measurementValid = validMeasurement;
                    track.value.contributors.clear();
                    for (const auto ri : overlappingRaw)
                        track.value.contributors.push_back({frame.sequence, static_cast<std::uint32_t>(ri)});
                    track.value.observationState = ObservationState::Observed;
                    track.value.occupancyCoverage = occupiedRatio;
                    track.value.diagnostic = "聚合信道由本帧片段与占用证据恢复";
                    track.missingCount = track.splitCount = 0;
                    track.missingDurationNs = 0;
                    track.lastObservedNs = track.lastEvaluatedNs = frame.timestampNs;
                    track.lastSequence = frame.sequence;
                    output.push_back(track.value);
                    continue;
                }
            }

            const bool reliableAbsence = overlappingRaw.empty() && knownRatio >= 0.75 &&
                                          occupiedRatio <= 0.20 && !longDataGap;
            if (reliableAbsence) {
                ++track.missingCount;
                if (track.lastEvaluatedNs > 0 && frame.timestampNs > track.lastEvaluatedNs &&
                    frame.timestampNs - track.lastEvaluatedNs <= maxContinuousGapNs)
                    track.missingDurationNs += frame.timestampNs - track.lastEvaluatedNs;
                track.lastEvaluatedNs = frame.timestampNs;
                const double missingSeconds = static_cast<double>(track.missingDurationNs) / 1e9;
                if (track.missingCount >= config.missingConfirmationCount &&
                    missingSeconds >= config.missingHoldSeconds) {
                    workChannels.erase(workChannels.begin() + static_cast<std::ptrdiff_t>(ti));
                    channelMatched.erase(channelMatched.begin() + static_cast<std::ptrdiff_t>(ti));
                    --ti;
                    continue;
                }
                track.value.observationState = ObservationState::TemporarilyUnobserved;
                track.value.missingCount = track.missingCount;
                track.value.requiredMissingCount = config.missingConfirmationCount;
                track.value.diagnostic = "信道可靠缺失待确认";
                output.push_back(track.value);
            } else {
                track.missingCount = 0;
                track.missingDurationNs = 0;
                track.lastEvaluatedNs = frame.timestampNs;
                track.value.observationState = ObservationState::TemporarilyUnobserved;
                track.value.missingCount = 0;
                track.value.requiredMissingCount = config.missingConfirmationCount;
                track.value.diagnostic = longDataGap || !overlappingRaw.empty()
                    ? "当前观测不确定，保留最近有效信道测量"
                    : "缺少可靠噪底证据，信道状态未知";
                output.push_back(track.value);
            }
        }

        for (std::size_t ri = 0; ri < rawTracked.size(); ++ri) {
            if (rawSuppressed[ri]) continue;
            const auto& raw = rawTracked[ri];
            auto found = std::find_if(workIndividuals.begin(), workIndividuals.end(), [&](const TrackedIndividual& item) {
                return item.rawTrackId == raw.stable.id;
            });
            TrackedIndividual item;
            if (found == workIndividuals.end()) {
                item.rawTrackId = raw.stable.id;
                item.value.raw = raw.raw;
                item.value.stable = raw.stable;
                item.value.raw.id = item.value.stable.id = nextId--;
                item.value.aggregate = false;
                item.value.boundaryState = raw.boundaryState;
                item.value.occupancyCoverage = 0.0;
                item.lastObservedNs = frame.timestampNs;
                item.lastEvaluatedNs = frame.timestampNs;
                item.lastSequence = frame.sequence;
                item.value.stable.occurrenceCount = raw.stable.occurrenceCount;
                item.value.observationState = ObservationState::Observed;
                workIndividuals.push_back(item);
                found = std::prev(workIndividuals.end());
            } else {
                found->value.raw = raw.raw;
                found->value.stable = raw.stable;
                found->value.stable.id = found->value.raw.id;
                found->value.boundaryState = raw.boundaryState;
                found->value.observationState = ObservationState::Observed;
                found->lastObservedNs = frame.timestampNs;
                found->lastEvaluatedNs = frame.timestampNs;
                found->missingDurationNs = 0;
                found->lastSequence = frame.sequence;
                found->missingCount = 0;
                found->value.diagnostic = raw.diagnostic;
            }
            found->value.contributors.clear();
            if (trackedCandidateIndex[ri] < candidates.size())
                found->value.contributors.push_back({frame.sequence,
                    static_cast<std::uint32_t>(trackedCandidateIndex[ri])});
            found->value.relatedChannelIds.clear();
            for (const auto& relation : splitParentsByRawTrack)
                if (relation.first == raw.stable.id)
                    found->value.relatedChannelIds.push_back(relation.second);
            std::sort(found->value.relatedChannelIds.begin(), found->value.relatedChannelIds.end());
            found->value.relatedChannelIds.erase(std::unique(found->value.relatedChannelIds.begin(),
                found->value.relatedChannelIds.end()), found->value.relatedChannelIds.end());
            output.push_back(found->value);
        }

        for (auto it = workIndividuals.begin(); it != workIndividuals.end();) {
            const bool observed = std::any_of(rawTracked.begin(), rawTracked.end(), [&](const auto& raw) {
                return raw.stable.id == it->rawTrackId;
            });
            const bool coveredByChannel = std::any_of(workChannels.begin(), workChannels.end(), [&](const auto& channel) {
                return channel.value.aggregate &&
                       overlapWidth(channel.value.stable, it->value.stable) > 0.0;
            });
            if (observed || coveredByChannel) { ++it; continue; }

            if (!definitiveObservation || longDataGap) {
                it->missingCount = 0;
                it->missingDurationNs = 0;
                it->lastEvaluatedNs = frame.timestampNs;
                it->value.observationState = ObservationState::TemporarilyUnobserved;
                it->value.missingCount = 0;
                it->value.requiredMissingCount = config.missingConfirmationCount;
                it->value.diagnostic = "当前数据不连续或结果不完整，信号状态未知";
                output.push_back(it->value);
                ++it;
                continue;
            }

            const auto firstCell = cellIndex(it->value.stable.startFrequencyHz, frame,
                                             spectrum.unitHz, spectrum.occupied.size());
            const auto lastCell = cellIndex(std::nextafter(it->value.stable.endFrequencyHz,
                it->value.stable.startFrequencyHz), frame, spectrum.unitHz, spectrum.occupied.size());
            std::size_t knownCells = 0, occupiedCells = 0;
            for (std::size_t cell = firstCell; cell <= lastCell; ++cell) {
                knownCells += spectrum.known[cell] != 0;
                occupiedCells += spectrum.occupied[cell] != 0;
            }
            const auto cellCount = lastCell - firstCell + 1;
            const bool reliableAbsence = static_cast<double>(knownCells) / cellCount >= 0.75 &&
                                         static_cast<double>(occupiedCells) / cellCount <= 0.20;
            if (!reliableAbsence) {
                it->missingCount = 0;
                it->missingDurationNs = 0;
                it->lastEvaluatedNs = frame.timestampNs;
                it->value.observationState = ObservationState::TemporarilyUnobserved;
                it->value.missingCount = 0;
                it->value.requiredMissingCount = config.missingConfirmationCount;
                it->value.diagnostic = "缺少可靠的当前占用证据，保留最近有效测量";
                output.push_back(it->value);
                ++it;
                continue;
            }

            ++it->missingCount;
            if (it->lastEvaluatedNs > 0 && frame.timestampNs > it->lastEvaluatedNs &&
                frame.timestampNs - it->lastEvaluatedNs <= maxContinuousGapNs)
                it->missingDurationNs += frame.timestampNs - it->lastEvaluatedNs;
            it->lastEvaluatedNs = frame.timestampNs;
            if (it->missingCount >= config.missingConfirmationCount &&
                static_cast<double>(it->missingDurationNs) / 1e9 >= config.missingHoldSeconds) {
                it = workIndividuals.erase(it);
                continue;
            }
            it->value.observationState = ObservationState::TemporarilyUnobserved;
            it->value.missingCount = it->missingCount;
            it->value.requiredMissingCount = config.missingConfirmationCount;
            it->value.diagnostic = "信号可靠缺失待确认";
            output.push_back(it->value);
            ++it;
        }

        std::sort(workPending.begin(), workPending.end(), [](const auto& a, const auto& b) {
            return a.lastSequence > b.lastSequence;
        });
        workPending.erase(std::remove_if(workPending.begin(), workPending.end(), [&](const auto& item) {
            return frame.sequence > item.lastSequence &&
                (frame.timestampNs < item.proposal.measurement.lastSeenNs ||
                 frame.timestampNs - item.proposal.measurement.lastSeenNs > maxContinuousGapNs);
        }), workPending.end());
        std::sort(output.begin(), output.end(), [](const auto& a, const auto& b) {
            return a.stable.startFrequencyHz < b.stable.startFrequencyHz;
        });
        checkCancelled();
        m_history = std::move(workHistory);
        m_pending = std::move(workPending);
        m_channels = std::move(workChannels);
        m_individuals = std::move(workIndividuals);
        m_nextBusinessId = nextId;
        if (longDataGap || !definitiveObservation) intervals.clear();
        if (definitiveObservation && !longDataGap && !sequenceReset &&
            m_lastTimestampNs > 0 && frame.timestampNs > m_lastTimestampNs &&
            frame.timestampNs - m_lastTimestampNs <= maxContinuousGapNs) {
            intervals.push_back(frame.timestampNs - m_lastTimestampNs);
            while (intervals.size() > 16) intervals.pop_front();
        }
        m_frameIntervalsNs = std::move(intervals);
        m_lastTimestampNs = frame.timestampNs;
        m_lastSequence = frame.sequence;
        m_previousObservationDefinitive = definitiveObservation;
        if (evidence) evidence->groupingDiagnostics = std::move(groupingDiagnostics);
        return true;
    } catch (...) {
        output.clear();
        return false;
    }
}

} // namespace scn::algorithm
