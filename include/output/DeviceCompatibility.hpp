#pragma once

#include <array>
#include <span>

namespace V2::Output {

enum class PureDeviceStage {
    ExistingBackend,
    PlannedBackend,
    StaticReferenceOnly
};

struct PureDevicePlan {
    const char* key;
    const char* label;
    const char* transport;
    PureDeviceStage stage;
    bool hasPureIdentityBranch;
    bool hasCurrentUnBackend;
};

inline constexpr std::array<PureDevicePlan, 5> kPureDevicePlans{{
    {"kmbox-usb", "KMBox USB", "serial / firmware-dependent", PureDeviceStage::PlannedBackend, true, true},
    {"makcu", "MAKCU", "serial text or firmware-dependent binary", PureDeviceStage::ExistingBackend, true, true},
    {"macku-controller", "Macku Controller", "serial controller / joystick", PureDeviceStage::PlannedBackend, true, false},
    {"macku-medius", "Macku Medius", "6 Mbaud framed serial", PureDeviceStage::PlannedBackend, true, false},
    {"kmbox-net-pro8k", "KMBox Net / Pro 8K", "UDP endpoint", PureDeviceStage::PlannedBackend, true, true},
}};

inline std::span<const PureDevicePlan> PureDevicePlans() noexcept
{
    return kPureDevicePlans;
}

inline const char* ToString(PureDeviceStage stage) noexcept
{
    switch (stage) {
    case PureDeviceStage::ExistingBackend: return "existing-backend";
    case PureDeviceStage::PlannedBackend: return "planned-backend";
    case PureDeviceStage::StaticReferenceOnly: return "static-reference-only";
    default: return "unknown";
    }
}

}
