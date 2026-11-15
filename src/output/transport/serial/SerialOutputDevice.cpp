#include "SerialOutputDevice.hpp"
#include "output/OutputRuntime.hpp"
#include "output/TimerResolution.hpp"

namespace V2::Output {

SerialOutputDevice::SerialOutputDevice(std::unique_ptr<SerialDeviceProtocol> protocol,
    bool physicalMonitor)
    : protocol_(std::move(protocol)), transport_(*protocol_), physicalMonitor_(physicalMonitor) {}

int SerialOutputDevice::Initialize(const OutputRuntimeDescriptor& descriptor) {
    const auto* endpoint = std::get_if<OutputSerialEndpoint>(&descriptor.endpoint);
    if (!endpoint) return err_net_cmd;
    (void)EnsureTimerResolution();
    return transport_.init(endpoint->port, endpoint->baud);
}

int SerialOutputDevice::ReleaseAll(std::chrono::milliseconds timeout) {
    return transport_.ReleaseAllOutputAndWait(timeout);
}

int SerialOutputDevice::Shutdown(std::chrono::milliseconds timeout) {
    return transport_.Shutdown(timeout);
}

OutputConnectionState SerialOutputDevice::ConnectionState() const {
    return transport_.GetConnectionState();
}

OutputDeviceCapabilities SerialOutputDevice::Capabilities() const {
    OutputDeviceCapabilities capabilities;
    capabilities.keyboard = transport_.SupportsKeyboard();
    capabilities.mouseMask = transport_.SupportsMask();
    capabilities.timedMove = protocol_->SupportsTimedMove();
    capabilities.physicalMonitor = physicalMonitor_;
    return capabilities;
}

PhysicalInputSnapshot SerialOutputDevice::PhysicalInput() {
    return transport_.PhysicalInput();
}

int SerialOutputDevice::Move(int x, int y, int durationMs) {
    return durationMs > 0 ? transport_.MoveTimed(x, y, durationMs) : transport_.Move(x, y);
}

int SerialOutputDevice::Button(int button, bool down, bool) {
    return transport_.Button(button, down);
}

int SerialOutputDevice::ButtonState(std::uint32_t mask, bool) {
    int result = success;
    for (int button = 0; button < transport_.MouseButtonCount(); ++button) {
        const int status = transport_.Button(button, (mask & (1u << button)) != 0);
        if (result == success) result = status;
    }
    return result;
}

int SerialOutputDevice::ReleaseButtons() {
    return ButtonState(0, true);
}

int SerialOutputDevice::Keyboard(unsigned char modifiers,
    const std::vector<unsigned char>& usages, OutputIntent intent) {
    return transport_.SendKeyboardReport(modifiers, usages, intent);
}

int SerialOutputDevice::Mask(std::uint32_t mask,
    std::shared_ptr<OutputCommandCompletion> completion) {
    return transport_.MaskMouse(mask, std::move(completion));
}

int SerialOutputDevice::RecoverMaskTimeout(
    const std::shared_ptr<OutputCommandCompletion>&,
    std::shared_ptr<OutputCommandCompletion>& cleanup) {
    cleanup.reset();
    return Mask(0);
}

}
