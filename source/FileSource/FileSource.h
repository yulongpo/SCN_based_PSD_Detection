#pragma once

#include "../ISpectrumSource.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace scn::source
{

/**
 * @brief 原 ISA 离线频谱文件名中的元数据。
 *
 * 原 ISA 的 SpectrumFileSource 写出连续 float32 频谱帧，文件名使用：
 * `..._Fc=..._Bw=..._Rbw=..._Reflevel=..._SpectrumLen=....dat`。
 */
struct FileSourceMetadata
{
    bool hasCenterFrequency = false;
    bool hasBandwidth = false;
    bool hasResolutionBandwidth = false;
    bool hasReferenceLevel = false;
    bool hasSpectrumLength = false;
    std::int64_t centerFrequencyHz = 0;
    std::int64_t bandwidthHz = 0;
    std::int64_t resolutionBandwidthHz = 0;
    double referenceLevelDbm = 0.0;
    std::size_t spectrumLength = 0;
    std::uint64_t fileSizeBytes = 0;
    std::size_t completeFrameCount = 0;
    std::size_t trailingBytes = 0;
    bool isBinary = false;
};

class FileSource final : public ISpectrumSource
{
public:
    algorithm::SourceKind kind() const noexcept override { return algorithm::SourceKind::File; }
    std::string name() const override { return "FILE"; }

    /**
     * @brief 读取文件存在性及原 ISA 文件名元数据，不读取全部数据。
     */
    static bool inspectFile(const std::string& path,
                            FileSourceMetadata& metadata,
                            std::string& error);

    bool open(const SourceConfig& config, std::string& error) override;
    bool start() override;
    void pause(bool paused) override { m_paused = paused; }
    void stop() override;
    bool read(algorithm::SpectrumFrame& frame) override;
    bool isLive() const noexcept override { return false; }
    bool seekFrame(std::size_t frameIndex, std::string& error);
    std::size_t frameCount() const noexcept;
    std::size_t position() const noexcept { return m_frameIndex; }

private:
    bool loadTextValues(const std::string& path, std::string& error);
    bool readBinaryFrame(std::vector<float>& values);
    bool readTextFrame(std::vector<float>& values);
    bool rewindBinary();
    bool readOneFrame(std::vector<float>& values);
    void closeFile();

    SourceConfig m_config{};
    FileSourceMetadata m_metadata{};
    std::ifstream m_binaryInput;
    std::vector<float> m_textValues;
    std::vector<float> m_frameBuffer;
    std::size_t m_textOffset = 0;
    std::size_t m_frameLength = 0;
    std::uint64_t m_sequence = 0;
    std::size_t m_frameIndex = 0;
    bool m_textFile = false;
    std::size_t m_binaryFrameCount = 0;
    std::size_t m_binaryTrailingBytes = 0;
    bool m_running = false;
    bool m_paused = false;
};

} // namespace scn::source
