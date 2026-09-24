#include "../tools/DetectionLab/LabSession.h"
#include "../algorithm/detector/IScnBackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " CHECK " #expression); } while (false)

namespace
{
class FakeBackend final : public scn::algorithm::IScnBackend
{
public:
    bool initialize(const scn::algorithm::DetectorConfig&, std::string& error) override
    {
        error.clear();
        return true;
    }
    bool infer(const std::vector<float>& input, scn::algorithm::ScnModelOutput& output, std::string& error) override
    {
        CHECK(input.size() == 32768);
        ++inferenceCount;
        output.heatmap.assign(8192, 0.0F);
        output.bandwidth.assign(8192, 0.0F);
        output.offset.assign(8192, 0.0F);
        error.clear();
        return true;
    }
    std::string modelInfo() const override { return "CPU test backend"; }
    unsigned inferenceCount = 0;
};

QString createInputFile(const QString& directory, const QString& name = QStringLiteral("spectrum.dat"))
{
    const auto path = QDir(directory).filePath(name);
    std::ofstream output(path.toStdWString(), std::ios::binary);
    if (!output) throw std::runtime_error("Unable to create test spectrum file.");
    const float frames[3][8] = {
        {-50, -49, -48, -47, -46, -45, -44, -43},
        {-40, -39, -38, -37, -36, -35, -34, -33},
        {-30, -29, -28, -27, -26, -25, -24, -23}};
    output.write(reinterpret_cast<const char*>(frames), sizeof(frames));
    if (!output) throw std::runtime_error("Unable to write test spectrum file.");
    return path;
}

scn::lab::LabRunOptions baseOptions(const QString& input)
{
    scn::lab::LabRunOptions options;
    options.inputFile = input;
    options.config.enabled = false;
    options.config.detector.modelPath.clear();
    options.fallbackPointCount = 8;
    options.fallbackCenterFrequencyHz = 1000;
    options.fallbackSpanHz = 80;
    options.fallbackResolutionBandwidthHz = 1;
    return options;
}

void testConfigurationRoundTrip(const QString& directory)
{
    using namespace scn::lab;
    auto config = scn::algorithm::DetectionConfig{};
    config.enabled = false;
    config.detector.modelPath = "cpu-only-model-placeholder";
    config.accumulator.frames = 7;
    config.tracker.smoothingAlpha = 0.61;
    config.channelAggregation.priors.push_back({3, "test prior", true, 100, 300});
    const auto path = QDir(directory).filePath(QStringLiteral("config.json"));
    writeJsonFile(path, configurationJson(config));
    const auto loaded = readConfiguration(path);
    CHECK(!loaded.enabled);
    CHECK(loaded.detector.modelPath == config.detector.modelPath);
    CHECK(loaded.accumulator.frames == 7);
    CHECK(loaded.tracker.smoothingAlpha == 0.61);
    CHECK(loaded.channelAggregation.priors.size() == 1);
    CHECK(loaded.channelAggregation.priors.front().name == "test prior");

    const auto legacy = QDir(directory).filePath(QStringLiteral("legacy.json"));
    QFile legacyFile(legacy);
    CHECK(legacyFile.open(QIODevice::WriteOnly));
    const QByteArray legacyJson("{\"enabled\":false}");
    CHECK(legacyFile.write(legacyJson) == legacyJson.size());
    legacyFile.close();
    const auto legacyConfig = readConfiguration(legacy);
    CHECK(!legacyConfig.enabled);
    CHECK(legacyConfig.accumulator.frames == 16);
}

void testStepAndSeek(const QString& input)
{
    scn::lab::LabSession session;
    auto options = baseOptions(input);
    options.maximumFrames = 2;
    QString error;
    CHECK(session.open(options, error));
    CHECK(session.totalFrames() == 2);
    scn::lab::LabFrameUpdate update;
    CHECK(session.processNext(update, error) == scn::lab::LabStepStatus::Frame);
    CHECK(update.fileFrameIndex == 0);
    CHECK(update.detection.stage == scn::algorithm::DetectionStage::Bypassed);
    CHECK(update.snapshot);
    CHECK(update.processedFrames == 1);

    CHECK(session.seek(2, error));
    CHECK(session.position() == 2);
    CHECK(session.processedFrames() == 1);
    CHECK(session.totalFrames() == 2);
    CHECK(session.processNext(update, error) == scn::lab::LabStepStatus::Frame);
    CHECK(update.fileFrameIndex == 2);
    CHECK(update.processedFrames == 2);
    CHECK(session.processNext(update, error) == scn::lab::LabStepStatus::End);
    CHECK(session.statistics().processedFrames == 2);
}

void testSeekResetsDetectionHistory(const QString& input)
{
    scn::lab::LabSession session(std::make_unique<FakeBackend>());
    auto options = baseOptions(input);
    options.config.enabled = true;
    options.config.detector.modelPath = "test-only.engine";
    options.config.accumulator.frames = 2;
    QString error;
    CHECK(session.open(options, error));

    scn::lab::LabFrameUpdate beforeSeek;
    CHECK(session.processNext(beforeSeek, error) == scn::lab::LabStepStatus::Frame);
    CHECK(beforeSeek.detection.accumulatedFrames == 1);
    CHECK(beforeSeek.detection.detections.empty());

    CHECK(session.seek(2, error));
    scn::lab::LabFrameUpdate afterSeek;
    CHECK(session.processNext(afterSeek, error) == scn::lab::LabStepStatus::Frame);
    CHECK(afterSeek.fileFrameIndex == 2);
    CHECK(afterSeek.detection.accumulatedFrames == 1);
    CHECK(afterSeek.detection.generation > beforeSeek.detection.generation);
    CHECK(afterSeek.detection.trackingSegment > beforeSeek.detection.trackingSegment);
}

void testCancellation(const QString& input)
{
    auto backend = std::make_unique<FakeBackend>();
    auto* backendView = backend.get();
    scn::lab::LabSession session(std::move(backend));
    auto options = baseOptions(input);
    options.config.enabled = true;
    options.config.detector.modelPath = "test-only.engine";
    QString error;
    CHECK(session.open(options, error));
    scn::lab::LabFrameUpdate update;
    CHECK(session.processNext(update, error, [] { return true; }) == scn::lab::LabStepStatus::Cancelled);
    CHECK(backendView->inferenceCount == 0);
    CHECK(session.processedFrames() == 0);
}

void testExportPathConflict(const QString& input)
{
    scn::lab::LabSession session;
    auto options = baseOptions(input);
    options.outputFile = input;
    QString error;
    CHECK(!session.open(options, error));
    CHECK(error.contains(QStringLiteral("Output conflicts with protected input")));
    CHECK(QFileInfo(input).exists());
}
}

namespace scn::algorithm
{
std::unique_ptr<IScnBackend> createTensorRtScnBackend()
{
    return std::make_unique<FakeBackend>();
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        CHECK(temporary.isValid());
        const auto input = createInputFile(temporary.path());
        testConfigurationRoundTrip(temporary.path());
        testStepAndSeek(input);
        testSeekResetsDetectionHistory(input);
        testCancellation(input);
        testExportPathConflict(input);
        std::cout << "DetectionLab CPU session tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
