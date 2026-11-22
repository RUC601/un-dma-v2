#pragma once

#include "output/OutputTypes.hpp"
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace V2::Output {
struct OutputRuntimeDescriptor;

struct OutputDeviceCapabilities {
    bool move = true;
    bool buttons = true;
    bool keyboard = false;
    bool physicalMonitor = false;
    bool release = true;
    bool mouseMask = false;
    bool timedMove = false;
    bool callerMoveSplit = true;
    bool maskTimeoutClosesGate = true;
    bool identityConfirmed = false;
    const char* identityTag = "unconfirmed";
    const char* capabilitySource = "unknown";
    int maxDelta = 32767;
    int verifiedRateHz = 0;
};

// Only the registry sees concrete device classes. Device implementations own
// protocol encoding, connection resources and recovery, including queued work.
class IOutputDevice {
public:
    virtual ~IOutputDevice() = default;
    virtual int Initialize(const OutputRuntimeDescriptor& descriptor) = 0;
    virtual int ReleaseAll(std::chrono::milliseconds timeout) = 0;
    virtual int Shutdown(std::chrono::milliseconds timeout) = 0;
    virtual OutputConnectionState ConnectionState() const = 0;
    virtual OutputDeviceCapabilities Capabilities() const = 0;
    virtual PhysicalInputSnapshot PhysicalInput() = 0;
    virtual int Move(int x, int y, int durationMs) = 0;
    virtual int Button(int button, bool down, bool force = false) = 0;
    virtual int ButtonState(std::uint32_t mask, bool force) = 0;
    virtual int ReleaseButtons() = 0;
    virtual int Keyboard(unsigned char modifiers,
        const std::vector<unsigned char>& usages, OutputIntent intent) = 0;

    // With a completion token, every accepted operation resolves that token.
    // Completion waits happen outside the runtime transition lock.
    virtual int Mask(std::uint32_t mask,
        std::shared_ptr<OutputCommandCompletion> completion = {}) = 0;
    // Called under the runtime generation lock. The implementation may cancel
    // pending work or queue cleanup. Optional cleanup completion is waited later.
    virtual int RecoverMaskTimeout(
        const std::shared_ptr<OutputCommandCompletion>& timedOut,
        std::shared_ptr<OutputCommandCompletion>& cleanup) = 0;
};
}
