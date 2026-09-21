#include "SpectrumRenderWorker.h"

#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace scn::app
{

SpectrumRenderWorker::SpectrumRenderWorker(QObject* parent)
    : QObject(parent)
{
}

void SpectrumRenderWorker::submit(const SpectrumRenderRequest& request)
{
    bool postDispatch = false;
    {
        QMutexLocker locker(&m_mailboxMutex);
        m_pendingRequest = request;
        if (!m_dispatchPosted) {
            m_dispatchPosted = true;
            postDispatch = true;
        }
    }

    if (postDispatch) {
        QMetaObject::invokeMethod(this, &SpectrumRenderWorker::processLatest,
                                  Qt::QueuedConnection);
    }
}

void SpectrumRenderWorker::reset(std::uint64_t generation)
{
    bool postDispatch = false;
    {
        QMutexLocker locker(&m_mailboxMutex);
        m_pendingRequest.reset();
        m_resetPending = true;
        m_pendingResetGeneration = generation;
        if (!m_dispatchPosted) {
            m_dispatchPosted = true;
            postDispatch = true;
        }
    }

    if (postDispatch) {
        QMetaObject::invokeMethod(this, &SpectrumRenderWorker::processLatest,
                                  Qt::QueuedConnection);
    }
}

void SpectrumRenderWorker::processLatest()
{
    SpectrumRenderRequest request;
    bool hasRequest = false;
    {
        QMutexLocker locker(&m_mailboxMutex);
        if (m_resetPending) {
            m_currentGeneration = m_pendingResetGeneration;
            m_lastAccumulatedSequence = 0;
            m_accumulatedFrameCount = 0;
            m_nextFrameSerial = 0;
            m_lastAccumulatedSnapshot.reset();
            m_recentFrames.clear();
            m_rollingPeaks.clear();
            m_rollingSums.clear();
            m_rollingCounts.clear();
            m_maxSpectrum.clear();
            m_averageSpectrum.clear();
            m_resetPending = false;
        }
        if (m_pendingRequest) {
            request = std::move(*m_pendingRequest);
            m_pendingRequest.reset();
            hasRequest = true;
        }
        if (!hasRequest) {
            m_dispatchPosted = false;
            return;
        }
    }

    if (request.generation >= m_currentGeneration && request.snapshot) {
        const SpectrumRenderResult result = render(request);
        emit rendered(result);
    }

    scheduleNextIfNeeded();
}

void SpectrumRenderWorker::scheduleNextIfNeeded()
{
    bool postDispatch = false;
    {
        QMutexLocker locker(&m_mailboxMutex);
        if (!m_pendingRequest && !m_resetPending) {
            m_dispatchPosted = false;
            return;
        }
        // Keep the mailbox marked as posted so a concurrent submit() does not
        // enqueue a second event while this handoff is being scheduled.
        postDispatch = true;
    }

    if (postDispatch) {
        QMetaObject::invokeMethod(this, &SpectrumRenderWorker::processLatest,
                                  Qt::QueuedConnection);
    }
}

SpectrumRenderResult SpectrumRenderWorker::render(const SpectrumRenderRequest& request)
{
    SpectrumRenderResult result;
    result.requestId = request.requestId;
    result.generation = request.generation;
    result.frameSequence = request.snapshot ? request.snapshot->frame.sequence : 0;
    result.viewStartHz = request.viewStartHz;
    result.viewEndHz = request.viewEndHz;
    result.displayMinDb = request.displayMinDb;
    result.displayMaxDb = request.displayMaxDb;
    result.plotWidth = request.plotWidth;
    result.plotHeight = request.plotHeight;

    if (!request.snapshot || !request.snapshot->frame.isValid() ||
        request.plotWidth <= 0 || request.plotHeight <= 0) {
        return result;
    }

    const auto& frame = request.snapshot->frame;
    if (m_accumulatedFrameCount == 0 || request.snapshot != m_lastAccumulatedSnapshot) {
        updateAccumulatedTraces(request.snapshot);
        m_lastAccumulatedSequence = frame.sequence;
    }

    if (request.showCurrentSpectrum) {
        buildDisplayTrace(frame.powerDb, request, result.currentUpper, result.currentLower);
    }
    if (request.showMaxSpectrum) {
        buildDisplayTrace(m_maxSpectrum, request, result.maxUpper, result.maxLower);
    }
    if (request.showAverageSpectrum) {
        buildDisplayTrace(m_averageSpectrum, request,
                          result.averageUpper, result.averageLower);
    }
    return result;
}

void SpectrumRenderWorker::updateAccumulatedTraces(
    const algorithm::DisplaySnapshotPtr& snapshot)
{
    if (!snapshot || !snapshot->frame.isValid()) return;
    const auto& frame = snapshot->frame;
    const std::size_t pointCount = frame.powerDb.size();
    if (pointCount == 0) return;

    if (m_maxSpectrum.size() != pointCount || m_averageSpectrum.size() != pointCount) {
        m_recentFrames.clear();
        m_rollingPeaks.assign(pointCount, {});
        m_rollingSums.assign(pointCount, 0.0);
        m_rollingCounts.assign(pointCount, 0);
        m_maxSpectrum.assign(pointCount, -200.0F);
        m_averageSpectrum.assign(pointCount, -200.0F);
        m_accumulatedFrameCount = 0;
    }

    if (m_recentFrames.size() >= kRollingFrameCount) {
        const RecentFrame expired = m_recentFrames.front();
        m_recentFrames.pop_front();
        if (expired.snapshot && expired.snapshot->frame.powerDb.size() == pointCount) {
            const auto& expiredValues = expired.snapshot->frame.powerDb;
            for (std::size_t index = 0; index < pointCount; ++index) {
                const float value = expiredValues[index];
                if (!std::isfinite(value)) continue;
                m_rollingSums[index] -= value;
                if (m_rollingCounts[index] > 0) --m_rollingCounts[index];
                if (!m_rollingPeaks[index].empty() &&
                    m_rollingPeaks[index].front().serial == expired.serial) {
                    m_rollingPeaks[index].pop_front();
                }
            }
        }
    }

    const std::uint64_t serial = ++m_nextFrameSerial;
    for (std::size_t index = 0; index < pointCount; ++index) {
        const float value = frame.powerDb[index];
        if (!std::isfinite(value)) continue;
        m_rollingSums[index] += value;
        ++m_rollingCounts[index];
        auto& peak = m_rollingPeaks[index];
        while (!peak.empty() && peak.back().value <= value) peak.pop_back();
        peak.push_back({serial, value});
    }
    m_recentFrames.push_back({snapshot, serial});
    m_accumulatedFrameCount = m_recentFrames.size();
    m_lastAccumulatedSnapshot = snapshot;

    for (std::size_t index = 0; index < pointCount; ++index) {
        m_maxSpectrum[index] = m_rollingPeaks[index].empty()
            ? -200.0F : m_rollingPeaks[index].front().value;
        m_averageSpectrum[index] = m_rollingCounts[index] == 0
            ? -200.0F
            : static_cast<float>(m_rollingSums[index] /
                                 static_cast<double>(m_rollingCounts[index]));
    }
}

double SpectrumRenderWorker::normalizedY(float value, const SpectrumRenderRequest& request)
{
    const double range = request.displayMaxDb - request.displayMinDb;
    if (!std::isfinite(value) || range <= 0.0) return static_cast<double>(request.plotHeight);
    const double normalized = std::clamp(
        (static_cast<double>(value) - request.displayMinDb) / range, 0.0, 1.0);
    return static_cast<double>(request.plotHeight) * (1.0 - normalized);
}

void SpectrumRenderWorker::buildDisplayTrace(const std::vector<float>& values,
                                             const SpectrumRenderRequest& request,
                                             QPolygonF& upper,
                                             QPolygonF& lower) const
{
    upper.clear();
    lower.clear();
    if (!request.snapshot || values.empty() || request.plotWidth <= 0 ||
        request.plotHeight <= 0 || !request.snapshot->frame.isValid()) {
        return;
    }

    const auto& frame = request.snapshot->frame;
    const double fullStartHz = frame.startFrequencyHz;
    const double fullEndHz = frame.endFrequencyHz();
    double requestedStartHz = std::isfinite(request.viewStartHz)
        ? request.viewStartHz : fullStartHz;
    double requestedEndHz = std::isfinite(request.viewEndHz)
        ? request.viewEndHz : fullEndHz;
    if (!(requestedEndHz > requestedStartHz)) {
        requestedStartHz = fullStartHz;
        requestedEndHz = fullEndHz;
    }
    const double viewStartHz = std::clamp(requestedStartHz, fullStartHz, fullEndHz);
    const double viewEndHz = std::clamp(std::max(requestedEndHz, viewStartHz),
                                        viewStartHz, fullEndHz);
    const std::size_t lastValue = values.size() - 1;
    const std::size_t firstVisibleIndex = std::clamp(
        static_cast<std::size_t>(std::max(
            0.0, std::floor((viewStartHz - fullStartHz) / frame.binWidthHz))),
        std::size_t(0), lastValue);
    const std::size_t lastVisibleIndex = std::min(
        values.size(),
        std::max(firstVisibleIndex + 1,
                 static_cast<std::size_t>(std::max(
                     1.0, std::ceil((viewEndHz - fullStartHz) / frame.binWidthHz)))));
    const std::size_t visibleCount = lastVisibleIndex - firstVisibleIndex;
    if (visibleCount == 0) return;

    const std::size_t targetColumns = std::min<std::size_t>(
        visibleCount,
        static_cast<std::size_t>(std::max(
            1, request.interactivePreview ? request.plotWidth / 2 : request.plotWidth)));
    if (targetColumns == 0) return;

    upper.reserve(static_cast<int>(targetColumns));
    lower.reserve(static_cast<int>(targetColumns));
    for (std::size_t column = 0; column < targetColumns; ++column) {
        const std::size_t begin = targetColumns == visibleCount
            ? firstVisibleIndex + column
            : firstVisibleIndex + (column * visibleCount) / targetColumns;
        const std::size_t end = targetColumns == visibleCount
            ? begin + 1
            : std::max(begin + 1,
                       firstVisibleIndex + ((column + 1) * visibleCount) / targetColumns);

        float minimum = std::numeric_limits<float>::infinity();
        float maximum = -std::numeric_limits<float>::infinity();
        for (std::size_t index = begin; index < end && index < values.size(); ++index) {
            const float value = values[index];
            if (!std::isfinite(value)) continue;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        if (!std::isfinite(minimum) || !std::isfinite(maximum)) continue;

        const double bucketStartHz = std::max(
            viewStartHz, fullStartHz + static_cast<double>(begin) * frame.binWidthHz);
        const double bucketEndHz = std::min(
            viewEndHz, fullStartHz + static_cast<double>(end) * frame.binWidthHz);
        const double bucketFrequencyHz = bucketEndHz > bucketStartHz
            ? (bucketStartHz + bucketEndHz) * 0.5
            : fullStartHz + static_cast<double>(begin) * frame.binWidthHz;
        const double x = request.plotWidth > 1
            ? std::clamp((bucketFrequencyHz - viewStartHz) /
                         (viewEndHz - viewStartHz), 0.0, 1.0) *
                static_cast<double>(request.plotWidth - 1)
            : 0.0;

        // Keep one display trace, but retain both extrema of every source
        // bucket.  Drawing only the maximum value makes the spectrum appear
        // to lose a large amount of data; drawing `upper` and `lower` as two
        // independent polylines creates the duplicate-trace artifact.  A
        // same-X max/min pair is the usual raster envelope representation:
        // it preserves narrow peaks and the complete noise-floor excursion
        // without introducing a second visible curve.
        const QPointF maximumPoint(x, normalizedY(maximum, request));
        const QPointF minimumPoint(x, normalizedY(minimum, request));
        upper.append(maximumPoint);
        if (minimum != maximum) upper.append(minimumPoint);
    }
}

} // namespace scn::app
