#include "LabIO.h"
#include "common/Frequency.h"
#include "../../source/FileSource/FileSource.h"
#include "../../runtime/PerformanceMonitor.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSysInfo>
#include <QTextStream>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace
{
std::size_t unsignedValue(const QString& text, const char* name)
{
    bool ok = false;
    const auto value = text.toULongLong(&ok);
    if (!ok || value > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error(std::string("Invalid unsigned option: ") + name);
    return static_cast<std::size_t>(value);
}
void jsonLine(QFile& file, const QJsonObject& object)
{
    auto data = QJsonDocument(object).toJson(QJsonDocument::Compact);
    data += '\n';
    if (file.write(data) != data.size() || !file.flush()) throw std::runtime_error("Output write failed.");
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("DetectionLab");
    QCommandLineParser parser;
    parser.setApplicationDescription("ISA TensorRT SCN offline detection (no UI, whitelist or alarms).");
    parser.addHelpOption();
    parser.addOptions({
        {"inspect-model", "Load and validate engine metadata, without executing inference."},
        {"file", "Input ISA DAT/BIN or TXT/CSV/ASC spectrum file.", "path"},
        {"model", "TensorRT engine path; relative to executable directory.", "path"},
        {"config", "Detection config JSON; see --write-config.", "path"},
        {"write-config", "Create default/current config JSON and exit (no GPU access; refuses existing paths).", "path"},
        {"start-frame", "First zero-based file frame; starts with empty temporal history.", "index", "0"},
        {"frames", "Maximum number of frames (0 = all remaining).", "count", "0"},
        {"fps", "Logical replay frame rate; not a processing throttle.", "hz", "30"},
        {"points", "Frame length for files without SpectrumLen metadata.", "count", "202242"},
        {"center-hz", "Fallback center frequency in Hz.", "hz", "2025000000"},
        {"span-hz", "Fallback spectrum bandwidth in Hz.", "hz", "3950000000"},
        {"reference-dbm", "Fallback reference level.", "dbm", "-20"},
        {"output", "New streaming JSONL path; also creates <path>.manifest.json; never overwrites.", "path"},
        {"csv", "New signal CSV path; also creates <path>.manifest.json; never overwrites.", "path"},
        {"dump", "Create a unique run-UUID subdirectory with stages and manifest.json under this directory.", "directory"},
        {"compare", "Reference JSONL (same frame indices); requires --output, writes .comparison.jsonl.", "path"},
        {"step", "Interactive: Enter = next frame; seek N = reset to frame N; q = quit."}
    });
    parser.process(app);
    try {
        for (const auto* name : {"file", "model", "config", "compare", "write-config", "output", "csv", "dump"})
            if (parser.isSet(name) && parser.value(name).isEmpty())
                throw std::runtime_error(std::string("Empty path for --") + name);
        auto config = scn::lab::readConfiguration(parser.value("config"));
        if (parser.isSet("inspect-model")) config.enabled = true;
        if (parser.isSet("model")) config.detector.modelPath = parser.value("model").toStdString();
        if (!config.detector.modelPath.empty() && QDir::isRelativePath(QString::fromStdString(config.detector.modelPath)))
            config.detector.modelPath = QDir(app.applicationDirPath())
                .absoluteFilePath(QString::fromStdString(config.detector.modelPath)).toStdString();
        std::string error;
        if (!scn::algorithm::validateConfig(config, error)) throw std::runtime_error(error);
        const QStringList protectedPaths{parser.value("file"), parser.value("model"), parser.value("config"), parser.value("compare"),
                                          QString::fromStdString(config.detector.modelPath)};
        if (parser.isSet("write-config")) {
            if (parser.isSet("output") || parser.isSet("csv") || parser.isSet("dump") ||
                parser.isSet("inspect-model") || parser.isSet("step"))
                throw std::runtime_error("--write-config is export-only; do not combine it with frame exports, --inspect-model or --step.");
            // Even export-only invocations protect every supplied input and
            // the effective (executable-relative) model before creating a file.
            scn::lab::validateExportPaths(protectedPaths, {parser.value("write-config")});
            scn::lab::writeJsonFile(parser.value("write-config"), scn::lab::configurationJson(config));
            return 0;
        }
        if (!parser.isSet("inspect-model") && !parser.isSet("file"))
            throw std::runtime_error("Specify --file or --inspect-model. Use --help for options.");
        if (parser.isSet("compare") && (!parser.isSet("output") || parser.isSet("step")))
            throw std::runtime_error("--compare requires --output and sequential (not --step) replay.");
        if (parser.isSet("inspect-model") && (parser.isSet("output") || parser.isSet("csv") ||
            parser.isSet("dump") || parser.isSet("compare") || parser.isSet("step")))
            throw std::runtime_error("--inspect-model does not export frames; remove frame export/step options.");
        const auto runId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto outputPath = parser.isSet("output") ? scn::lab::resolvedPath(parser.value("output")) : QString();
        const auto csvPath = parser.isSet("csv") ? scn::lab::resolvedPath(parser.value("csv")) : QString();
        const auto comparisonPath = parser.isSet("compare") ? outputPath + ".comparison.jsonl" : QString();
        const auto stageDirectory = parser.isSet("dump")
            ? QDir(scn::lab::resolvedPath(parser.value("dump"))).filePath("run-" + runId) : QString();
        QStringList outputPaths, manifestPaths;
        for (const auto& path : {outputPath, csvPath}) {
            if (path.isEmpty()) continue;
            outputPaths.push_back(path);
            manifestPaths.push_back(path + ".manifest.json");
        }
        if (!comparisonPath.isEmpty()) outputPaths.push_back(comparisonPath);
        outputPaths.append(manifestPaths);
        scn::lab::validateExportPaths(protectedPaths, outputPaths, stageDirectory);
        scn::algorithm::DetectionEngine engine;
        if (!engine.initialize(config)) throw std::runtime_error(engine.lastError() + "\n" + engine.modelInfo());
        std::cout << engine.modelInfo() << '\n';
        if (parser.isSet("inspect-model")) return 0;

        scn::source::SourceConfig sourceConfig;
        sourceConfig.kind = scn::algorithm::SourceKind::File;
        sourceConfig.filePath = parser.value("file").toStdString();
        sourceConfig.loopFile = false;
        const auto fps = unsignedValue(parser.value("fps"), "fps");
        if (!fps || fps > 1000000) throw std::runtime_error("fps must be in [1,1000000].");
        sourceConfig.frameRateHz = static_cast<int>(fps);
        sourceConfig.pointCount = unsignedValue(parser.value("points"), "points");
        bool ok = false;
        const QByteArray centerText = parser.value("center-hz").toUtf8();
        const QByteArray spanText = parser.value("span-hz").toUtf8();
        if (!scn::common::parseFrequencyHz(
                std::string_view(centerText.constData(), static_cast<std::size_t>(centerText.size())),
                sourceConfig.centerFrequencyHz))
            throw std::runtime_error("Invalid center-hz.");
        if (!scn::common::parseFrequencyHz(
                std::string_view(spanText.constData(), static_cast<std::size_t>(spanText.size())),
                sourceConfig.bandwidthHz) || sourceConfig.bandwidthHz <= 0)
            throw std::runtime_error("Invalid span-hz.");
        sourceConfig.referenceLevelDbm = parser.value("reference-dbm").toDouble(&ok);
        if (!ok || !std::isfinite(sourceConfig.referenceLevelDbm)) throw std::runtime_error("Invalid reference-dbm.");
        const auto start = unsignedValue(parser.value("start-frame"), "start-frame");
        const auto limit = unsignedValue(parser.value("frames"), "frames");
        scn::source::FileSource source;
        if (!source.open(sourceConfig, error) || !source.seekFrame(start, error) || !source.start())
            throw std::runtime_error(error);
        engine.reset();
        // Match FileSource's metadata precedence without changing the supplied
        // input filename: symlink names may themselves carry ISA metadata.
        scn::source::FileSourceMetadata metadata;
        if (!scn::source::FileSource::inspectFile(sourceConfig.filePath, metadata, error))
            throw std::runtime_error(error);
        const auto points = metadata.hasSpectrumLength ? metadata.spectrumLength : sourceConfig.pointCount;
        const double center = static_cast<double>(metadata.hasCenterFrequency
            ? metadata.centerFrequencyHz : sourceConfig.centerFrequencyHz);
        const double span = static_cast<double>(metadata.hasBandwidth
            ? metadata.bandwidthHz : sourceConfig.bandwidthHz);
        const double rbw = static_cast<double>(metadata.hasResolutionBandwidth
            ? metadata.resolutionBandwidthHz : sourceConfig.resolutionBandwidthHz);
        const auto level = metadata.hasReferenceLevel ? metadata.referenceLevelDbm : sourceConfig.referenceLevelDbm;
        if (!points || !std::isfinite(center) || !std::isfinite(span) || span <= 0.0 ||
            !std::isfinite(center - span / 2.0) || !std::isfinite(center + span / 2.0) ||
            !std::isfinite(rbw) || !std::isfinite(level))
            throw std::runtime_error("Input filename/fallback metadata do not describe a finite spectrum frame.");
        const bool exporting = !outputPaths.isEmpty() || !stageDirectory.isEmpty();
        QJsonObject manifest;
        if (exporting) {
            const auto model = scn::lab::modelProvenance(config, QString::fromStdString(engine.modelInfo()));
            const auto available = source.frameCount() - start;
            const auto planned = limit ? std::min(limit, available) : available;
            const QFileInfo inputInfo(parser.value("file"));
            manifest = {{"schemaVersion", 1}, {"runId", runId},
                {"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"inputFile", inputInfo.absoluteFilePath()},
                {"inputCanonicalPath", scn::lab::resolvedPath(parser.value("file"))},
                {"inputSizeBytes", QString::number(metadata.fileSizeBytes)},
                {"inputModifiedUtc", inputInfo.lastModified().toUTC().toString(Qt::ISODateWithMs)},
                {"configFile", parser.isSet("config") ? scn::lab::resolvedPath(parser.value("config")) : QString()},
                {"referenceFile", parser.isSet("compare") ? scn::lab::resolvedPath(parser.value("compare")) : QString()},
                {"logicalFps", static_cast<int>(fps)}, {"startFrame", static_cast<qint64>(start)},
                {"requestedMaxFrames", QString::number(static_cast<qulonglong>(limit))},
                {"inputFrameCount", QString::number(static_cast<qulonglong>(source.frameCount()))},
                {"plannedSequentialFrames", QString::number(static_cast<qulonglong>(planned))},
                {"interactiveSeekEnabled", parser.isSet("step")},
                {"timestampBasis", "zero-based fileFrameIndex / logicalFps; not wall clock"},
                {"manifestScope", "Configuration and input geometry, not a completion record; failures may leave partial exports."},
                {"modelInfo", QString::fromStdString(engine.modelInfo())}, {"modelSha256", model.value("sha256")},
                {"model", model}, {"config", scn::lab::configurationJson(config)},
                {"frameInfo", QJsonObject{{"pointCount", static_cast<qint64>(points)},
                    {"startHz", center - span / 2.0}, {"endHz", center + span / 2.0},
                    {"binHz", span / static_cast<double>(points)}, {"referenceLevelDbm", level},
                    {"resolutionBandwidthHz", rbw}, {"sourceName", "FILE"},
                    {"format", metadata.isBinary ? "native_float32" : "text"},
                    {"binaryTrailingBytes", metadata.isBinary
                        ? QString::number(metadata.fileSizeBytes % (static_cast<std::uint64_t>(points) * sizeof(float))) : QString()}}},
                {"exports", QJsonObject{{"jsonl", outputPath}, {"csv", csvPath},
                    {"comparison", comparisonPath}, {"stageDirectory", stageDirectory}}},
                {"stageFloatFormat", QSysInfo::ByteOrder == QSysInfo::LittleEndian ? "float32_le" : "float32_be"}};
        }
        // Initialization may take time; recheck the entire plan immediately
        // before creation. Every individual writer still uses atomic NewOnly.
        scn::lab::validateExportPaths(protectedPaths, outputPaths, stageDirectory);
        std::unique_ptr<scn::lab::StageExporter> exporter;
        if (!stageDirectory.isEmpty()) {
            exporter = std::make_unique<scn::lab::StageExporter>(stageDirectory);
            manifestPaths.push_back(QDir(stageDirectory).filePath("manifest.json"));
            engine.setObserver(exporter.get());
            std::cout << "stage_directory=" << stageDirectory.toStdString() << '\n';
        }
        manifest.insert("manifests", QJsonArray::fromStringList(manifestPaths));
        for (const auto& path : manifestPaths) scn::lab::writeJsonFile(path, manifest);
        QFile output, csv, comparison, reference;
        if (!outputPath.isEmpty()) scn::lab::openNewOutput(output, outputPath);
        if (!csvPath.isEmpty()) {
            scn::lab::openNewOutput(csv, csvPath);
            const QByteArray header("fileFrameIndex,sequence,id,startHz,endHz,centerHz,bandwidthHz,confidence,signalDbm,noiseDbm,cnrDb,branch,firstSeenNs,lastSeenNs,occurrences,stableStartHz,stableEndHz,stableCenterHz,stableBandwidthHz,stableSignalDbm,stableNoiseDbm,stableCnrDb,boundaryState,pendingCount,requiredCount,measurementBranch,associationIoU,centerDistanceHz,bandwidthRatio\n");
            if (csv.write(header) != header.size() || !csv.flush()) throw std::runtime_error("CSV header write failed.");
        }
        if (parser.isSet("compare")) {
            reference.setFileName(parser.value("compare"));
            if (!reference.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open reference JSONL.");
            scn::lab::openNewOutput(comparison, comparisonPath);
        }
        scn::runtime::PerformanceMonitor performance;
        const auto runBegin = std::chrono::steady_clock::now();
        std::size_t processed = 0;
        while (source.position() < source.frameCount() && (!limit || processed < limit)) {
            if (parser.isSet("step")) {
                std::cout << "frame " << source.position() << " [Enter/seek N/q]> " << std::flush;
                std::string command;
                if (!std::getline(std::cin, command) || command == "q") break;
                if (command.rfind("seek ", 0) == 0) {
                    const auto index = unsignedValue(QString::fromStdString(command.substr(5)), "seek");
                    if (!source.seekFrame(index, error)) { std::cerr << error << '\n'; continue; }
                    engine.reset(); performance.reset();
                    continue;
                }
                if (!command.empty()) { std::cerr << "Use Enter, seek N, or q.\n"; continue; }
            }
            const auto fileIndex = source.position();
            scn::algorithm::SpectrumFrame frame;
            if (!source.read(frame)) throw std::runtime_error("Unexpected file read failure before complete frame count.");
            if (exporter) exporter->beginFrame(frame, fileIndex);
            const auto result = engine.process(frame);
            if (result.diagnostics.exportFailed) {
                // The engine keeps its detection stage/results when the final
                // observer fails. The CLI must still fail the export operation,
                // before CSV/comparison output or any success/progress summary.
                auto message = QStringLiteral(
                    "Terminal observer export failed at file frame %1 (sequence %2, detection stage %3): %4. Exports are incomplete.")
                    .arg(static_cast<qulonglong>(fileIndex)).arg(result.sequence)
                    .arg(static_cast<int>(result.stage))
                    .arg(result.diagnostics.diagnosticError.empty()
                        ? QStringLiteral("No export diagnostic was supplied")
                        : QString::fromStdString(result.diagnostics.diagnosticError)).toStdString();
                if (result.stage == scn::algorithm::DetectionStage::Error && !result.diagnostics.message.empty())
                    message += " Detection error: " + result.diagnostics.message;
                if (output.isOpen()) {
                    try {
                        // Preserve the returned successful detections and the
                        // export-failure flags in JSONL when it is still writable.
                        jsonLine(output, scn::lab::resultJson(result, fileIndex));
                    } catch (const std::exception& writeError) {
                        // A second I/O failure must not hide the observer error.
                        message += " Additionally, failed to record the result in JSONL: ";
                        message += writeError.what();
                    }
                }
                throw std::runtime_error(message);
            }
            const auto json = scn::lab::resultJson(result, fileIndex);
            if (output.isOpen()) jsonLine(output, json);
            if (comparison.isOpen()) {
                QJsonParseError parse;
                const auto expected = QJsonDocument::fromJson(reference.readLine(), &parse);
                if (parse.error != QJsonParseError::NoError || !expected.isObject())
                    throw std::runtime_error(QStringLiteral(
                        "Invalid reference JSONL at file frame %1: expected a JSON object; reference may be malformed or exhausted (%2).")
                        .arg(static_cast<qulonglong>(fileIndex)).arg(parse.errorString()).toStdString());
                // Validate both full records before emitting any comparison.
                // Valid differences/unmatched signals are reported, not failed.
                jsonLine(comparison, scn::lab::compareResults(expected.object(), json, frame.binWidthHz));
            }
            if (csv.isOpen()) {
                QTextStream stream(&csv);
                stream.setRealNumberPrecision(16);
                std::unordered_map<std::int64_t, const scn::algorithm::DetectionResult::TrackedDetection*> trackedById;
                trackedById.reserve(result.trackedDetections.size());
                for (const auto& tracked : result.trackedDetections)
                    trackedById.emplace(tracked.raw.id, &tracked);
                for (const auto& s : result.detections)
                {
                    stream << fileIndex << ',' << frame.sequence << ',' << s.id << ',' << s.startFrequencyHz << ','
                           << s.endFrequencyHz << ',' << s.centerFrequencyHz << ',' << s.bandwidthHz << ','
                           << s.confidence << ',' << s.signalLevelDbm << ',' << s.noiseLevelDbm << ',' << s.snrDb << ','
                           << static_cast<int>(s.branch) << ',' << s.firstSeenNs << ',' << s.lastSeenNs << ',' << s.occurrenceCount;
                    const auto found = trackedById.find(s.id);
                    if (found != trackedById.end()) {
                        const auto& item = *found->second;
                        const auto& stable = item.stable;
                        stream << ',' << stable.startFrequencyHz << ',' << stable.endFrequencyHz << ','
                               << stable.centerFrequencyHz << ',' << stable.bandwidthHz << ','
                               << stable.signalLevelDbm << ',' << stable.noiseLevelDbm << ',' << stable.snrDb << ','
                               << static_cast<int>(item.boundaryState) << ',' << item.pendingCount << ','
                               << item.requiredCount << ',' << static_cast<int>(item.measurementBranch) << ','
                               << item.associationIou << ',' << item.centerDistanceHz << ',' << item.bandwidthRatio;
                    } else {
                        // Fourteen stable-tracking columns follow the raw fields.
                        for (int column = 0; column < 14; ++column) stream << ',';
                    }
                    stream << '\n';
                }
                stream.flush();
                if (stream.status() != QTextStream::Ok || !csv.flush()) throw std::runtime_error("CSV write failed.");
            }
            if (result.stage == scn::algorithm::DetectionStage::Error) throw std::runtime_error(result.diagnostics.message);
            performance.recordFrame(result.diagnostics.processingTimeMs);
            ++processed;
            std::cout << "frame=" << fileIndex << " bins=" << frame.powerDb.size()
                      << " accumulation=" << result.accumulatedFrames << '/' << result.requiredFrames
                      << " detections=" << result.detections.size()
                      << " windows=" << result.diagnostics.windowCount
                      << " ms=" << result.diagnostics.processingTimeMs << '\n';
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - runBegin).count();
        std::cout << "processed=" << processed << " dropped=0 throughput_fps=" << (elapsed > 0 ? processed / elapsed : 0)
                  << " mean_ms=" << performance.averageMs() << " p50_ms=" << performance.percentile(.5)
                  << " p95_ms=" << performance.percentile(.95) << " (percentiles: latest <=2048 frames)\n";
        source.stop();
        engine.setObserver(nullptr);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "DetectionLab: " << e.what() << '\n';
        return 1;
    }
}
