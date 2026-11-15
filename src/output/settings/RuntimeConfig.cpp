#include <Windows.h>
#include "output/OutputRuntime.hpp"
#include "Utils/Config.hpp"
#include "output/DeviceRegistry.hpp"

namespace V2::Output {
    OutputRuntimeDescriptor RuntimeDescriptorFromConfig()
    {
#ifdef UNLEASHED_OBSERVATION_ONLY
        return OutputRuntimeDescriptor::Disabled();
#endif
        std::lock_guard<std::mutex> lock(OW::Config::mutex);
        if (!OW::Config::outputEnabled)
            return OutputRuntimeDescriptor::Disabled();

        switch (static_cast<DeviceId>(OW::Config::outputDeviceType)) {
        case DeviceId::KmBoxNet: {
            const int commandPort = OW::Config::outputPort;
            const int monitorPort = OW::Config::EffectiveOutputMonitorPort();
            return OutputRuntimeDescriptor::Network(
                OW::Config::outputIp,
                OW::Config::IsValidOutputUdpPort(commandPort)
                    ? static_cast<std::uint16_t>(commandPort)
                    : 0,
                OW::Config::outputMac,
                OW::Config::IsValidOutputUdpPort(monitorPort)
                    ? static_cast<std::uint16_t>(monitorPort)
                    : 0);
        }
        case DeviceId::KmBoxSerial:
            return OutputRuntimeDescriptor::Serial(OW::Config::outputComPort);
        case DeviceId::Mock:
            return OutputRuntimeDescriptor::Mock();
        case DeviceId::Makcu4M:
            return OutputRuntimeDescriptor::Serial(OW::Config::outputComPort,4000000,V2::Output::SerialProtocol::Makcu);
        case DeviceId::Makcu115200:
            return OutputRuntimeDescriptor::Serial(OW::Config::outputComPort,115200,V2::Output::SerialProtocol::Makcu);
        case DeviceId::CatBox:
            return OutputRuntimeDescriptor::CatBox(OW::Config::catBoxIp,
                OW::Config::IsValidOutputUdpPort(OW::Config::catBoxPort) ? static_cast<uint16_t>(OW::Config::catBoxPort) : 0,
                OW::Config::catBoxUuid,
                OW::Config::IsValidOutputUdpPort(OW::Config::catBoxMonitorPort) ? static_cast<uint16_t>(OW::Config::catBoxMonitorPort) : 0);
        case DeviceId::Ferrum:
            return OutputRuntimeDescriptor::Serial(OW::Config::outputComPort,115200,V2::Output::SerialProtocol::Ferrum);
        default:
            return OutputRuntimeDescriptor::Disabled();
        }
    }

}
