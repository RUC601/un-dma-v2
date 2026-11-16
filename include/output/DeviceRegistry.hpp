#pragma once

#include <cstddef>
#include <span>

namespace V2::Output {
class IOutputDevice;
struct OutputRuntimeDescriptor;

enum class SettingsKind { Network, Serial, CatBox, DhzBox, Mock };
enum class DeviceId : int {
    KmBoxNet = 0,
    KmBoxSerial = 1,
    Mock = 2,
    Makcu4M = 3,
    Makcu115200 = 4,
    Ferrum = 5,
    CatBox = 6,
    DhzBox = 7
};
struct DeviceChoice {
    int legacyId;
    const char* label;
    SettingsKind settingsKind;
};

std::span<const DeviceChoice> DeviceChoices();
const DeviceChoice& DeviceChoiceForId(int legacyId);
int NormalizeLegacyDeviceId(int legacyId);
const char* OutputTransportTag(const OutputRuntimeDescriptor& descriptor);
const char* OutputDeviceName(const OutputRuntimeDescriptor& descriptor);
IOutputDevice* ResolveOutputDevice(const OutputRuntimeDescriptor& descriptor);
}
