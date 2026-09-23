#include "HarogicSource.h"

#if defined(SCN_HAS_HAROGIC_SDK) && SCN_HAS_HAROGIC_SDK
#include <htra_api/htra_api.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <thread>

namespace scn::source
{

namespace
{
constexpr double kMinimumFrequencyHz = 9.0e3;
constexpr double kMaximumFrequencyHz = 6.4e9;
constexpr double kMinimumSpanHz = 20.0;
constexpr auto kSweepTimeout = std::chrono::seconds(2);

bool finite(double value)
{
    return std::isfinite(value);
}

#if defined(SCN_HAS_HAROGIC_SDK) && SCN_HAS_HAROGIC_SDK
std::string htraError(const char* operation, int status)
{
    std::ostringstream stream;
    stream << operation << " failed (HTRA status " << status << ")";
    return stream.str();
}

Window_TypeDef windowType(RbwShape shape)
{
    switch (shape) {
    case RbwShape::Flattop:
        return FlatTop;
    case RbwShape::Cispr:
        return Gaussian_CISPR;
    case RbwShape::Nuttall:
    default:
        return Blackman_Nuttall;
    }
}
#endif
} // namespace

HarogicSource::HarogicSource() = default;

HarogicSource::~HarogicSource()
{
    closeDevice();
}

algorithm::SourceKind HarogicSource::kind() const noexcept
{
    return algorithm::SourceKind::Harogic;
}

std::string HarogicSource::name() const
{
    return "Harogic";
}

bool HarogicSource::open(const SourceConfig& config, std::string& error)
{
    stop();
    closeDevice();

    m_config = {};
    m_sequence = 0;
    m_totalHops = 0;
    m_partialTraceLength = 0;
    m_traceLength = 0;
    m_startFrequencyHz = 0.0;
    m_binWidthHz = 0.0;
    m_actualRbwHz = 0.0;
    m_frequency.clear();
    m_power.clear();
    m_partialFrequency.clear();
    m_partialPower.clear();

    if (config.kind != algorithm::SourceKind::Harogic) {
        error = "HarogicSource received an incompatible source kind.";
        return false;
    }

    const double centerFrequencyHz = static_cast<double>(config.centerFrequencyHz);
    const double bandwidthHz = static_cast<double>(config.bandwidthHz);
    const double startFrequencyHz = centerFrequencyHz - bandwidthHz / 2.0;
    const double endFrequencyHz = centerFrequencyHz + bandwidthHz / 2.0;
    if (!finite(config.centerFrequencyHz) || !finite(config.bandwidthHz) ||
        config.bandwidthHz < kMinimumSpanHz || !finite(startFrequencyHz) ||
        !finite(endFrequencyHz) || startFrequencyHz < kMinimumFrequencyHz ||
        endFrequencyHz > kMaximumFrequencyHz || endFrequencyHz <= startFrequencyHz) {
        error = "Harogic frequency configuration is outside the supported 9 kHz to 6.4 GHz range.";
        return false;
    }
    if (!finite(config.resolutionBandwidthHz) || config.resolutionBandwidthHz <= 0.0) {
        error = "Harogic resolution bandwidth must be a positive finite value.";
        return false;
    }
    if (!finite(config.referenceLevelDbm)) {
        error = "Harogic reference level must be a finite value.";
        return false;
    }
    if (config.rbwShape < RbwShape::Nuttall || config.rbwShape > RbwShape::Cispr) {
        error = "Harogic RBW window shape is invalid.";
        return false;
    }

#if !defined(SCN_HAS_HAROGIC_SDK) || !SCN_HAS_HAROGIC_SDK
    error = "Harogic HTRA SDK is not configured. Set SCN_HAROGIC_SDK_ROOT to the HTRA SDK.";
    return false;
#else
    BootProfile_TypeDef bootProfile{};
    bootProfile.DevicePowerSupply = USBPortAndPowerPort;
    bootProfile.PhysicalInterface = USB;
    BootInfo_TypeDef bootInfo{};

    const int status = Device_Open(&m_device, 0, &bootProfile, &bootInfo);
    if (status != APIRETVAL_NoError) {
        error = htraError("Device_Open", status);
        m_device = nullptr;
        return false;
    }

    if (!configureDevice(config, error)) {
        closeDevice();
        return false;
    }

    m_config = config;
    m_open = true;
    return true;
#endif
}

bool HarogicSource::configureDevice(const SourceConfig& config, std::string& error)
{
#if !defined(SCN_HAS_HAROGIC_SDK) || !SCN_HAS_HAROGIC_SDK
    (void)config;
    error = "Harogic HTRA SDK is not configured.";
    return false;
#else
    if (!m_device) {
        error = "Harogic device is not open.";
        return false;
    }

    SWP_Profile_TypeDef profileIn{};
    SWP_Profile_TypeDef profileOut{};
    SWP_TraceInfo_TypeDef traceInfo{};

    int status = SWP_ProfileDeInit(&m_device, &profileIn);
    if (status != APIRETVAL_NoError) {
        error = htraError("SWP_ProfileDeInit", status);
        return false;
    }

    const double centerFrequencyHz = static_cast<double>(config.centerFrequencyHz);
    const double bandwidthHz = static_cast<double>(config.bandwidthHz);
    const double startFrequencyHz = centerFrequencyHz - bandwidthHz / 2.0;
    const double endFrequencyHz = centerFrequencyHz + bandwidthHz / 2.0;

    profileIn.StartFreq_Hz = startFrequencyHz;
    profileIn.StopFreq_Hz = endFrequencyHz;
    profileIn.FreqAssignment = StartStop;
    profileIn.RBWMode = RBW_Manual;
    profileIn.RBW_Hz = static_cast<double>(config.resolutionBandwidthHz);
    profileIn.VBWMode = VBW_EqualToRBW;
    profileIn.VBW_Hz = static_cast<double>(config.resolutionBandwidthHz);
    profileIn.Window = windowType(config.rbwShape);
    profileIn.SweepTimeMode = SWTMode_minSWT;
    profileIn.Detector = Detector_Sample;
    profileIn.TraceType = ClearWrite;
    profileIn.TracePoints = static_cast<std::uint32_t>(
        std::min<std::size_t>(config.pointCount, std::numeric_limits<std::uint32_t>::max()));
    profileIn.TracePointsStrategy = PointsAccuracyPreferred;
    profileIn.TraceAlign = AlignToStart;
    profileIn.FFTExecutionStrategy = Auto;
    profileIn.RxPort = ExternalPort;
    profileIn.SpurRejection = Enhanced;
    profileIn.ReferenceClockSource = ReferenceClockSource_Internal;
    profileIn.TriggerSource = InternalFreeRun;
    profileIn.TriggerEdge = RisingEdge;
    profileIn.GainStrategy = LowNoisePreferred;
    profileIn.Preamplifier = AutoOn;
    profileIn.Atten = -1;
    profileIn.EnableIFAGC = 1;
    profileIn.RefLevel_dBm = config.referenceLevelDbm;
    profileIn.LOOptimization = LOOpt_Auto;

    status = SWP_Configuration(&m_device, &profileIn, &profileOut, &traceInfo);
    if (status != APIRETVAL_NoError) {
        error = htraError("SWP_Configuration", status);
        return false;
    }

    if (traceInfo.FullsweepTracePoints <= 0 || traceInfo.TotalHops <= 0 ||
        traceInfo.PartialsweepTracePoints <= 0 || !finite(traceInfo.TraceBinBW_Hz) ||
        traceInfo.TraceBinBW_Hz <= 0.0 || !finite(traceInfo.StartFreq_Hz)) {
        error = "Harogic returned invalid sweep trace information.";
        return false;
    }

    m_totalHops = traceInfo.TotalHops;
    m_partialTraceLength = traceInfo.PartialsweepTracePoints;
    m_traceLength = static_cast<std::size_t>(traceInfo.FullsweepTracePoints);
    m_startFrequencyHz = traceInfo.StartFreq_Hz;
    m_binWidthHz = traceInfo.TraceBinBW_Hz;
    m_actualRbwHz = finite(profileOut.RBW_Hz) && profileOut.RBW_Hz > 0.0
        ? profileOut.RBW_Hz : static_cast<double>(config.resolutionBandwidthHz);

    m_frequency.resize(m_traceLength);
    m_power.assign(m_traceLength, -150.0F);
    m_partialFrequency.resize(static_cast<std::size_t>(m_partialTraceLength));
    m_partialPower.resize(static_cast<std::size_t>(m_partialTraceLength));
    for (std::size_t index = 0; index < m_frequency.size(); ++index)
        m_frequency[index] = m_startFrequencyHz + m_binWidthHz * static_cast<double>(index);

    return true;
#endif
}

bool HarogicSource::start()
{
    if (!m_open || !m_device) return false;
    m_paused = false;
    m_running = true;
    return true;
}

void HarogicSource::pause(bool paused)
{
    if (m_running) m_paused = paused;
}

void HarogicSource::stop()
{
    m_running = false;
    m_paused = false;
}

bool HarogicSource::read(algorithm::SpectrumFrame& frame)
{
    if (!m_open || !m_running || m_paused || !m_device || m_traceLength == 0 ||
        m_totalHops <= 0 || m_partialTraceLength <= 0) {
        return false;
    }

#if !defined(SCN_HAS_HAROGIC_SDK) || !SCN_HAS_HAROGIC_SDK
    (void)frame;
    return false;
#else
    std::fill(m_power.begin(), m_power.end(), -150.0F);
    const auto deadline = std::chrono::steady_clock::now() + kSweepTimeout;
    MeasAuxInfo_TypeDef measurementInfo{};
    int completedHops = 0;

    while (completedHops < m_totalHops) {
        std::fill(m_partialPower.begin(), m_partialPower.end(), -150.0F);
        int hopIndex = 0;
        int frameIndex = 0;
        const int status = SWP_GetPartialSweep(&m_device,
                                               m_partialFrequency.data(),
                                               m_partialPower.data(),
                                               &hopIndex,
                                               &frameIndex,
                                               &measurementInfo);
        const bool overflow = status == APIRETVAL_WARNING_IFOverflow;
        if (status == APIRETVAL_NoError || overflow) {
            const std::size_t offset = static_cast<std::size_t>(completedHops) *
                static_cast<std::size_t>(m_partialTraceLength);
            if (offset < m_power.size()) {
                const std::size_t count = std::min<std::size_t>(
                    static_cast<std::size_t>(m_partialTraceLength), m_power.size() - offset);
                std::copy_n(m_partialPower.begin(), count, m_power.begin() + offset);
                std::copy_n(m_partialFrequency.begin(), count, m_frequency.begin() + offset);
            }
            ++completedHops;
            continue;
        }
        if (status == APIRETVAL_WARNING_DataNotReady) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        return false;
    }

    for (float& value : m_power) {
        if (!std::isfinite(value)) value = -150.0F;
    }

    const auto systemNow = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto hardwareTimestamp = measurementInfo.nsSinceEpoch;
    const auto timestamp = hardwareTimestamp > 0 &&
            hardwareTimestamp <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
        ? static_cast<std::int64_t>(hardwareTimestamp)
        : systemNow;

    frame = {};
    frame.sequence = ++m_sequence;
    frame.timestampNs = timestamp;
    frame.startFrequencyHz = m_startFrequencyHz;
    frame.binWidthHz = m_binWidthHz;
    frame.resolutionBandwidthHz = m_actualRbwHz;
    frame.referenceLevelDbm = m_config.referenceLevelDbm;
    frame.sourceName = name();
    frame.powerDb = m_power;
    return true;
#endif
}

bool HarogicSource::isLive() const noexcept
{
    return true;
}

void HarogicSource::closeDevice()
{
#if defined(SCN_HAS_HAROGIC_SDK) && SCN_HAS_HAROGIC_SDK
    if (m_device) Device_Close(&m_device);
#endif
    m_device = nullptr;
    m_open = false;
    m_running = false;
    m_paused = false;
}

} // namespace scn::source
