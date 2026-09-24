#include "LabSession.h"
#include "../../common/Frequency.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

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

void resolveModelPath(scn::algorithm::DetectionConfig& config)
{
    if (config.detector.modelPath.empty()) return;
    const auto modelPath = QString::fromStdString(config.detector.modelPath);
    if (QDir::isRelativePath(modelPath)) {
        config.detector.modelPath = QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(modelPath).toStdString();
    }
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
        for (const auto* name : {"file", "model", "config", "compare", "write-config", "output", "csv", "dump"}) {
            if (parser.isSet(name) && parser.value(name).isEmpty())
                throw std::runtime_error(std::string("Empty path for --") + name);
        }

        auto config = scn::lab::readConfiguration(parser.value("config"));
        if (parser.isSet("model")) config.detector.modelPath = parser.value("model").toStdString();
        resolveModelPath(config);
        std::string configError;
        if (!scn::algorithm::validateConfig(config, configError))
            throw std::runtime_error(configError);

        const QStringList protectedPaths{parser.value("file"), parser.value("model"),
            parser.value("config"), parser.value("compare"),
            QString::fromStdString(config.detector.modelPath)};
        if (parser.isSet("write-config")) {
            if (parser.isSet("output") || parser.isSet("csv") || parser.isSet("dump") ||
                parser.isSet("inspect-model") || parser.isSet("step")) {
                throw std::runtime_error(
                    "--write-config is export-only; do not combine it with frame exports, --inspect-model or --step.");
            }
            scn::lab::validateExportPaths(protectedPaths, {parser.value("write-config")});
            scn::lab::writeJsonFile(parser.value("write-config"), scn::lab::configurationJson(config));
            return 0;
        }
        if (parser.isSet("inspect-model")) {
            if (parser.isSet("output") || parser.isSet("csv") || parser.isSet("dump") ||
                parser.isSet("compare") || parser.isSet("step")) {
                throw std::runtime_error("--inspect-model does not export frames; remove frame export/step options.");
            }
            config.enabled = true;
            QString modelInfo, error;
            if (!scn::lab::LabSession::inspectModel(config, modelInfo, error))
                throw std::runtime_error(error.toStdString());
            std::cout << modelInfo.toStdString() << '\n';
            return 0;
        }
        if (!parser.isSet("file"))
            throw std::runtime_error("Specify --file or --inspect-model. Use --help for options.");
        if (parser.isSet("compare") && (!parser.isSet("output") || parser.isSet("step")))
            throw std::runtime_error("--compare requires --output and sequential (not --step) replay.");

        scn::lab::LabRunOptions options;
        options.config = config;
        options.inputFile = parser.value("file");
        options.configFile = parser.value("config");
        options.referenceFile = parser.value("compare");
        options.outputFile = parser.value("output");
        options.csvFile = parser.value("csv");
        options.dumpDirectory = parser.value("dump");
        options.startFrame = unsignedValue(parser.value("start-frame"), "start-frame");
        options.maximumFrames = unsignedValue(parser.value("frames"), "frames");
        options.logicalFrameRateHz = static_cast<std::uint32_t>(unsignedValue(parser.value("fps"), "fps"));
        options.fallbackPointCount = unsignedValue(parser.value("points"), "points");
        QByteArray centerText = parser.value("center-hz").toUtf8();
        QByteArray spanText = parser.value("span-hz").toUtf8();
        if (!scn::common::parseFrequencyHz(
                std::string_view(centerText.constData(), static_cast<std::size_t>(centerText.size())),
                options.fallbackCenterFrequencyHz))
            throw std::runtime_error("Invalid center-hz.");
        if (!scn::common::parseFrequencyHz(
                std::string_view(spanText.constData(), static_cast<std::size_t>(spanText.size())),
                options.fallbackSpanHz) || options.fallbackSpanHz <= 0)
            throw std::runtime_error("Invalid span-hz.");
        bool ok = false;
        options.fallbackReferenceLevelDbm = parser.value("reference-dbm").toDouble(&ok);
        if (!ok || !std::isfinite(options.fallbackReferenceLevelDbm))
            throw std::runtime_error("Invalid reference-dbm.");
        options.interactiveStep = parser.isSet("step");

        scn::lab::LabSession session;
        QString error;
        if (!session.open(options, error)) throw std::runtime_error(error.toStdString());
        std::cout << session.modelInfo().toStdString() << '\n';
        if (!session.stageDirectory().isEmpty())
            std::cout << "stage_directory=" << session.stageDirectory().toStdString() << '\n';

        while (true) {
            if (parser.isSet("step")) {
                std::cout << "frame " << session.position() << " [Enter/seek N/q]> " << std::flush;
                std::string command;
                if (!std::getline(std::cin, command) || command == "q") break;
                if (command.rfind("seek ", 0) == 0) {
                    const auto index = unsignedValue(QString::fromStdString(command.substr(5)), "seek");
                    if (!session.seek(index, error)) {
                        std::cerr << error.toStdString() << '\n';
                        continue;
                    }
                    continue;
                }
                if (!command.empty()) {
                    std::cerr << "Use Enter, seek N, or q.\n";
                    continue;
                }
            }
            scn::lab::LabFrameUpdate update;
            const auto status = session.processNext(update, error);
            if (status == scn::lab::LabStepStatus::End) break;
            if (status == scn::lab::LabStepStatus::Cancelled) continue;
            if (status == scn::lab::LabStepStatus::Error)
                throw std::runtime_error(error.toStdString());

            const auto& result = update.detection;
            std::cout << "frame=" << update.fileFrameIndex << " bins=" << update.frame.powerDb.size()
                      << " accumulation=" << result.accumulatedFrames << '/' << result.requiredFrames
                      << " detections=" << result.detections.size()
                      << " windows=" << result.diagnostics.windowCount
                      << " ms=" << result.diagnostics.processingTimeMs << '\n';
        }
        const auto stats = session.statistics();
        std::cout << "processed=" << stats.processedFrames << " dropped=0 throughput_fps="
                  << stats.throughputHz << " mean_ms=" << stats.averageMs
                  << " p50_ms=" << stats.p50Ms << " p95_ms=" << stats.p95Ms
                  << " (percentiles: latest <=2048 frames)\n";
        session.stop();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DetectionLab: " << error.what() << '\n';
        return 1;
    }
}
