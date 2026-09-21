#pragma once
#include "../../algorithm/DetectionEngine/DetectionEngine.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>

class QFile;

namespace scn::lab
{
algorithm::DetectionConfig readConfiguration(const QString& path);
QJsonObject configurationJson(const algorithm::DetectionConfig& config);
QJsonObject resultJson(const algorithm::DetectionResult& result, std::size_t fileFrameIndex);
QJsonObject compareResults(const QJsonObject& reference, const QJsonObject& actual, double binWidthHz);

// Resolve existing ancestors before comparing prospective outputs. Existing
// files are additionally compared by filesystem identity (including hardlinks).
QString resolvedPath(const QString& path);
void validateExportPaths(const QStringList& inputs, const QStringList& outputs,
                         const QString& stageDirectory = {});
void openNewOutput(QFile& file, const QString& path);
void writeJsonFile(const QString& path, const QJsonObject& object);
QJsonObject modelProvenance(const algorithm::DetectionConfig& config, const QString& modelInfo);

class StageExporter final : public algorithm::DetectionObserver
{
public:
    explicit StageExporter(QString directory);
    void beginFrame(const algorithm::SpectrumFrame& frame, std::size_t fileFrameIndex);
    void accumulated(const algorithm::SpectrumFrame&, const std::vector<float>&,
                     const std::vector<float>&) override;
    void window(std::uint64_t, algorithm::SpectrumBranch, std::size_t, std::size_t,
                const std::vector<float>&, const algorithm::ScnModelOutput&,
                const std::vector<algorithm::ScnCandidate>&,
                const std::vector<algorithm::DetectedSignal>&) override;
    void fused(const algorithm::DetectionResult&) override;
private:
    QString m_directory, m_prefix;
    std::uint64_t m_lastSequence = 0, m_run = 0;
    std::size_t m_fileFrameIndex = 0;
};
}
