#include "output/DeviceRegistry.hpp"
#include "output/OutputRuntime.hpp"
#include "output/devices/kmbox_net/Device.hpp"
#include "output/devices/kmbox_serial/Backend.hpp"
#include "output/devices/makcu/Backend.hpp"
#include "output/devices/ferrum/Backend.hpp"
#include "output/devices/catbox/Device.hpp"
#include "output/devices/mock/Device.hpp"
#include <array>
#include <algorithm>

namespace V2::Output {
namespace {
constexpr std::array<DeviceChoice, 7> choices{{
    {0, "KMBox Net", SettingsKind::Network},
    {1, "KMBox Serial", SettingsKind::Serial},
    {2, "Mock", SettingsKind::Mock},
    {3, "MAKCU 4M", SettingsKind::Serial},
    {4, "MAKCU 115200", SettingsKind::Serial},
    {5, "Ferrum Software API", SettingsKind::Serial},
    {6, "CatBox", SettingsKind::CatBox}
}};
}
std::span<const DeviceChoice> DeviceChoices() { return choices; }
int NormalizeLegacyDeviceId(int id) {
    return std::clamp(id, choices.front().legacyId, choices.back().legacyId);
}
const DeviceChoice& DeviceChoiceForId(int id) {
    return choices[static_cast<std::size_t>(NormalizeLegacyDeviceId(id))];
}
IOutputDevice* ResolveOutputDevice(const OutputRuntimeDescriptor& descriptor) {
    switch (descriptor.backend) {
    case OutputRuntimeBackend::Network: return &KmBoxNetDevice();
    case OutputRuntimeBackend::Mock: return &MockDevice();
    case OutputRuntimeBackend::CatBox: return &CatBoxDevice();
    case OutputRuntimeBackend::Serial: {
        const auto* endpoint = std::get_if<OutputSerialEndpoint>(&descriptor.endpoint);
        if (!endpoint) return nullptr;
        switch (endpoint->protocol) {
        case SerialProtocol::KmBox: return &KmBoxSerialDevice();
        case SerialProtocol::Makcu: return &MakcuDevice();
        case SerialProtocol::Ferrum: return &FerrumDevice();
        }
        return nullptr;
    }
    default: return nullptr;
    }
}
const char* OutputTransportTag(const OutputRuntimeDescriptor& descriptor) {
    switch (descriptor.backend) {
    case OutputRuntimeBackend::Network: return "network";
    case OutputRuntimeBackend::Serial: return "serial";
    case OutputRuntimeBackend::CatBox: return "catbox";
    case OutputRuntimeBackend::Mock: return "mock";
    case OutputRuntimeBackend::None: return "none";
    default: return "unknown";
    }
}
const char* OutputDeviceName(const OutputRuntimeDescriptor& descriptor) {
    switch (descriptor.backend) {
    case OutputRuntimeBackend::Network: return "KMBox Net";
    case OutputRuntimeBackend::Mock: return "Mock / 离线";
    case OutputRuntimeBackend::CatBox: return "CatBox / 社区协议 / 实机待验证";
    case OutputRuntimeBackend::Serial: {
        const auto* endpoint = std::get_if<OutputSerialEndpoint>(&descriptor.endpoint);
        if (!endpoint) return "KMBox Serial";
        switch (endpoint->protocol) {
        case SerialProtocol::KmBox: return "KMBox Serial";
        case SerialProtocol::Makcu: return "MAKCU / 实机待验证";
        case SerialProtocol::Ferrum: return "Ferrum Software API / 实机待验证";
        }
        return "关闭";
    }
    default: return "关闭";
    }
}
}
