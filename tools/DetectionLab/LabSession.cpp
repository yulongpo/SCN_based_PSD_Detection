#include "LabSession.h"

#include "../../common/Frequency.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSysInfo>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace scn::lab
{

namespace
{
QString frequencyParseError(const std::string& error)
{
    return QString::fromStdString(error.empty() ? "DetectionLab initialization failed." : error);
}

void writeJsonLine(QFile& file, const QJsonObject& object)
{
    auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    bytes += '\n';
    if (file.write(bytes) != bytes.size() || !file.flush())
        throw std::runtime_error("Output write failed.");
}

QString exceptionText(const std::exception& error)
{
    return QString::fromUtf8(error.what());
}

void validateSessionExportPaths(const LabRunOptions& options, const QString& stageDirectory)
{
    const auto outputPath = options.outputFile.isEmpty() ? QString() : resolvedPath(options.outputFile);
    const auto csvPath = options.csvFile.isEmpty() ? QString() : resolvedPath(options.csvFile);
    const auto comparisonPath = options.referenceFile.isEmpty()
        ? QString() : outputPath + QStringLiteral(".comparison.jsonl");
    QStringList inputs{options.inputFile, options.configFile, options.referenceFile,
                       QString::fromStdString(options.config.detector.modelPath)};
    QStringList outputs;
    for (const auto& path : {outputPath, csvPath}) {
        if (!path.isEmpty()) {
            outputs.push_back(path);
            outputs.push_back(path + QStringLiteral(".manifest.json"));
        }
    }
    if (!comparisonPath.isEmpty()) outputs.push_back(comparisonPath);
    validateExportPaths(inputs, outputs, stageDirectory);
}
}

LabSession::LabSession(std::unique_ptr<algorithm::IScnBackend> backend)
    : m_engine(std::move(backend))
{
}

LabSession::~LabSession()
{
    stop();
    m_engine.setObserver(nullptr);
}

bool LabSession::inspectModel(const algorithm::DetectionConfig& suppliedConfig,
                              QString& modelInfo, QString& error)
{
    try {
        auto config = suppliedConfig;
        config.enabled = true;
        if (!config.detector.modelPath.empty() &&
            QDir::isRelativePath(QString::fromStdString(config.detector.modelPath))) {
            config.detector.modelPath = QDir(QCoreApplication::applicationDirPath())
                .absoluteFilePath(QString::fromStdString(config.detector.modelPath)).toStdString();
        }
        algorithm::DetectionEngine engine;
        if (!engine.initialize(config)) {
            error = frequencyParseError(engine.lastError()) + "\n" + QString::fromStdString(engine.modelInfo());
            return false;
        }
        modelInfo = QString::fromStdString(engine.modelInfo());
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exceptionText(exception);
        return false;
    }
}

bool LabSession::open(const LabRunOptions& suppliedOptions, QString& error)
{
    if (m_open) {
        error = QStringLiteral("This DetectionLab session is already open.");
        return false;
    }
    try {
        m_options = suppliedOptions;
        if (m_options.inputFile.isEmpty()) {
            error = QStringLiteral("A spectrum input file is required.");
            return false;
        }
        if (m_options.logicalFrameRateHz == 0 || m_options.logicalFrameRateHz > 1'000'000) {
            error = QStringLiteral("Logical frame rate must be in [1, 1000000].");
            return false;
        }
        if (m_options.config.detector.inputLength == 0 || m_options.config.detector.windowStep == 0) {
            error = QStringLiteral("Detection window length and step must be greater than zero.");
            return false;
        }
        if (m_options.config.enabled && m_options.config.detector.modelPath.empty()) {
            error = QStringLiteral("A TensorRT engine path is required.");
            return false;
        }
        const auto modelPath = QString::fromStdString(m_options.config.detector.modelPath);
        if (!modelPath.isEmpty() && QDir::isRelativePath(modelPath)) {
            m_options.config.detector.modelPath = QDir(QCoreApplication::applicationDirPath())
                .absoluteFilePath(modelPath).toStdString();
        }

        source::SourceConfig sourceConfig;
        sourceConfig.kind = algorithm::SourceKind::File;
        sourceConfig.filePath = m_options.inputFile.toStdString();
        sourceConfig.loopFile = false;
        sourceConfig.frameRateHz = static_cast<int>(m_options.logicalFrameRateHz);
        sourceConfig.pointCount = m_options.fallbackPointCount;
        sourceConfig.centerFrequencyHz = m_options.fallbackCenterFrequencyHz;
        sourceConfig.bandwidthHz = m_options.fallbackSpanHz;
        sourceConfig.resolutionBandwidthHz = m_options.fallbackResolutionBandwidthHz;
        sourceConfig.referenceLevelDbm = m_options.fallbackReferenceLevelDbm;

        std::string sourceError;
        if (!source::FileSource::inspectFile(sourceConfig.filePath, m_metadata, sourceError)) {
            error = QString::fromStdString(sourceError);
            return false;
        }

        m_centerHz = static_cast<double>(m_metadata.hasCenterFrequency
            ? m_metadata.centerFrequencyHz : sourceConfig.centerFrequencyHz);
        m_spanHz = static_cast<double>(m_metadata.hasBandwidth
            ? m_metadata.bandwidthHz : sourceConfig.bandwidthHz);
        m_rbwHz = static_cast<double>(m_metadata.hasResolutionBandwidth
            ? m_metadata.resolutionBandwidthHz : sourceConfig.resolutionBandwidthHz);
        m_referenceLevelDbm = m_metadata.hasReferenceLevel
            ? m_metadata.referenceLevelDbm : sourceConfig.referenceLevelDbm;
        const auto points = m_metadata.hasSpectrumLength ? m_metadata.spectrumLength : sourceConfig.pointCount;
        if (!points || !std::isfinite(m_centerHz) || !std::isfinite(m_spanHz) || m_spanHz <= 0.0 ||
            !std::isfinite(m_centerHz - m_spanHz / 2.0) || !std::isfinite(m_centerHz + m_spanHz / 2.0) ||
            !std::isfinite(m_rbwHz) || !std::isfinite(m_referenceLevelDbm)) {
            error = QStringLiteral("Input filename and fallback settings do not describe a finite spectrum frame.");
            return false;
        }
        sourceConfig.pointCount = points;
        sourceConfig.centerFrequencyHz = static_cast<std::int64_t>(m_centerHz);
        sourceConfig.bandwidthHz = static_cast<std::int64_t>(m_spanHz);
        sourceConfig.resolutionBandwidthHz = static_cast<std::int64_t>(m_rbwHz);
        sourceConfig.referenceLevelDbm = m_referenceLevelDbm;

        std::string configError;
        if (!algorithm::validateConfig(m_options.config, configError)) {
            error = QString::fromStdString(configError);
            return false;
        }
        if (!m_options.referenceFile.isEmpty() && m_options.outputFile.isEmpty()) {
            error = QStringLiteral("Reference comparison requires JSONL output.");
            return false;
        }
        if (!m_options.referenceFile.isEmpty() && m_options.interactiveStep) {
            error = QStringLiteral("Reference comparison requires sequential replay; disable interactive stepping.");
            return false;
        }

        m_runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto stageRoot = m_options.dumpDirectory.isEmpty()
            ? QString() : resolvedPath(m_options.dumpDirectory);
        m_stageDirectory = stageRoot.isEmpty() ? QString()
            : QDir(stageRoot).filePath(QStringLiteral("run-") + m_runId);
        validateSessionExportPaths(m_options, m_stageDirectory);

        if (!m_engine.initialize(m_options.config)) {
            error = frequencyParseError(m_engine.lastError()) + "\n" + QString::fromStdString(m_engine.modelInfo());
            return false;
        }
        m_modelInfo = QString::fromStdString(m_engine.modelInfo());
        if (!m_source.open(sourceConfig, sourceError)) {
            error = QString::fromStdString(sourceError);
            return false;
        }
        if (m_options.startFrame >= m_source.frameCount()) {
            error = QStringLiteral("Start frame is outside the complete frames in this file.");
            return false;
        }
        if (!m_source.seekFrame(m_options.startFrame, sourceError) || !m_source.start()) {
            error = QString::fromStdString(sourceError.empty() ? "Unable to start the spectrum file source." : sourceError);
            return false;
        }
        const auto available = m_source.frameCount() - m_options.startFrame;
        m_plannedFrames = m_options.maximumFrames
            ? std::min(m_options.maximumFrames, available) : available;
        if (!prepareExports(error)) {
            stop();
            return false;
        }
        m_runStarted = std::chrono::steady_clock::now();
        m_open = true;
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exceptionText(exception);
        stop();
        return false;
    }
}

bool LabSession::prepareExports(QString& error)
{
    const bool hasExports = !m_options.outputFile.isEmpty() || !m_options.csvFile.isEmpty() ||
                            !m_options.dumpDirectory.isEmpty();
    if (!hasExports && m_options.referenceFile.isEmpty()) return true;

    const auto outputPath = m_options.outputFile.isEmpty()
        ? QString() : resolvedPath(m_options.outputFile);
    const auto csvPath = m_options.csvFile.isEmpty()
        ? QString() : resolvedPath(m_options.csvFile);
    const auto comparisonPath = m_options.referenceFile.isEmpty()
        ? QString() : outputPath + QStringLiteral(".comparison.jsonl");

    QStringList inputs{m_options.inputFile, m_options.configFile, m_options.referenceFile,
                       QString::fromStdString(m_options.config.detector.modelPath)};
    QStringList outputs;
    QStringList manifestPaths;
    for (const auto& path : {outputPath, csvPath}) {
        if (path.isEmpty()) continue;
        outputs.push_back(path);
        manifestPaths.push_back(path + QStringLiteral(".manifest.json"));
    }
    if (!comparisonPath.isEmpty()) outputs.push_back(comparisonPath);
    outputs.append(manifestPaths);
    validateExportPaths(inputs, outputs, m_stageDirectory);

    const auto model = modelProvenance(m_options.config, m_modelInfo);
    const QFileInfo inputInfo(m_options.inputFile);
    m_manifest = {{"schemaVersion", 1}, {"runId", m_runId},
        {"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"inputFile", inputInfo.absoluteFilePath()}, {"inputCanonicalPath", resolvedPath(m_options.inputFile)},
        {"inputSizeBytes", QString::number(m_metadata.fileSizeBytes)},
        {"inputModifiedUtc", inputInfo.lastModified().toUTC().toString(Qt::ISODateWithMs)},
        {"configFile", m_options.configFile.isEmpty() ? QString() : resolvedPath(m_options.configFile)},
        {"referenceFile", m_options.referenceFile.isEmpty() ? QString() : resolvedPath(m_options.referenceFile)},
        {"logicalFps", static_cast<int>(m_options.logicalFrameRateHz)},
        {"startFrame", static_cast<qint64>(m_options.startFrame)},
        {"requestedMaxFrames", QString::number(static_cast<qulonglong>(m_options.maximumFrames))},
        {"inputFrameCount", QString::number(static_cast<qulonglong>(m_source.frameCount()))},
        {"plannedSequentialFrames", QString::number(static_cast<qulonglong>(m_plannedFrames))},
        {"interactiveSeekEnabled", m_options.interactiveStep},
        {"timestampBasis", "zero-based fileFrameIndex / logicalFps; not wall clock"},
        {"manifestScope", "Configuration and input geometry, not a completion record; failures may leave partial exports."},
        {"modelInfo", m_modelInfo}, {"modelSha256", model.value("sha256")}, {"model", model},
        {"config", configurationJson(m_options.config)},
        {"frameInfo", QJsonObject{{"pointCount", static_cast<qint64>(m_source.frameCount() ?
                (m_metadata.hasSpectrumLength ? m_metadata.spectrumLength : m_options.fallbackPointCount) : 0)},
            {"startHz", m_centerHz - m_spanHz / 2.0}, {"endHz", m_centerHz + m_spanHz / 2.0},
            {"binHz", m_spanHz / static_cast<double>(m_metadata.hasSpectrumLength
                ? m_metadata.spectrumLength : m_options.fallbackPointCount)},
            {"referenceLevelDbm", m_referenceLevelDbm}, {"resolutionBandwidthHz", m_rbwHz},
            {"sourceName", "FILE"}, {"format", m_metadata.isBinary ? "native_float32" : "text"},
            {"binaryTrailingBytes", m_metadata.isBinary
                ? QString::number(m_metadata.fileSizeBytes % (static_cast<std::uint64_t>(
                    m_metadata.hasSpectrumLength ? m_metadata.spectrumLength : m_options.fallbackPointCount) * sizeof(float)))
                : QString()} }},
        {"exports", QJsonObject{{"jsonl", outputPath}, {"csv", csvPath},
            {"comparison", comparisonPath}, {"stageDirectory", m_stageDirectory}}},
        {"stageFloatFormat", QSysInfo::ByteOrder == QSysInfo::LittleEndian ? "float32_le" : "float32_be"}};

    validateExportPaths(inputs, outputs, m_stageDirectory);
    if (!m_stageDirectory.isEmpty()) {
        m_exporter = std::make_unique<StageExporter>(m_stageDirectory);
        m_engine.setObserver(m_exporter.get());
        manifestPaths.push_back(QDir(m_stageDirectory).filePath(QStringLiteral("manifest.json")));
    }
    m_manifest.insert(QStringLiteral("manifests"), QJsonArray::fromStringList(manifestPaths));
    for (const auto& path : manifestPaths) writeJsonFile(path, m_manifest);
    if (!outputPath.isEmpty()) openNewOutput(m_output, outputPath);
    if (!csvPath.isEmpty()) {
        openNewOutput(m_csv, csvPath);
        const QByteArray header("fileFrameIndex,sequence,id,startHz,endHz,centerHz,bandwidthHz,confidence,signalDbm,noiseDbm,cnrDb,branch,firstSeenNs,lastSeenNs,occurrences,stableStartHz,stableEndHz,stableCenterHz,stableBandwidthHz,stableSignalDbm,stableNoiseDbm,stableCnrDb,boundaryState,pendingCount,requiredCount,measurementBranch,associationIoU,centerDistanceHz,bandwidthRatio\n");
        if (m_csv.write(header) != header.size() || !m_csv.flush())
            throw std::runtime_error("CSV header write failed.");
    }
    if (!m_options.referenceFile.isEmpty()) {
        m_reference.setFileName(m_options.referenceFile);
        if (!m_reference.open(QIODevice::ReadOnly))
            throw std::runtime_error("Cannot open reference JSONL.");
        openNewOutput(m_comparison, comparisonPath);
    }
    error.clear();
    return true;
}

LabStepStatus LabSession::processNext(LabFrameUpdate& update, QString& error,
                                      const std::function<bool()>& cancelled)
{
    if (!m_open) {
        error = QStringLiteral("DetectionLab session is not open.");
        return LabStepStatus::Error;
    }
    if (m_source.position() >= m_source.frameCount() ||
        (m_options.maximumFrames && m_processedFrames >= m_options.maximumFrames)) {
        error.clear();
        return LabStepStatus::End;
    }

    try {
        update = {};
        update.fileFrameIndex = m_source.position();
        if (!m_source.read(update.frame)) {
            error = QStringLiteral("Unexpected file read failure before complete frame count.");
            return LabStepStatus::Error;
        }
        if (m_exporter) m_exporter->beginFrame(update.frame, update.fileFrameIndex);
        update.detection = m_engine.process(update.frame, cancelled);
        if (update.detection.stage == algorithm::DetectionStage::Cancelled) {
            error.clear();
            return LabStepStatus::Cancelled;
        }
        if (update.detection.diagnostics.exportFailed) {
            QString message = QStringLiteral(
                "Terminal observer export failed at file frame %1 (sequence %2, detection stage %3): %4. Exports are incomplete.")
                .arg(static_cast<qulonglong>(update.fileFrameIndex)).arg(update.detection.sequence)
                .arg(static_cast<int>(update.detection.stage))
                .arg(update.detection.diagnostics.diagnosticError.empty()
                    ? QStringLiteral("No diagnostic was supplied")
                    : QString::fromStdString(update.detection.diagnostics.diagnosticError));
            if (update.detection.stage == algorithm::DetectionStage::Error &&
                !update.detection.diagnostics.message.empty())
                message += QStringLiteral(" Detection error: ") +
                           QString::fromStdString(update.detection.diagnostics.message);
            if (m_output.isOpen()) {
                try { writeJsonLine(m_output, resultJson(update.detection, update.fileFrameIndex)); }
                catch (const std::exception& writeError) {
                    message += QStringLiteral(" Additionally, failed to record the result in JSONL: ") +
                               exceptionText(writeError);
                }
            }
            error = message;
            return LabStepStatus::Error;
        }
        auto snapshot = std::make_shared<algorithm::DisplaySnapshot>();
        snapshot->frame = update.frame;
        snapshot->detection = update.detection;
        update.snapshot = std::move(snapshot);
        if (!writeResult(update, error)) return LabStepStatus::Error;
        if (update.detection.stage == algorithm::DetectionStage::Error) {
            error = QString::fromStdString(update.detection.diagnostics.message);
            return LabStepStatus::Error;
        }
        m_performance.recordFrame(update.detection.diagnostics.processingTimeMs);
        ++m_processedFrames;
        update.processedFrames = m_processedFrames;
        update.totalFrames = m_plannedFrames;
        update.statistics = statistics();
        error.clear();
        return LabStepStatus::Frame;
    } catch (const std::exception& exception) {
        error = exceptionText(exception);
        return LabStepStatus::Error;
    }
}

bool LabSession::writeResult(const LabFrameUpdate& update, QString& error)
{
    try {
        const auto json = resultJson(update.detection, update.fileFrameIndex);
        if (m_output.isOpen()) writeJsonLine(m_output, json);
        if (m_comparison.isOpen()) {
            QJsonParseError parse;
            const auto expected = QJsonDocument::fromJson(m_reference.readLine(), &parse);
            if (parse.error != QJsonParseError::NoError || !expected.isObject())
                throw std::runtime_error(QStringLiteral(
                    "Invalid reference JSONL at file frame %1: expected a JSON object; reference may be malformed or exhausted (%2).")
                    .arg(static_cast<qulonglong>(update.fileFrameIndex)).arg(parse.errorString()).toStdString());
            writeJsonLine(m_comparison, compareResults(expected.object(), json, update.frame.binWidthHz));
        }
        if (m_csv.isOpen()) {
            QTextStream stream(&m_csv);
            stream.setRealNumberPrecision(16);
            std::unordered_map<std::int64_t, const algorithm::DetectionResult::TrackedDetection*> trackedById;
            trackedById.reserve(update.detection.trackedDetections.size());
            for (const auto& tracked : update.detection.trackedDetections)
                trackedById.emplace(tracked.raw.id, &tracked);
            for (const auto& signal : update.detection.detections) {
                stream << update.fileFrameIndex << ',' << update.frame.sequence << ',' << signal.id << ','
                       << signal.startFrequencyHz << ',' << signal.endFrequencyHz << ','
                       << signal.centerFrequencyHz << ',' << signal.bandwidthHz << ',' << signal.confidence << ','
                       << signal.signalLevelDbm << ',' << signal.noiseLevelDbm << ',' << signal.snrDb << ','
                       << static_cast<int>(signal.branch) << ',' << signal.firstSeenNs << ','
                       << signal.lastSeenNs << ',' << signal.occurrenceCount;
                const auto found = trackedById.find(signal.id);
                if (found != trackedById.end()) {
                    const auto& tracked = *found->second;
                    const auto& stable = tracked.stable;
                    stream << ',' << stable.startFrequencyHz << ',' << stable.endFrequencyHz << ','
                           << stable.centerFrequencyHz << ',' << stable.bandwidthHz << ','
                           << stable.signalLevelDbm << ',' << stable.noiseLevelDbm << ',' << stable.snrDb << ','
                           << static_cast<int>(tracked.boundaryState) << ',' << tracked.pendingCount << ','
                           << tracked.requiredCount << ',' << static_cast<int>(tracked.measurementBranch) << ','
                           << tracked.associationIou << ',' << tracked.centerDistanceHz << ','
                           << tracked.bandwidthRatio;
                } else {
                    for (int column = 0; column < 14; ++column) stream << ',';
                }
                stream << '\n';
            }
            stream.flush();
            if (stream.status() != QTextStream::Ok || !m_csv.flush())
                throw std::runtime_error("CSV write failed.");
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exceptionText(exception);
        return false;
    }
}

bool LabSession::seek(std::size_t frameIndex, QString& error)
{
    if (!m_open) {
        error = QStringLiteral("DetectionLab session is not open.");
        return false;
    }
    if (m_comparison.isOpen()) {
        error = QStringLiteral("Seek is unavailable while reference comparison is active.");
        return false;
    }
    std::string sourceError;
    if (!m_source.seekFrame(frameIndex, sourceError)) {
        error = QString::fromStdString(sourceError);
        return false;
    }
    m_engine.reset();
    m_performance.reset();
    error.clear();
    return true;
}

void LabSession::stop() noexcept
{
    m_source.stop();
    m_engine.setObserver(nullptr);
    m_open = false;
}

LabRunStatistics LabSession::statistics() const noexcept
{
    LabRunStatistics result;
    result.processedFrames = m_processedFrames;
    if (m_runStarted != std::chrono::steady_clock::time_point{}) {
        result.elapsedSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_runStarted).count();
    }
    result.throughputHz = m_performance.throughputHz();
    result.averageMs = m_performance.averageMs();
    result.p50Ms = m_performance.percentile(0.5);
    result.p95Ms = m_performance.percentile(0.95);
    return result;
}

} // namespace scn::lab
