#pragma once

#include "../../../algorithm/types/DisplayTypes.h"

#include <QMetaType>
#include <QPolygonF>
#include <QObject>
#include <QMutex>

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace scn::app
{

struct SpectrumRenderRequest
{
    algorithm::DisplaySnapshotPtr snapshot;
    std::uint64_t requestId = 0;
    std::uint64_t generation = 0;
    double viewStartHz = 0.0;
    double viewEndHz = 0.0;
    double displayMinDb = -120.0;
    double displayMaxDb = 0.0;
    int plotWidth = 0;
    int plotHeight = 0;
    bool showMaxSpectrum = false;
    bool showAverageSpectrum = false;
    bool showCurrentSpectrum = true;
    bool interactivePreview = false;
};

struct SpectrumRenderResult
{
    std::uint64_t requestId = 0;
    std::uint64_t generation = 0;
    std::uint64_t frameSequence = 0;
    double viewStartHz = 0.0;
    double viewEndHz = 0.0;
    double displayMinDb = -120.0;
    double displayMaxDb = 0.0;
    int plotWidth = 0;
    int plotHeight = 0;
    QPolygonF currentUpper;
    QPolygonF currentLower;
    QPolygonF maxUpper;
    QPolygonF maxLower;
    QPolygonF averageUpper;
    QPolygonF averageLower;
};

Q_DECLARE_METATYPE(scn::app::SpectrumRenderResult)

class SpectrumRenderWorker final : public QObject
{
    Q_OBJECT

public:
    explicit SpectrumRenderWorker(QObject* parent = nullptr);

    // Thread-safe mailbox entry point. Only the newest request is retained.
    void submit(const SpectrumRenderRequest& request);
    void reset(std::uint64_t generation);

signals:
    void rendered(const scn::app::SpectrumRenderResult& result);

private slots:
    void processLatest();

private:
    SpectrumRenderResult render(const SpectrumRenderRequest& request);
    void updateAccumulatedTraces(const algorithm::DisplaySnapshotPtr& snapshot);
    void buildDisplayTrace(const std::vector<float>& values,
                           const SpectrumRenderRequest& request,
                           QPolygonF& upper,
                           QPolygonF& lower) const;
    static double normalizedY(float value, const SpectrumRenderRequest& request);
    void scheduleNextIfNeeded();

    QMutex m_mailboxMutex;
    std::optional<SpectrumRenderRequest> m_pendingRequest;
    bool m_resetPending = false;
    bool m_dispatchPosted = false;
    std::uint64_t m_pendingResetGeneration = 0;

    std::uint64_t m_currentGeneration = 0;
    std::uint64_t m_lastAccumulatedSequence = 0;
    std::uint64_t m_accumulatedFrameCount = 0;
    struct RecentFrame final
    {
        algorithm::DisplaySnapshotPtr snapshot;
        std::uint64_t serial = 0;
    };
    struct RollingPeak final
    {
        std::uint64_t serial = 0;
        float value = -200.0F;
    };
    static constexpr std::size_t kRollingFrameCount = 100;
    std::uint64_t m_nextFrameSerial = 0;
    algorithm::DisplaySnapshotPtr m_lastAccumulatedSnapshot;
    std::deque<RecentFrame> m_recentFrames;
    std::vector<std::deque<RollingPeak>> m_rollingPeaks;
    std::vector<double> m_rollingSums;
    std::vector<std::uint16_t> m_rollingCounts;
    std::vector<float> m_maxSpectrum;
    std::vector<float> m_averageSpectrum;
};

} // namespace scn::app
