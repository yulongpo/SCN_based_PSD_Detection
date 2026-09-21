#include "BB60CSource.h"

#if defined(SCN_HAS_BB60C_SDK) && SCN_HAS_BB60C_SDK
#include <bb_series/bb_api.h>
#endif

#include <chrono>
#include <cmath>
#include <sstream>

namespace scn::source
{

namespace
{
constexpr double kMinimumFrequencyHz = 9.0e3;
constexpr double kMaximumFrequencyHz = 6.4e9;
constexpr double kMinimumSpanHz = 20.0;
constexpr double kMinimumRbwHz = 0.602006912;
constexpr double kMaximumRbwHz = 10.1e6;

bool validFinite(double value)
{
    return std::isfinite(value);
}

#if defined(SCN_HAS_BB60C_SDK) && SCN_HAS_BB60C_SDK
std::string bbError(const char* operation, bbStatus status)
{
    std::ostringstream stream;
    stream << operation << " failed (" << static_cast<int>(status) << "): "
           << (bbGetErrorString(status) ? bbGetErrorString(status) : "unknown BB API error");
    return stream.str();
}
#endif
} // namespace

BB60CSource::BB60CSource() = default;

BB60CSource::~BB60CSource()
{
    closeDevice();
}

algorithm::SourceKind BB60CSource::kind() const noexcept
{
    return algorithm::SourceKind::BB60C;
}

std::string BB60CSource::name() const
{
    return "BB60C";
}

bool BB60CSource::open(const SourceConfig& config, std::string& error)
{
    stop();
    closeDevice();
    m_config = {};
    m_sequence = 0;
    m_traceLength = 0;
    m_binWidthHz = 0.0;
    m_startFrequencyHz = 0.0;
    m_minTrace.clear();
    m_maxTrace.clear();

    if (config.kind != algorithm::SourceKind::BB60C) {
        error = "BB60CSource received an incompatible source kind.";
        return false;
    }
    if (!validFinite(config.centerFrequencyHz) || !validFinite(config.bandwidthHz) ||
        config.bandwidthHz < kMinimumSpanHz ||
        config.centerFrequencyHz - config.bandwidthHz / 2.0 < kMinimumFrequencyHz ||
        config.centerFrequencyHz + config.bandwidthHz / 2.0 > kMaximumFrequencyHz) {
        error = "BB60C frequency configuration is outside the supported 9 kHz to 6.4 GHz range.";
        return false;
    }
    if (!validFinite(config.resolutionBandwidthHz) ||
        config.resolutionBandwidthHz < kMinimumRbwHz ||
        config.resolutionBandwidthHz > kMaximumRbwHz) {
        error = "BB60C resolution bandwidth is outside the supported range.";
        return false;
    }
    if (!validFinite(config.referenceLevelDbm) || config.referenceLevelDbm > 20.0) {
        error = "BB60C reference level must be no greater than 20 dBm.";
        return false;
    }
    if (config.rbwShape < RbwShape::Nuttall || config.rbwShape > RbwShape::Cispr) {
        error = "BB60C RBW window shape is invalid.";
        return false;
    }

#if !defined(SCN_HAS_BB60C_SDK) || !SCN_HAS_BB60C_SDK
    error = "BB60C SDK is not configured. Set SCN_BB60C_SDK_ROOT to the Signal Hound BB API SDK.";
    return false;
#else
    auto fail = [this, &error](const char* operation, bbStatus status) {
        error = bbError(operation, status);
        closeDevice();
        return false;
    };

    bbStatus status = bbOpenDevice(&m_device);
    if (status != bbNoError) return fail("bbOpenDevice", status);

    int deviceType = BB_DEVICE_NONE;
    status = bbGetDeviceType(m_device, &deviceType);
    if (status != bbNoError) return fail("bbGetDeviceType", status);
    if (deviceType != BB_DEVICE_BB60C) {
        std::ostringstream stream;
        stream << "The connected Signal Hound device type is " << deviceType
               << "; a BB60C is required.";
        error = stream.str();
        closeDevice();
        return false;
    }

    status = bbConfigureCenterSpan(m_device, config.centerFrequencyHz, config.bandwidthHz);
    if (status != bbNoError) return fail("bbConfigureCenterSpan", status);
    status = bbConfigureRefLevel(m_device, config.referenceLevelDbm);
    if (status != bbNoError) return fail("bbConfigureRefLevel", status);
    status = bbConfigureGainAtten(m_device, BB_AUTO_GAIN, BB_AUTO_ATTEN);
    if (status != bbNoError) return fail("bbConfigureGainAtten", status);

    // Equal RBW/VBW avoids an additional VBW filtering delay in the live path.
    status = bbConfigureSweepCoupling(m_device,
                                      config.resolutionBandwidthHz,
                                      config.resolutionBandwidthHz,
                                      0.001,
                                      static_cast<uint32_t>(config.rbwShape),
                                      BB_NO_SPUR_REJECT);
    if (status != bbNoError) return fail("bbConfigureSweepCoupling", status);
    status = bbConfigureProcUnits(m_device, BB_LOG);
    if (status != bbNoError) return fail("bbConfigureProcUnits", status);
    status = bbConfigureAcquisition(m_device, BB_MIN_AND_MAX, BB_LOG_SCALE);
    if (status != bbNoError) return fail("bbConfigureAcquisition", status);
    status = bbInitiate(m_device, BB_SWEEPING, 0);
    if (status != bbNoError) return fail("bbInitiate", status);
    m_measurementInitiated = true;

    uint32_t traceLength = 0;
    status = bbQueryTraceInfo(m_device, &traceLength, &m_binWidthHz, &m_startFrequencyHz);
    if (status != bbNoError) return fail("bbQueryTraceInfo", status);
    if (traceLength == 0 || !validFinite(m_binWidthHz) || m_binWidthHz <= 0.0 ||
        !validFinite(m_startFrequencyHz)) {
        error = "BB60C returned invalid trace information.";
        closeDevice();
        return false;
    }

    m_traceLength = static_cast<std::size_t>(traceLength);
    m_minTrace.resize(m_traceLength);
    m_maxTrace.resize(m_traceLength);
    m_config = config;
    m_open = true;
    return true;
#endif
}

bool BB60CSource::start()
{
    if (!m_open || m_device < 0) return false;
    if (m_running) return true;

#if defined(SCN_HAS_BB60C_SDK) && SCN_HAS_BB60C_SDK
    if (!m_measurementInitiated) {
        const bbStatus status = bbInitiate(m_device, BB_SWEEPING, 0);
        if (status != bbNoError) return false;
        m_measurementInitiated = true;
    }
#endif

    m_paused = false;
    m_running = true;
    return true;
}

void BB60CSource::pause(bool paused)
{
    if (!m_running) return;
    m_paused = paused;
}

void BB60CSource::stop()
{
#if defined(SCN_HAS_BB60C_SDK) && SCN_HAS_BB60C_SDK
    if (m_device >= 0 && m_measurementInitiated) {
        bbAbort(m_device);
        m_measurementInitiated = false;
    }
#endif
    m_running = false;
    m_paused = false;
}

bool BB60CSource::read(algorithm::SpectrumFrame& frame)
{
    if (!m_open || !m_running || m_paused || m_traceLength == 0) return false;

#if !defined(SCN_HAS_BB60C_SDK) || !SCN_HAS_BB60C_SDK
    return false;
#else
    const bbStatus status = bbFetchTrace_32f(m_device,
                                             static_cast<int>(m_traceLength),
                                             m_minTrace.data(),
                                             m_maxTrace.data());
    if (status != bbNoError && status != bbADCOverflow) return false;

    frame = {};
    frame.sequence = ++m_sequence;
    frame.timestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame.startFrequencyHz = m_startFrequencyHz;
    frame.binWidthHz = m_binWidthHz;
    frame.resolutionBandwidthHz = m_config.resolutionBandwidthHz;
    frame.referenceLevelDbm = m_config.referenceLevelDbm;
    frame.sourceName = name();
    frame.powerDb = m_maxTrace;
    return true;
#endif
}

bool BB60CSource::isLive() const noexcept
{
    return true;
}

void BB60CSource::closeDevice()
{
#if defined(SCN_HAS_BB60C_SDK) && SCN_HAS_BB60C_SDK
    if (m_device >= 0) {
        if (m_measurementInitiated) bbAbort(m_device);
        bbCloseDevice(m_device);
    }
#endif
    m_device = -1;
    m_open = false;
    m_running = false;
    m_paused = false;
    m_measurementInitiated = false;
}

} // namespace scn::source
