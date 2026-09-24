#pragma once

#include "LabIO.h"
#include "../../algorithm/types/DisplayTypes.h"
#include "../../runtime/PerformanceMonitor.h"
#include "../../source/FileSource/FileSource.h"

#include <QFile>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <chrono>
#include <functional>
#include <memory>

namespace scn::lab
{

struct LabRunOptions
{
    algorithm::DetectionConfig config{};
    QString inputFile;
    QString configFile;
    QString referenceFile;
    QString outputFile;
    QString csvFile;
    QString dumpDirectory;
    std::size_t startFrame = 0;
    std::size_t maximumFrames = 0;
    std::size_t fallbackPointCount = 202242;
    std::int64_t fallbackCenterFrequencyHz = 2025000000LL;
    std::int64_t fallbackSpanHz = 3950000000LL;
    std::int64_t fallbackResolutionBandwidthHz = 50000LL;
    double fallbackReferenceLevelDbm = -20.0;
    std::uint32_t logicalFrameRateHz = 30;
    bool interactiveStep = false;
};

struct LabRunStatistics
{
    std::size_t processedFrames = 0;
    double elapsedSeconds = 0.0;
    double throughputHz = 0.0;
    double averageMs = 0.0;
    double p50Ms = 0.0;
    double p95Ms = 0.0;
};

struct LabFrameUpdate
{
    std::size_t fileFrameIndex = 0;
    std::size_t processedFrames = 0;
    std::size_t totalFrames = 0;
    algorithm::SpectrumFrame frame;
    algorithm::DetectionResult detection;
    algorithm::DisplaySnapshotPtr snapshot;
    LabRunStatistics statistics;
};

struct LabSessionInfo
{
    std::size_t frameCount = 0;
    std::size_t plannedFrames = 0;
    std::size_t startFrame = 0;
    double centerFrequencyHz = 0.0;
    double spanHz = 0.0;
    double resolutionBandwidthHz = 0.0;
    double referenceLevelDbm = 0.0;
    source::FileSourceMetadata metadata;
    QString modelInfo;
    QString stageDirectory;
};

enum class LabStepStatus
{
    Frame,
    End,
    Cancelled,
    Error
};

class LabSession final
{
public:
    explicit LabSession(std::unique_ptr<algorithm::IScnBackend> backend = {});
    ~LabSession();

    LabSession(const LabSession&) = delete;
    LabSession& operator=(const LabSession&) = delete;

    static bool inspectModel(const algorithm::DetectionConfig& config,
                             QString& modelInfo, QString& error);

    bool open(const LabRunOptions& options, QString& error);
    LabStepStatus processNext(LabFrameUpdate& update, QString& error,
                              const std::function<bool()>& cancelled = {});
    bool seek(std::size_t frameIndex, QString& error);
    void stop() noexcept;

    bool isOpen() const noexcept { return m_open; }
    std::size_t position() const noexcept { return m_source.position(); }
    std::size_t frameCount() const noexcept { return m_source.frameCount(); }
    std::size_t totalFrames() const noexcept { return m_plannedFrames; }
    std::size_t processedFrames() const noexcept { return m_processedFrames; }
    const source::FileSourceMetadata& metadata() const noexcept { return m_metadata; }
    double centerFrequencyHz() const noexcept { return m_centerHz; }
    double spanHz() const noexcept { return m_spanHz; }
    double resolutionBandwidthHz() const noexcept { return m_rbwHz; }
    double referenceLevelDbm() const noexcept { return m_referenceLevelDbm; }
    const QString& stageDirectory() const noexcept { return m_stageDirectory; }
    const QString& modelInfo() const noexcept { return m_modelInfo; }
    LabRunStatistics statistics() const noexcept;

private:
    bool prepareExports(QString& error);
    bool writeResult(const LabFrameUpdate& update, QString& error);

    algorithm::DetectionEngine m_engine;
    source::FileSource m_source;
    LabRunOptions m_options;
    source::FileSourceMetadata m_metadata;
    std::unique_ptr<StageExporter> m_exporter;
    QFile m_output;
    QFile m_csv;
    QFile m_comparison;
    QFile m_reference;
    QTextStream m_csvStream;
    QJsonObject m_manifest;
    QString m_stageDirectory;
    QString m_runId;
    QString m_modelInfo;
    std::size_t m_plannedFrames = 0;
    std::size_t m_processedFrames = 0;
    double m_centerHz = 0.0;
    double m_spanHz = 0.0;
    double m_rbwHz = 0.0;
    double m_referenceLevelDbm = 0.0;
    bool m_open = false;
    runtime::PerformanceMonitor m_performance;
    std::chrono::steady_clock::time_point m_runStarted{};
};

} // namespace scn::lab

Q_DECLARE_METATYPE(scn::lab::LabFrameUpdate)
Q_DECLARE_METATYPE(scn::lab::LabSessionInfo)
