#include "FileSource.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <limits>
#include <sstream>

namespace scn::source
{

namespace
{
std::string lowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

bool parseNumericToken(const std::string& fileName, const std::string& key, double& value)
{
    const auto keyPosition = fileName.find(key);
    if (keyPosition == std::string::npos) return false;

    const std::size_t valueStart = keyPosition + key.size();
    // Metadata values may contain a decimal point, for example Reflevel=-20.0.
    // The filename grammar uses '_' as the field separator; stopping at '.'
    // silently truncated valid values in the previous implementation.
    std::size_t valueEnd = fileName.find('_', valueStart);
    if (valueEnd == std::string::npos) {
        // The final field is followed by the extension rather than another '_'.
        // Use the last dot so decimal points inside the value remain valid.
        valueEnd = fileName.find_last_of('.');
        if (valueEnd != std::string::npos && valueEnd <= valueStart) {
            valueEnd = std::string::npos;
        }
    }
    const std::string token = fileName.substr(
        valueStart, valueEnd == std::string::npos ? std::string::npos : valueEnd - valueStart);
    if (token.empty()) return false;

    try {
        std::size_t consumed = 0;
        value = std::stod(token, &consumed);
        return consumed == token.size();
    } catch (...) {
        return false;
    }
}

bool parseIntegerToken(const std::string& fileName, const std::string& key, std::size_t& value)
{
    double parsed = 0.0;
    if (!parseNumericToken(fileName, key, parsed) || parsed <= 0.0 ||
        parsed > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}
}

bool FileSource::inspectFile(const std::string& path,
                             FileSourceMetadata& metadata,
                             std::string& error)
{
    metadata = {};
    if (path.empty()) {
        error = "A spectrum file path is required.";
        return false;
    }

    const std::filesystem::path filePath(path);
    std::error_code statusError;
    if (!std::filesystem::exists(filePath, statusError) ||
        !std::filesystem::is_regular_file(filePath, statusError)) {
        error = "Spectrum file does not exist: " + path;
        return false;
    }

    const std::string fileName = filePath.filename().string();
    const std::string extension = lowerExtension(filePath);
    metadata.isBinary = extension != ".txt" && extension != ".csv" && extension != ".asc";
    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(filePath, sizeError);
    if (sizeError) {
        error = "Unable to inspect spectrum file size: " + path;
        return false;
    }
    metadata.fileSizeBytes = static_cast<std::uint64_t>(fileSize);
    double value = 0.0;
    if (parseNumericToken(fileName, "Fc=", value)) {
        metadata.centerFrequencyHz = value;
        metadata.hasCenterFrequency = true;
    }
    if (parseNumericToken(fileName, "Bw=", value)) {
        metadata.bandwidthHz = value;
        metadata.hasBandwidth = true;
    }
    if (parseNumericToken(fileName, "Rbw=", value)) {
        metadata.resolutionBandwidthHz = value;
        metadata.hasResolutionBandwidth = true;
    }
    if (parseNumericToken(fileName, "Reflevel=", value) ||
        parseNumericToken(fileName, "RefLevel=", value)) {
        metadata.referenceLevelDbm = value;
        metadata.hasReferenceLevel = true;
    }
    if (parseIntegerToken(fileName, "SpectrumLen=", metadata.spectrumLength)) {
        metadata.hasSpectrumLength = true;
    }
    if (metadata.isBinary && metadata.hasSpectrumLength) {
        const std::uint64_t frameBytes =
            static_cast<std::uint64_t>(metadata.spectrumLength) * sizeof(float);
        if (frameBytes > 0) {
            metadata.completeFrameCount = static_cast<std::size_t>(metadata.fileSizeBytes / frameBytes);
            metadata.trailingBytes = static_cast<std::size_t>(metadata.fileSizeBytes % frameBytes);
        }
    }
    return true;
}

bool FileSource::open(const SourceConfig& config, std::string& error)
{
    stop();
    closeFile();
    m_textValues.clear();
    m_frameBuffer.clear();
    m_textOffset = 0;
    m_sequence = 0;
    m_binaryFrameCount = 0;
    m_binaryTrailingBytes = 0;
    m_config = config;

    if (!inspectFile(config.filePath, m_metadata, error)) return false;

    const std::filesystem::path filePath(config.filePath);
    const std::string extension = lowerExtension(filePath);
    m_textFile = extension == ".txt" || extension == ".csv" || extension == ".asc";

    m_frameLength = m_metadata.hasSpectrumLength
        ? m_metadata.spectrumLength : config.pointCount;
    if (m_frameLength == 0) {
        error = "Spectrum frame length must be greater than zero.";
        return false;
    }

    m_frameBuffer.resize(m_frameLength);
    if (m_textFile) {
        if (!loadTextValues(config.filePath, error)) return false;
        if (m_textValues.size() < m_frameLength) {
            error = "The spectrum file contains fewer samples than SpectrumLen/pointCount.";
            return false;
        }
    } else {
        if (m_metadata.fileSizeBytes < m_frameLength * sizeof(float)) {
            error = "Binary spectrum file is shorter than one SpectrumLen frame.";
            return false;
        }
        if (m_metadata.fileSizeBytes % sizeof(float) != 0) {
            error = "Binary spectrum file size is not aligned to float32 samples.";
            return false;
        }
        const std::uint64_t frameBytes =
            static_cast<std::uint64_t>(m_frameLength) * sizeof(float);
        m_binaryFrameCount = static_cast<std::size_t>(m_metadata.fileSizeBytes / frameBytes);
        m_binaryTrailingBytes = static_cast<std::size_t>(m_metadata.fileSizeBytes % frameBytes);
        m_binaryInput.open(config.filePath, std::ios::binary);
        if (!m_binaryInput.is_open()) {
            error = "Unable to open binary spectrum file: " + config.filePath;
            return false;
        }
    }

    m_config.pointCount = m_frameLength;
    if (m_metadata.hasCenterFrequency) m_config.centerFrequencyHz = m_metadata.centerFrequencyHz;
    if (m_metadata.hasBandwidth) m_config.bandwidthHz = m_metadata.bandwidthHz;
    if (m_metadata.hasResolutionBandwidth) {
        m_config.resolutionBandwidthHz = m_metadata.resolutionBandwidthHz;
    }
    if (m_metadata.hasReferenceLevel) {
        m_config.referenceLevelDbm = m_metadata.referenceLevelDbm;
    }
    m_paused = false;
    return true;
}

bool FileSource::start()
{
    m_running = true;
    return m_frameLength > 0 && (m_textFile ? !m_textValues.empty() : m_binaryInput.is_open());
}

void FileSource::stop()
{
    m_running = false;
    m_paused = false;
}

bool FileSource::loadTextValues(const std::string& path, std::string& error)
{
    std::ifstream input(path);
    if (!input.is_open()) {
        error = "Unable to open text spectrum file: " + path;
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        for (char& character : line) {
            if (character == ',' || character == ';' || character == '\t') character = ' ';
        }
        std::istringstream values(line);
        float value = 0.0F;
        while (values >> value) m_textValues.push_back(value);
    }
    if (m_textValues.empty()) {
        error = "No float spectrum samples were loaded from: " + path;
        return false;
    }
    return true;
}

bool FileSource::rewindBinary()
{
    if (!m_binaryInput.is_open()) return false;
    m_binaryInput.clear();
    m_binaryInput.seekg(0, std::ios::beg);
    return static_cast<bool>(m_binaryInput);
}

bool FileSource::readBinaryFrame(std::vector<float>& values)
{
    if (!m_binaryInput.is_open()) return false;
    const std::streamsize byteCount = static_cast<std::streamsize>(m_frameLength * sizeof(float));
    m_binaryInput.read(reinterpret_cast<char*>(values.data()), byteCount);
    return m_binaryInput.gcount() == byteCount;
}

bool FileSource::readTextFrame(std::vector<float>& values)
{
    if (m_textOffset + m_frameLength > m_textValues.size()) return false;
    std::copy_n(m_textValues.begin() + static_cast<std::ptrdiff_t>(m_textOffset),
                m_frameLength, values.begin());
    m_textOffset += m_frameLength;
    return true;
}

bool FileSource::readOneFrame(std::vector<float>& values)
{
    if (m_textFile) {
        if (readTextFrame(values)) return true;
        if (!m_config.loopFile) return false;
        m_textOffset = 0;
        return readTextFrame(values);
    }

    if (readBinaryFrame(values)) return true;
    if (!m_config.loopFile || !rewindBinary()) return false;
    return readBinaryFrame(values);
}

bool FileSource::read(algorithm::SpectrumFrame& frame)
{
    if (!m_running || m_paused || m_frameLength == 0) return false;
    if (!readOneFrame(m_frameBuffer)) {
        m_running = false;
        return false;
    }

    const double centerFrequencyHz = m_config.centerFrequencyHz;
    const double bandwidthHz = m_config.bandwidthHz;
    frame = {};
    frame.sequence = ++m_sequence;
    frame.timestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame.startFrequencyHz = centerFrequencyHz - bandwidthHz / 2.0;
    frame.binWidthHz = bandwidthHz / static_cast<double>(m_frameLength);
    frame.resolutionBandwidthHz = m_config.resolutionBandwidthHz;
    frame.referenceLevelDbm = m_config.referenceLevelDbm;
    frame.sourceName = name();
    frame.powerDb = m_frameBuffer;
    return true;
}

void FileSource::closeFile()
{
    if (m_binaryInput.is_open()) m_binaryInput.close();
}

} // namespace scn::source
