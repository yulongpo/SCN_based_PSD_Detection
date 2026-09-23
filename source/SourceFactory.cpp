#include "SourceFactory.h"

#include "BB60CSource/BB60CSource.h"
#include "FileSource/FileSource.h"
#include "HarogicSource/HarogicSource.h"

#if defined(SCN_HAS_BB60C_SDK) && SCN_HAS_BB60C_SDK
#include <bb_series/bb_api.h>
#endif

#if defined(SCN_HAS_HAROGIC_SDK) && SCN_HAS_HAROGIC_SDK
#include <htra_api/htra_api.h>
#endif

namespace scn::source
{

std::unique_ptr<ISpectrumSource> createSource(algorithm::SourceKind kind)
{
    switch (kind) {
    case algorithm::SourceKind::BB60C:
        return std::make_unique<BB60CSource>();
    case algorithm::SourceKind::Harogic:
        return std::make_unique<HarogicSource>();
    case algorithm::SourceKind::File:
        return std::make_unique<FileSource>();
    }
    return nullptr;
}

LiveSourcePresence probeLiveSourcePresence() noexcept
{
    LiveSourcePresence presence;

#if defined(SCN_HAS_BB60C_SDK) && SCN_HAS_BB60C_SDK
    int serialNumbers[BB_MAX_DEVICES]{};
    int deviceTypes[BB_MAX_DEVICES]{};
    int bbDeviceCount = 0;
    if (bbGetSerialNumberList2(serialNumbers, deviceTypes, &bbDeviceCount) == bbNoError) {
        const int boundedCount = bbDeviceCount < 0 ? 0 :
            (bbDeviceCount > BB_MAX_DEVICES ? BB_MAX_DEVICES : bbDeviceCount);
        for (int index = 0; index < boundedCount; ++index) {
            if (deviceTypes[index] == BB_DEVICE_BB60C) {
                presence.bb60c = true;
                break;
            }
        }
    }
#endif

#if defined(SCN_HAS_HAROGIC_SDK) && SCN_HAS_HAROGIC_SDK
    BootProfile_TypeDef bootProfile{};
    bootProfile.DevicePowerSupply = USBPortAndPowerPort;
    bootProfile.PhysicalInterface = USB;
    uint8_t harogicDeviceCount = 0;
    uint8_t deviceNumbers[MAX_DEVICE]{};
    DeviceInfo_TypeDef deviceInfo[MAX_DEVICE]{};
    if (Device_List(&bootProfile, &harogicDeviceCount, deviceNumbers, deviceInfo) == APIRETVAL_NoError)
        presence.harogic = harogicDeviceCount > 0;
#endif

    return presence;
}

} // namespace scn::source
