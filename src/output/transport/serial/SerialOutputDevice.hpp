#pragma once

#include "SerialTransport.hpp"
#include "output/OutputDevice.hpp"
#include <memory>

namespace V2::Output {

class SerialOutputDevice final : public IOutputDevice {
    // Destruction order keeps protocol callbacks alive until workers stop.
    std::unique_ptr<SerialDeviceProtocol> protocol_;
    SerialTransport transport_;
    bool physicalMonitor_;
public:
    explicit SerialOutputDevice(std::unique_ptr<SerialDeviceProtocol> protocol,
        bool physicalMonitor);
    int Initialize(const OutputRuntimeDescriptor& descriptor) override;
    int ReleaseAll(std::chrono::milliseconds timeout) override;
    int Shutdown(std::chrono::milliseconds timeout) override;
    OutputConnectionState ConnectionState() const override;
    OutputDeviceCapabilities Capabilities() const override;
    PhysicalInputSnapshot PhysicalInput() override;
    int Move(int x, int y, int durationMs) override;
    int Button(int button, bool down, bool force = false) override;
    int ButtonState(std::uint32_t mask, bool force) override;
    int ReleaseButtons() override;
    int Keyboard(unsigned char modifiers, const std::vector<unsigned char>& usages,
        OutputIntent intent) override;
    int Mask(std::uint32_t mask,
        std::shared_ptr<OutputCommandCompletion> completion = {}) override;
    int RecoverMaskTimeout(const std::shared_ptr<OutputCommandCompletion>& timedOut,
        std::shared_ptr<OutputCommandCompletion>& cleanup) override;
};

}
