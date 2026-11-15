#include "output/OutputRuntime.hpp"
#include <utility>

namespace V2::Output {
    OutputRuntimeDescriptor OutputRuntimeDescriptor::Disabled()
    {
        return {};
    }

    OutputRuntimeDescriptor OutputRuntimeDescriptor::Network(
        std::string ip,
        std::uint16_t commandPort,
        std::string mac,
        std::uint16_t monitorPort)
    {
        OutputRuntimeDescriptor descriptor{};
        descriptor.backend = OutputRuntimeBackend::Network;
        descriptor.endpoint = OutputNetworkEndpoint{
            std::move(ip), commandPort, std::move(mac), monitorPort
        };
        return descriptor;
    }

    OutputRuntimeDescriptor OutputRuntimeDescriptor::Serial(std::string port, uint32_t baud, V2::Output::SerialProtocol protocol)
    {
        OutputRuntimeDescriptor descriptor{};
        descriptor.backend = OutputRuntimeBackend::Serial;
        descriptor.endpoint = OutputSerialEndpoint{ std::move(port), baud, protocol };
        return descriptor;
    }

    OutputRuntimeDescriptor OutputRuntimeDescriptor::Mock()
    {
        OutputRuntimeDescriptor descriptor{};
        descriptor.backend = OutputRuntimeBackend::Mock;
        return descriptor;
    }

    OutputRuntimeDescriptor OutputRuntimeDescriptor::CatBox(std::string ip,uint16_t port,std::string uuid,uint16_t monitorPort) {
        OutputRuntimeDescriptor d;d.backend=OutputRuntimeBackend::CatBox;
        d.endpoint=CatBoxEndpoint{std::move(ip),port,std::move(uuid),monitorPort};return d;
    }
}
