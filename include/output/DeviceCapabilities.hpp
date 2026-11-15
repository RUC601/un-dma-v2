#pragma once
#include "output/OutputRuntime.hpp"
#include "output/DeviceRegistry.hpp"

namespace V2::Output {
struct Capabilities : OutputDeviceCapabilities { const char* name = "关闭"; };
inline Capabilities Describe(const OutputRuntimeDescriptor& descriptor) {
    Capabilities result;
    static_cast<OutputDeviceCapabilities&>(result) = CapabilitiesFor(descriptor);
    result.name = OutputDeviceName(descriptor);
    return result;
}
}
