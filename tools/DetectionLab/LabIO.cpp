#include "LabIO.h"
#include "common/Frequency.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace scn::lab
{
namespace
{
namespace fs = std::filesystem;

fs::path nativePath(const QString& path)
{
#ifdef _WIN32
    return fs::path(path.toStdWString());
#else
    return fs::path(QFile::encodeName(path).toStdString());
#endif
}

QString pathText(const fs::path& path)
{
#ifdef _WIN32
    auto text = QDir::fromNativeSeparators(QString::fromStdWString(path.native()));
    // Some filesystem implementations return a normal canonical drive/UNC
    // path with a Win32 extended prefix; keep our normalized form idempotent.
    if (text.startsWith(QStringLiteral("//?/UNC/"), Qt::CaseInsensitive)) return "//" + text.mid(8);
    if (text.startsWith(QStringLiteral("//?/")) && text.size() > 6 &&
        text.at(5) == QLatin1Char(':') && text.at(6) == QLatin1Char('/')) return text.mid(4);
    return text;
#else
    return QFile::decodeName(path.native().c_str());
#endif
}

[[noreturn]] void pathError(const QString& reason, const QString& path)
{
    throw std::runtime_error(QStringLiteral("%1: %2").arg(reason, path).toStdString());
}

fs::file_status pathStatus(const fs::path& path, bool followLinks)
{
    std::error_code error;
    const auto status = followLinks ? fs::status(path, error) : fs::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory)
        pathError(QStringLiteral("Cannot inspect path (%1)").arg(QString::fromStdString(error.message())), pathText(path));
    return status;
}

bool sameSpelling(const fs::path& a, const fs::path& b)
{
#ifdef _WIN32
    return pathText(a).compare(pathText(b), Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

bool sameFile(const fs::path& a, const fs::path& b)
{
    if (fs::exists(pathStatus(a, true)) && fs::exists(pathStatus(b, true))) {
        std::error_code error;
        const bool same = fs::equivalent(a, b, error);
        if (error) pathError(QStringLiteral("Cannot compare file identity"), pathText(a) + " <-> " + pathText(b));
        if (same) return true;
    }
    return sameSpelling(a, b);
}

// Component comparison, not a string prefix (e.g. out and outside differ).
bool containsPath(const fs::path& directory, const fs::path& path)
{
    auto child = path.begin();
    for (auto parent = directory.begin(); parent != directory.end(); ++parent, ++child)
        if (child == path.end() || !sameSpelling(*parent, *child)) return false;
    return true;
}

void requireNewPath(const fs::path& path)
{
    // A dangling symlink is an existing directory entry too: never replace it.
    if (fs::exists(pathStatus(path, false)))
        pathError(QStringLiteral("Refusing to overwrite an existing output; choose a new path"), pathText(path));
}

void writeBytes(const QString& name, const char* bytes, qint64 size)
{
    QFile file;
    openNewOutput(file, name);
    if (file.write(bytes, size) != size || !file.flush())
        throw std::runtime_error((QStringLiteral("Cannot write stage export: ") + name).toStdString());
}
void writeJson(const QString& name, const QJsonObject& value)
{
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
    writeBytes(name, bytes.constData(), bytes.size());
}
void writeFloats(const QString& name, const std::vector<float>& values)
{
    writeBytes(name, reinterpret_cast<const char*>(values.data()), static_cast<qint64>(values.size() * sizeof(float)));
}
QJsonObject signalJson(const algorithm::DetectedSignal& s)
{
    return {{"id", static_cast<qint64>(s.id)}, {"startHz", s.startFrequencyHz}, {"endHz", s.endFrequencyHz},
        {"centerHz", s.centerFrequencyHz}, {"bandwidthHz", s.bandwidthHz}, {"confidence", s.confidence},
        {"signalDbm", s.signalLevelDbm}, {"noiseDbm", s.noiseLevelDbm}, {"cnrDb", s.snrDb},
        {"branch", static_cast<int>(s.branch)},
        {"measurementTimestampNs", QString::number(s.measurementTimestampNs)},
        {"firstSeenNs", QString::number(s.firstSeenNs)},
        {"lastSeenNs", QString::number(s.lastSeenNs)}, {"occurrences", QString::number(s.occurrenceCount)}};
}
QJsonArray signalsJson(const std::vector<algorithm::DetectedSignal>& values)
{
    QJsonArray array;
    for (const auto& value : values) array.append(signalJson(value));
    return array;
}
double number(const QJsonObject& object, const char* key, double fallback)
{
    if (!object.contains(key)) return fallback;
    const auto value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error(std::string("Expected finite numeric config field: ") + key);
    return value.toDouble();
}
bool integerHz(const QJsonObject& object, const char* key, std::int64_t fallback,
               std::int64_t& output)
{
    const auto value = object.value(QLatin1String(key));
    if (!object.contains(key)) {
        output = fallback;
        return true;
    }
    if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;

    // Preserve exact integer JSON values and round legacy fractional-Hz values.
    constexpr qint64 invalidInteger = std::numeric_limits<qint64>::min();
    const qint64 exact = value.toInteger(invalidInteger);
    if (exact != invalidInteger) {
        output = static_cast<std::int64_t>(exact);
        return true;
    }
    return scn::common::toIntegerHz(value.toDouble(), output);
}
std::size_t count(const QJsonObject& object, const char* key, std::size_t fallback, std::size_t maximum)
{
    const double value = number(object, key, static_cast<double>(fallback));
    if (value < 0 || value > static_cast<double>(maximum) || std::floor(value) != value)
        throw std::runtime_error(std::string("Invalid integer config field: ") + key);
    return static_cast<std::size_t>(value);
}
QJsonObject object(const QJsonObject& parent, const char* key)
{
    if (parent.contains(key) && !parent.value(key).isObject())
        throw std::runtime_error(std::string("Expected config object: ") + key);
    return parent.value(key).toObject();
}

[[noreturn]] void comparisonError(const QString& location, const QString& reason)
{
    throw std::runtime_error(QStringLiteral("Invalid comparison %1: %2").arg(location, reason).toStdString());
}

double comparisonNumber(const QJsonObject& value, const char* field, const QString& location)
{
    const auto number = value.value(field);
    if (!number.isDouble() || !std::isfinite(number.toDouble()))
        comparisonError(location, QStringLiteral("%1 must be a present, finite JSON number").arg(QString::fromLatin1(field)));
    return number.toDouble();
}

QJsonArray comparisonDetections(const QJsonObject& result, const QString& role)
{
    const auto frameIndex = result.value("fileFrameIndex");
    if (!frameIndex.isDouble() || frameIndex.toInteger(-1) < 0)
        comparisonError(role, QStringLiteral("fileFrameIndex must be a nonnegative integer"));
    const auto location = QStringLiteral("%1 frame %2").arg(role).arg(frameIndex.toInteger());
    const auto stage = comparisonNumber(result, "stage", location);
    if (stage != static_cast<int>(algorithm::DetectionStage::Accumulating) &&
        stage != static_cast<int>(algorithm::DetectionStage::Completed))
        comparisonError(location, QStringLiteral("stage must be Accumulating or Completed; bypassed, failed or cancelled results cannot be compared"));
    if (!result.value("detections").isArray())
        comparisonError(location, QStringLiteral("detections must be a present JSON array (use [] for zero observations)"));
    const auto detections = result.value("detections").toArray();
    for (qsizetype i = 0; i < detections.size(); ++i) {
        const auto observation = QStringLiteral("%1 detections[%2]").arg(location).arg(i);
        if (!detections.at(i).isObject())
            comparisonError(observation, QStringLiteral("observation must be a JSON object"));
        const auto value = detections.at(i).toObject();
        const auto id = value.value("id");
        if (!id.isDouble() || id.toInteger(-1) < 0)
            comparisonError(observation, QStringLiteral("id must be a nonnegative integer"));
        const double start = comparisonNumber(value, "startHz", observation);
        const double end = comparisonNumber(value, "endHz", observation);
        const double center = comparisonNumber(value, "centerHz", observation);
        const double width = comparisonNumber(value, "bandwidthHz", observation);
        const double confidence = comparisonNumber(value, "confidence", observation);
        (void)comparisonNumber(value, "signalDbm", observation);
        (void)comparisonNumber(value, "noiseDbm", observation);
        (void)comparisonNumber(value, "cnrDb", observation);
        const double span = end - start;
        if (!(end > start) || !std::isfinite(span) || width <= 0.0 || center < start || center > end)
            comparisonError(observation, QStringLiteral("coordinates require startHz < endHz, positive finite width, and centerHz within the interval"));
        // This is internal coordinate consistency, not an accuracy threshold
        // between runs. Allow floating-point rounding at the frequency scale.
        const double tolerance = 64.0 * std::numeric_limits<double>::epsilon() *
            std::max({1.0, std::abs(start), std::abs(end), width});
        if (std::abs(width - span) > tolerance)
            comparisonError(observation, QStringLiteral("bandwidthHz must agree with endHz - startHz"));
        if (confidence < 0.0 || confidence > 1.0)
            comparisonError(observation, QStringLiteral("confidence must be in [0,1]"));
    }
    return detections;
}
}

QString resolvedPath(const QString& path)
{
    if (path.isEmpty() || path.contains(QChar(u'\0')))
        pathError(QStringLiteral("An empty path or a path containing NUL is not allowed"), path);
    auto native = nativePath(path);
#ifdef _WIN32
    // Reject device namespaces, alternate data streams and Win32-normalized
    // leaf aliases. Ordinary drive paths and UNC shares remain supported.
    const auto windows = QDir::fromNativeSeparators(path);
    if (windows.startsWith(QStringLiteral("//?/")) || windows.startsWith(QStringLiteral("//./")))
        pathError(QStringLiteral("Windows device/extended namespace paths are not supported"), path);
    for (const auto& component : native.relative_path()) {
        const auto part = pathText(component);
        if (part == "." || part == "..") continue;
        const auto base = part.section(QLatin1Char('.'), 0, 0).toUpper();
        const bool numberedDevice = base.size() == 4 &&
            (base.startsWith("COM") || base.startsWith("LPT")) &&
            QStringLiteral("123456789¹²³").contains(base.back());
        if (part.contains(QLatin1Char(':')) || part.endsWith(QLatin1Char('.')) ||
            part.endsWith(QLatin1Char(' ')) || base == "CON" || base == "PRN" ||
            base == "AUX" || base == "NUL" || base == "CONIN$" || base == "CONOUT$" || numberedDevice)
            pathError(QStringLiteral("Ambiguous or reserved Windows path component"), path);
    }
#endif
    std::error_code error;
    native = fs::absolute(native, error);
    if (error) pathError(QStringLiteral("Cannot make path absolute"), path);
    // weakly_canonical resolves junctions/symlinks in existing ancestors even
    // when the output leaf (or its directory) has not been created yet.
    const auto canonical = fs::weakly_canonical(native, error);
    if (error) pathError(QStringLiteral("Cannot resolve path ancestors (%1)")
        .arg(QString::fromStdString(error.message())), path);
    // Fail closed on dangling links rather than treating them as free names.
    for (auto ancestor = native; !ancestor.empty();) {
        if (fs::is_symlink(pathStatus(ancestor, false)) && !fs::exists(pathStatus(ancestor, true)))
            pathError(QStringLiteral("Dangling symlink in path"), path);
        const auto parent = ancestor.parent_path();
        if (parent == ancestor) break;
        ancestor = parent;
    }
    return pathText(canonical);
}

void validateExportPaths(const QStringList& inputs, const QStringList& outputs,
                         const QString& stageDirectory)
{
    std::vector<fs::path> protectedPaths;
    for (const auto& input : inputs)
        if (!input.isEmpty()) protectedPaths.push_back(nativePath(resolvedPath(input)));
    std::vector<fs::path> files;
    for (const auto& output : outputs) {
        const auto path = nativePath(resolvedPath(output));
        for (const auto& input : protectedPaths) {
            if (sameFile(path, input) || containsPath(path, input) || containsPath(input, path))
                pathError(QStringLiteral("Output conflicts with protected input %1").arg(pathText(input)), output);
        }
        for (const auto& previous : files) {
            if (sameFile(path, previous) || containsPath(path, previous) || containsPath(previous, path))
                pathError(QStringLiteral("Output paths conflict with each other (%1)").arg(pathText(previous)), output);
        }
        requireNewPath(path);
        files.push_back(path);
    }
    if (stageDirectory.isEmpty()) return;
    const auto directory = nativePath(resolvedPath(stageDirectory));
    requireNewPath(directory);
    // Reserve the whole stage namespace, including its manifest and all future
    // frame/window files, before the exporter creates anything.
    for (const auto& path : protectedPaths) {
        if (sameFile(directory, path) || containsPath(directory, path) || containsPath(path, directory))
            pathError(QStringLiteral("Stage directory conflicts with protected input %1").arg(pathText(path)), stageDirectory);
    }
    for (const auto& path : files) {
        if (sameFile(directory, path) || containsPath(directory, path) || containsPath(path, directory))
            pathError(QStringLiteral("Stage directory conflicts with output %1").arg(pathText(path)), stageDirectory);
    }
}

void openNewOutput(QFile& file, const QString& path)
{
    file.setFileName(resolvedPath(path));
    // NewOnly is checked by the file-open operation, not just by preflight:
    // a file/link appearing after validation is never opened for truncation.
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        pathError(QStringLiteral("Cannot create new output (existing files are never overwritten): %1")
            .arg(file.errorString()), path);
}

void writeJsonFile(const QString& path, const QJsonObject& object)
{
    writeJson(path, object);
}

QJsonObject modelProvenance(const algorithm::DetectionConfig& config, const QString& modelInfo)
{
    const auto path = QString::fromStdString(config.backend == algorithm::DetectionBackend::Ffscn
        ? config.ffscn.modelPath : config.detector.modelPath);
    QJsonObject result{{"path", path}, {"loaded", config.enabled}, {"info", modelInfo}};
    // TensorRT reports the SHA-256 of the bytes it actually deserialized. Use
    // that value, rather than hashing a potentially replaced model afterwards.
    const auto hashField = QStringLiteral("; sha256=");
    const auto marker = modelInfo.lastIndexOf(hashField);
    const auto loadedHash = marker < 0 ? QString()
        : modelInfo.mid(marker + hashField.size()).section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    const bool validHash = loadedHash.size() == 64 && std::all_of(loadedHash.begin(), loadedHash.end(),
        [](QChar c) { return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                            (c >= QLatin1Char('a') && c <= QLatin1Char('f')); });
    if (config.enabled) {
        if (!validHash) throw std::runtime_error("Loaded TensorRT engine did not report a valid SHA-256; cannot export provenance.");
        result.insert("sha256", loadedHash);
        result.insert("hashSource", "loaded_engine_bytes");
    } else if (!path.isEmpty() && fs::exists(pathStatus(nativePath(path), true))) {
        QFile file(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
            pathError(QStringLiteral("Cannot hash configured model for export"), path);
        result.insert("sha256", QString::fromLatin1(hash.result().toHex()));
        result.insert("hashSource", "configured_file_not_loaded");
    } else {
        result.insert("sha256", QJsonValue(QJsonValue::Null));
        result.insert("hashSource", "no_model_loaded_detection_disabled");
    }
    if (!path.isEmpty()) result.insert("canonicalPath", resolvedPath(path));
    return result;
}

algorithm::DetectionConfig readConfiguration(const QString& path)
{
    algorithm::DetectionConfig c;
    if (path.isEmpty()) return c;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Unable to read detection config JSON.");
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Invalid detection config JSON object.");
    const auto root = doc.object();
    if (root.contains("enabled")) {
        if (!root["enabled"].isBool()) throw std::runtime_error("enabled must be boolean.");
        c.enabled = root["enabled"].toBool();
    }
    if (root.contains("backend")) {
        if (!root.value("backend").isString()) throw std::runtime_error("backend must be 'scn' or 'ffscn'.");
        const auto backend = root.value("backend").toString().toLower();
        if (backend == QStringLiteral("scn")) c.backend = algorithm::DetectionBackend::Scn;
        else if (backend == QStringLiteral("ffscn")) c.backend = algorithm::DetectionBackend::Ffscn;
        else throw std::runtime_error("backend must be 'scn' or 'ffscn'.");
    }
    c.maxSignals = count(root, "maxSignals", c.maxSignals, 65536);
    c.accumulator.frames = count(object(root, "accumulator"), "frames", c.accumulator.frames, 256);
    const auto d = object(root, "detector");
    if (d.contains("modelPath")) {
        if (!d["modelPath"].isString()) throw std::runtime_error("modelPath must be a string.");
        c.detector.modelPath = d["modelPath"].toString().toStdString();
    }
    c.detector.deviceIndex = static_cast<int>(count(d, "deviceIndex", c.detector.deviceIndex, 1024));
    c.detector.inputLength = count(d, "inputLength", c.detector.inputLength, 32768);
    c.detector.windowStep = count(d, "windowStep", c.detector.windowStep, 32768);
    c.detector.topK = count(d, "topK", c.detector.topK, 8192);
    c.detector.maxCandidatesPerWindow = count(d, "maxCandidatesPerWindow", c.detector.maxCandidatesPerWindow, 8192);
    c.detector.confidenceThreshold = static_cast<float>(number(d, "confidenceThreshold", c.detector.confidenceThreshold));
    c.detector.nmsIou = static_cast<float>(number(d, "nmsIou", c.detector.nmsIou));
    const auto ffscn = object(root, "ffscn");
    if (ffscn.contains("modelPath")) {
        if (!ffscn.value("modelPath").isString()) throw std::runtime_error("ffscn.modelPath must be a string.");
        c.ffscn.modelPath = ffscn.value("modelPath").toString().toStdString();
    }
    c.ffscn.deviceIndex = static_cast<int>(count(ffscn, "deviceIndex", c.ffscn.deviceIndex, 1024));
    c.ffscn.confidenceThreshold = static_cast<float>(number(ffscn, "confidenceThreshold", c.ffscn.confidenceThreshold));
    c.ffscn.nmsIou = static_cast<float>(number(ffscn, "nmsIou", c.ffscn.nmsIou));
    c.ffscn.topK = count(ffscn, "topK", c.ffscn.topK, 32768);
    c.ffscn.maxCandidatesPerWindow = count(ffscn, "maxCandidatesPerWindow", c.ffscn.maxCandidatesPerWindow, 32768);
    c.refine.cnrThresholdDb = static_cast<float>(number(object(root, "refine"), "cnrThresholdDb", c.refine.cnrThresholdDb));
    const auto f = object(root, "fusion");
    c.fusion.iou = number(f, "iou", c.fusion.iou);
    c.fusion.overlapRatio = number(f, "overlapRatio", c.fusion.overlapRatio);
    if (!integerHz(f, "gapHz", c.fusion.gapHz, c.fusion.gapHz))
        throw std::runtime_error("fusion.gapHz must round to an integer Hz value.");
    const auto t = object(root, "tracker");
    c.tracker.overlapRatio = number(t, "overlapRatio", c.tracker.overlapRatio);
    c.tracker.maxMissSeconds = number(t, "maxMissSeconds", c.tracker.maxMissSeconds);
    if (t.contains("boundaryStabilityEnabled")) {
        if (!t.value("boundaryStabilityEnabled").isBool())
            throw std::runtime_error("boundaryStabilityEnabled must be a boolean.");
        c.tracker.boundaryStabilityEnabled = t.value("boundaryStabilityEnabled").toBool();
    }
    c.tracker.maxBandwidthRatio = number(t, "maxBandwidthRatio", c.tracker.maxBandwidthRatio);
    c.tracker.centerDistanceRatio = number(t, "centerDistanceRatio", c.tracker.centerDistanceRatio);
    c.tracker.medianWindow = count(t, "medianWindow", c.tracker.medianWindow, 31);
    c.tracker.smoothingAlpha = number(t, "smoothingAlpha", c.tracker.smoothingAlpha);
    c.tracker.jumpConfirmationCount = count(t, "jumpConfirmationCount", c.tracker.jumpConfirmationCount, 20);
    c.tracker.jumpEdgeChangeRatio = number(t, "jumpEdgeChangeRatio", c.tracker.jumpEdgeChangeRatio);
    c.tracker.jumpCenterToleranceRatio = number(t, "jumpCenterToleranceRatio", c.tracker.jumpCenterToleranceRatio);
    c.tracker.jumpBandwidthToleranceRatio = number(t, "jumpBandwidthToleranceRatio", c.tracker.jumpBandwidthToleranceRatio);
    std::string error;
    if (!algorithm::validateConfig(c, error)) throw std::runtime_error(error);
    return c;
}
QJsonObject configurationJson(const algorithm::DetectionConfig& c)
{
    return {{"enabled", c.enabled},
        {"backend", c.backend == algorithm::DetectionBackend::Ffscn ? "ffscn" : "scn"},
        {"maxSignals", static_cast<qint64>(c.maxSignals)},
        {"accumulator", QJsonObject{{"frames", static_cast<qint64>(c.accumulator.frames)}}},
        {"detector", QJsonObject{{"modelPath", QString::fromStdString(c.detector.modelPath)}, {"deviceIndex", c.detector.deviceIndex},
            {"inputLength", static_cast<qint64>(c.detector.inputLength)}, {"windowStep", static_cast<qint64>(c.detector.windowStep)},
            {"confidenceThreshold", c.detector.confidenceThreshold}, {"nmsIou", c.detector.nmsIou},
            {"topK", static_cast<qint64>(c.detector.topK)}, {"maxCandidatesPerWindow", static_cast<qint64>(c.detector.maxCandidatesPerWindow)}}},
        {"ffscn", QJsonObject{{"modelPath", QString::fromStdString(c.ffscn.modelPath)},
            {"deviceIndex", c.ffscn.deviceIndex}, {"inputLength", static_cast<qint64>(c.ffscn.inputLength)},
            {"frameCount", static_cast<qint64>(c.ffscn.frameCount)},
            {"confidenceThreshold", c.ffscn.confidenceThreshold}, {"nmsIou", c.ffscn.nmsIou},
            {"topK", static_cast<qint64>(c.ffscn.topK)},
            {"maxCandidatesPerWindow", static_cast<qint64>(c.ffscn.maxCandidatesPerWindow)}}},
        {"refine", QJsonObject{{"cnrThresholdDb", c.refine.cnrThresholdDb}}},
        {"fusion", QJsonObject{{"iou", c.fusion.iou}, {"overlapRatio", c.fusion.overlapRatio},
            {"gapHz", static_cast<qint64>(c.fusion.gapHz)}}},
        {"tracker", QJsonObject{{"overlapRatio", c.tracker.overlapRatio}, {"maxMissSeconds", c.tracker.maxMissSeconds},
            {"boundaryStabilityEnabled", c.tracker.boundaryStabilityEnabled},
            {"maxBandwidthRatio", c.tracker.maxBandwidthRatio}, {"centerDistanceRatio", c.tracker.centerDistanceRatio},
            {"medianWindow", static_cast<qint64>(c.tracker.medianWindow)},
            {"smoothingAlpha", c.tracker.smoothingAlpha},
            {"jumpConfirmationCount", static_cast<qint64>(c.tracker.jumpConfirmationCount)},
            {"jumpEdgeChangeRatio", c.tracker.jumpEdgeChangeRatio},
            {"jumpCenterToleranceRatio", c.tracker.jumpCenterToleranceRatio},
            {"jumpBandwidthToleranceRatio", c.tracker.jumpBandwidthToleranceRatio}}}};
}
QJsonObject resultJson(const algorithm::DetectionResult& r, std::size_t index)
{
    const auto& d = r.diagnostics;
    QJsonArray tracked;
    for (const auto& item : r.trackedDetections) {
        tracked.append(QJsonObject{{"raw", signalJson(item.raw)}, {"stable", signalJson(item.stable)},
            {"boundaryState", static_cast<int>(item.boundaryState)},
            {"pendingCount", static_cast<qint64>(item.pendingCount)},
            {"requiredCount", static_cast<qint64>(item.requiredCount)},
            {"associationIou", item.associationIou}, {"centerDistanceHz", item.centerDistanceHz},
            {"bandwidthRatio", item.bandwidthRatio},
            {"measurementBranch", static_cast<int>(item.measurementBranch)},
            {"diagnostic", QString::fromStdString(item.diagnostic)}});
    }
    return {{"fileFrameIndex", static_cast<qint64>(index)}, {"sequence", QString::number(r.sequence)},
        {"generation", QString::number(r.generation)}, {"configVersion", QString::number(r.configVersion)},
        {"trackingSegment", QString::number(r.trackingSegment)},
        {"backend", r.backend == algorithm::DetectionBackendId::Ffscn ? "ffscn" : "scn"},
        {"firstSequence", QString::number(r.firstSequence)}, {"timestampNs", QString::number(r.timestampNs)},
        {"firstTimestampNs", QString::number(r.firstTimestampNs)}, {"startHz", r.startFrequencyHz}, {"binHz", r.binWidthHz},
        {"pointCount", static_cast<qint64>(r.pointCount)}, {"referenceLevelDbm", r.referenceLevelDbm},
        {"resolutionBandwidthHz", r.resolutionBandwidthHz}, {"sourceName", QString::fromStdString(r.sourceName)},
        {"accumulatedFrames", static_cast<qint64>(r.accumulatedFrames)},
        {"requiredFrames", static_cast<qint64>(r.requiredFrames)}, {"stage", static_cast<int>(r.stage)},
        {"warmupFrames", static_cast<qint64>(r.warmupFrames)},
        {"windowStartTimestampNs", QString::number(r.windowStartTimestampNs)},
        {"windowEndTimestampNs", QString::number(r.windowEndTimestampNs)},
        {"windowSemantics", r.backend == algorithm::DetectionBackendId::Ffscn
            ? "present at least once within the most recent ten processed frames" : "SCN accumulated spectrum result"},
        {"detections", signalsJson(r.detections)}, {"trackedDetections", tracked},
        {"trackingApplied", r.trackingApplied}, {"diagnostics", QJsonObject{
            {"totalMs", d.processingTimeMs}, {"throughputHz", d.throughputHz},
            {"exportFailed", d.exportFailed}, {"diagnosticError", QString::fromStdString(d.diagnosticError)},
            {"accumulationMs", d.accumulationTimeMs}, {"inferenceMs", d.inferenceTimeMs},
            {"postprocessMs", d.postprocessTimeMs}, {"windows", static_cast<qint64>(d.windowCount)},
            {"candidates", static_cast<qint64>(d.candidateCount)}, {"cnrAccepted", static_cast<qint64>(d.cnrAcceptedCount)},
            {"truncated", static_cast<qint64>(d.truncatedCount)},
            {"missingFrames", static_cast<qint64>(d.missingFrames)},
            {"message", QString::fromStdString(d.message)}}}};
}
QJsonObject compareResults(const QJsonObject& reference, const QJsonObject& actual, double binHz)
{
    const auto before = comparisonDetections(reference, QStringLiteral("reference"));
    const auto after = comparisonDetections(actual, QStringLiteral("current"));
    if (reference.value("fileFrameIndex").toInteger() != actual.value("fileFrameIndex").toInteger())
        comparisonError(QStringLiteral("reference/current"), QStringLiteral("fileFrameIndex values differ"));
    if (!std::isfinite(binHz) || binHz <= 0.0)
        comparisonError(QStringLiteral("current"), QStringLiteral("bin width must be finite and positive"));
    std::vector<bool> matched(static_cast<std::size_t>(before.size()));
    QJsonArray pairs;
    for (const auto item : after) {
        const auto a = item.toObject(); int best = -1; double bestIou = 0;
        for (int i = 0; i < before.size(); ++i) {
            if (matched[static_cast<std::size_t>(i)]) continue;
            const auto b = before[i].toObject();
            const auto left = std::max(a["startHz"].toDouble(), b["startHz"].toDouble());
            const auto right = std::min(a["endHz"].toDouble(), b["endHz"].toDouble());
            const double intersection = std::max(0.0, right - left);
            const double aWidth = a["endHz"].toDouble() - a["startHz"].toDouble();
            const double bWidth = b["endHz"].toDouble() - b["startHz"].toDouble();
            const double scale = std::max(aWidth, bWidth);
            const double normalizedIntersection = intersection / scale;
            const double iou = normalizedIntersection / (aWidth / scale + bWidth / scale - normalizedIntersection);
            if (iou > bestIou) { bestIou = iou; best = i; }
        }
        if (best < 0 || bestIou < 0.5) { pairs.append(QJsonObject{{"actual", a}, {"matched", false}}); continue; }
        matched[static_cast<std::size_t>(best)] = true;
        const auto b = before[best].toObject();
        const double startError = (a["startHz"].toDouble() - b["startHz"].toDouble()) / binHz;
        const double endError = (a["endHz"].toDouble() - b["endHz"].toDouble()) / binHz;
        const double cnrDelta = a["cnrDb"].toDouble() - b["cnrDb"].toDouble();
        if (!std::isfinite(startError) || !std::isfinite(endError) || !std::isfinite(cnrDelta))
            comparisonError(QStringLiteral("reference/current"), QStringLiteral("numeric range overflow while calculating differences"));
        pairs.append(QJsonObject{{"actualId", a["id"]}, {"referenceId", b["id"]}, {"matched", true}, {"iou", bestIou},
            {"startErrorBins", startError},
            {"endErrorBins", endError},
            {"confidenceDelta", a["confidence"].toDouble() - b["confidence"].toDouble()},
            {"cnrDeltaDb", cnrDelta}});
    }
    return {{"fileFrameIndex", actual["fileFrameIndex"]}, {"referenceFrameIndex", reference["fileFrameIndex"]},
        {"referenceCount", before.size()}, {"actualCount", after.size()},
        {"unmatchedReferenceCount", static_cast<int>(std::count(matched.begin(), matched.end(), false))}, {"pairs", pairs}};
}

StageExporter::StageExporter(QString directory) : m_directory(std::move(directory))
{
    m_directory = resolvedPath(m_directory);
    const auto path = nativePath(m_directory);
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) pathError(QStringLiteral("Cannot create stage parent directory"), m_directory);
    if (!fs::create_directory(path, error) || error)
        pathError(QStringLiteral("Stage run directory must be newly created; refusing to reuse it"), m_directory);
}

void StageExporter::beginFrame(const algorithm::SpectrumFrame& frame, std::size_t fileFrameIndex)
{
    if (!m_run || frame.sequence <= m_lastSequence) ++m_run;
    m_lastSequence = frame.sequence;
    m_fileFrameIndex = fileFrameIndex;
    m_prefix = QDir(m_directory).filePath(QStringLiteral("run_%1_frame_%2").arg(m_run).arg(frame.sequence));
    writeFloats(m_prefix + "_raw.f32", frame.powerDb);
    writeJson(m_prefix + "_frame.json", {{"fileFrameIndex", static_cast<qint64>(fileFrameIndex)},
        {"sequence", QString::number(frame.sequence)}, {"timestampNs", QString::number(frame.timestampNs)},
        {"startHz", frame.startFrequencyHz}, {"binHz", frame.binWidthHz},
        {"pointCount", static_cast<qint64>(frame.powerDb.size())}, {"rbwHz", frame.resolutionBandwidthHz},
        {"referenceLevelDbm", frame.referenceLevelDbm}, {"source", QString::fromStdString(frame.sourceName)}});
}

void StageExporter::accumulated(const algorithm::SpectrumFrame&, const std::vector<float>& avg, const std::vector<float>& max)
{
    writeFloats(m_prefix + "_average.f32", avg);
    writeFloats(m_prefix + "_maximum.f32", max);
}
void StageExporter::window(std::uint64_t, algorithm::SpectrumBranch branch, std::size_t start, std::size_t valid,
    const std::vector<float>& input, const algorithm::ScnModelOutput& output,
    const std::vector<algorithm::ScnCandidate>& candidates, const std::vector<algorithm::DetectedSignal>& refined)
{
    const auto prefix = m_prefix + QStringLiteral("_branch_%1_window_%2").arg(static_cast<int>(branch)).arg(start);
    writeFloats(prefix + "_input.f32", input); writeFloats(prefix + "_heatmap.f32", output.heatmap);
    writeFloats(prefix + "_bandwidth.f32", output.bandwidth); writeFloats(prefix + "_offset.f32", output.offset);
    QJsonArray bands;
    for (const auto& c : candidates) bands.append(QJsonObject{{"beginBin", c.beginBin}, {"endBin", c.endBin},
        {"confidence", c.confidence}, {"peakIndex", static_cast<qint64>(c.peakIndex)}});
    writeJson(prefix + ".json", {{"windowStartBin", static_cast<qint64>(start)}, {"validLength", static_cast<qint64>(valid)},
        {"branch", static_cast<int>(branch)}, {"candidates", bands}, {"cnrAccepted", signalsJson(refined)}});
}
void StageExporter::ffscnWindow(std::uint64_t, std::size_t start, std::size_t width,
    const std::vector<algorithm::SpectrumFrame>& frames, const std::vector<float>& input,
    const algorithm::FfscnModelOutput& output,
    const std::vector<algorithm::FfscnCandidate>& candidates,
    const std::vector<algorithm::DetectedSignal>& refined)
{
    const auto prefix = m_prefix + QStringLiteral("_ffscn_window_%1").arg(start);
    writeFloats(prefix + "_input.f32", input);
    writeFloats(prefix + "_hm.f32", output.heatmap);
    writeFloats(prefix + "_bw.f32", output.bandwidth);
    writeFloats(prefix + "_off.f32", output.offset);
    QJsonArray bands, rows;
    for (const auto& candidate : candidates) bands.append(QJsonObject{
        {"beginBin", candidate.beginBin}, {"endBin", candidate.endBin},
        {"confidence", candidate.confidence}, {"peakIndex", static_cast<qint64>(candidate.peakIndex)}});
    for (const auto& frame : frames) rows.append(QJsonObject{
        {"sequence", QString::number(frame.sequence)}, {"timestampNs", QString::number(frame.timestampNs)},
        {"startHz", frame.startFrequencyHz}, {"binHz", frame.binWidthHz},
        {"pointCount", static_cast<qint64>(frame.powerDb.size())}});
    writeJson(prefix + ".json", {{"backend", "ffscn"}, {"windowStartBin", static_cast<qint64>(start)},
        {"inputWidth", static_cast<qint64>(width)}, {"outputWidth", static_cast<qint64>(output.heatmap.size())},
        {"inputLayout", "[1,1,10,N] row-major; normalized"}, {"frames", rows},
        {"candidates", bands}, {"cnrAccepted", signalsJson(refined)}});
}
void StageExporter::fused(const algorithm::DetectionResult& r)
{
    writeJson(m_prefix + "_result.json", resultJson(r, m_fileFrameIndex));
}
}
