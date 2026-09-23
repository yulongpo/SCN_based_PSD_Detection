#include "SpectrumRecorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>

namespace scn::source
{

namespace
{
std::string formatNumber(double value, int precision = 6)
{
    if (!std::isfinite(value)) return "0";
    if (std::abs(value) < 0.5 * std::pow(10.0, -precision)) value = 0.0;
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    auto text = stream.str();
    while (text.size() > 1 && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    if (text == "-0") text = "0";
    return text;
}

std::string timestampForFileName()
{
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    local = *std::localtime(&time);
#endif
    std::ostringstream stream;
    stream << std::put_time(&local, "%Y%m%d_%H%M%S_")
           << std::setw(3) << std::setfill('0') << milliseconds.count();
    return stream.str();
}
}

SpectrumRecorder::~SpectrumRecorder()
{
    stop();
}

std::string SpectrumRecorder::makeFileName(const algorithm::SpectrumFrame& frame)
{
    const double bandwidthHz = frame.binWidthHz * static_cast<double>(frame.powerDb.size());
    const double centerFrequencyHz = frame.startFrequencyHz + bandwidthHz / 2.0;
    std::ostringstream name;
    name << timestampForFileName()
         << "_Fc=" << formatNumber(centerFrequencyHz)
         << "_Bw=" << formatNumber(bandwidthHz)
         << "_Rbw=" << formatNumber(frame.resolutionBandwidthHz)
         << "_Reflevel=" << formatNumber(frame.referenceLevelDbm, 1)
         << "_SpectrumLen=" << frame.powerDb.size()
         << ".dat";
    return name.str();
}

bool SpectrumRecorder::start(const algorithm::SpectrumFrame& firstFrame,
                             const application::RecordingConfig& config,
                             std::string& error)
{
    stop();
    error.clear();
    if (!config.enabled) return true;
    if (!firstFrame.isValid()) {
        error = "The first spectrum frame is invalid; recording cannot start.";
        return false;
    }
    if (config.directory.empty()) {
        error = "A recording directory is required.";
        return false;
    }

    std::error_code directoryError;
    const auto directory = std::filesystem::u8path(config.directory);
    std::filesystem::create_directories(directory, directoryError);
    if (directoryError) {
        error = "Unable to create recording directory: " + directoryError.message();
        return false;
    }

    auto path = directory / std::filesystem::u8path(makeFileName(firstFrame));
    for (std::size_t suffix = 1; std::filesystem::exists(path, directoryError); ++suffix) {
        const auto base = path.stem().string();
        path = directory / std::filesystem::u8path(
            base + "_" + std::to_string(suffix) + ".dat");
        directoryError.clear();
    }

    m_output.open(path, std::ios::binary | std::ios::trunc);
    if (!m_output.is_open()) {
        error = "Unable to open recording file: " + path.u8string();
        return false;
    }
    m_path = path.u8string();
    m_frameCount = 0;
    m_startFrequencyHz = firstFrame.startFrequencyHz;
    m_endFrequencyHz = firstFrame.endFrequencyHz();
    m_resolutionBandwidthHz = firstFrame.resolutionBandwidthHz;
    return true;
}

bool SpectrumRecorder::write(const algorithm::SpectrumFrame& frame, std::string& error)
{
    error.clear();
    if (!m_output.is_open()) return false;
    if (frame.powerDb.empty()) {
        error = "Cannot record an empty spectrum frame.";
        return false;
    }
    if (frame.startFrequencyHz != m_startFrequencyHz ||
        frame.endFrequencyHz() != m_endFrequencyHz ||
        frame.resolutionBandwidthHz != m_resolutionBandwidthHz) {
        error = "Spectrum geometry changed during recording.";
        return false;
    }

    const auto byteCount = static_cast<std::streamsize>(frame.powerDb.size() * sizeof(float));
    m_output.write(reinterpret_cast<const char*>(frame.powerDb.data()), byteCount);
    if (!m_output) {
        error = "Unable to write the spectrum recording file.";
        return false;
    }
    ++m_frameCount;
    return true;
}

void SpectrumRecorder::stop() noexcept
{
    if (m_output.is_open()) {
        m_output.flush();
        m_output.close();
    }
}

} // namespace scn::source
