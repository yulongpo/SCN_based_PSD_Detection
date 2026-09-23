#pragma once

#include "../algorithm/types/SpectrumTypes.h"
#include "../application/RecordingConfig.h"

#include <cstdint>
#include <fstream>
#include <string>

namespace scn::source
{

/**
 * @brief Writes live PSD frames in the same raw float32 format consumed by FileSource.
 *
 * The generated filename carries the ISA-compatible Fc/Bw/Rbw/Reflevel/SpectrumLen
 * metadata, so a completed recording can be opened directly as a FILE source.
 */
class SpectrumRecorder final
{
public:
    SpectrumRecorder() = default;
    ~SpectrumRecorder();

    SpectrumRecorder(const SpectrumRecorder&) = delete;
    SpectrumRecorder& operator=(const SpectrumRecorder&) = delete;

    bool start(const algorithm::SpectrumFrame& firstFrame,
               const application::RecordingConfig& config,
               std::string& error);
    bool write(const algorithm::SpectrumFrame& frame, std::string& error);
    void stop() noexcept;

    [[nodiscard]] bool isRecording() const noexcept { return m_output.is_open(); }
    [[nodiscard]] const std::string& path() const noexcept { return m_path; }
    [[nodiscard]] std::uint64_t frameCount() const noexcept { return m_frameCount; }
    [[nodiscard]] double startFrequencyHz() const noexcept { return m_startFrequencyHz; }
    [[nodiscard]] double endFrequencyHz() const noexcept { return m_endFrequencyHz; }
    [[nodiscard]] double resolutionBandwidthHz() const noexcept { return m_resolutionBandwidthHz; }

private:
    static std::string makeFileName(const algorithm::SpectrumFrame& frame);

    std::ofstream m_output;
    std::string m_path;
    std::uint64_t m_frameCount = 0;
    double m_startFrequencyHz = 0.0;
    double m_endFrequencyHz = 0.0;
    double m_resolutionBandwidthHz = 0.0;
};

} // namespace scn::source
