#include "Device.hpp"
#include "Backend.hpp"
#include "output/OutputDevice.hpp"
#include "output/OutputRuntime.hpp"
#include "output/TimerResolution.hpp"

namespace V2::Output {
namespace {
class SimulatedDevice final : public IOutputDevice {
public:
    int Initialize(const OutputRuntimeDescriptor& descriptor) override {
        if (!std::holds_alternative<std::monostate>(descriptor.endpoint)) return err_net_cmd;
        (void)EnsureTimerResolution();
        return MockHardwareMgr.Initialize();
    }
    int ReleaseAll(std::chrono::milliseconds timeout) override { return MockHardwareMgr.ReleaseAllOutputAndWait(timeout); }
    int Shutdown(std::chrono::milliseconds timeout) override { return MockHardwareMgr.Shutdown(timeout); }
    OutputConnectionState ConnectionState() const override {
        return MockHardwareMgr.IsInitialized() ? OutputConnectionState::Connected : OutputConnectionState::Disconnected;
    }
    OutputDeviceCapabilities Capabilities() const override {
        OutputDeviceCapabilities value;
        value.identityConfirmed = MockHardwareMgr.IsInitialized();
        value.identityTag = "Mock offline device";
        value.capabilitySource = "offline-mock";
        value.keyboard = value.mouseMask = value.timedMove = true;
        return value;
    }
    PhysicalInputSnapshot PhysicalInput() override { return MockHardwareMgr.PhysicalInput(); }
    int Move(int x, int y, int duration) override { return MockHardwareMgr.RecordMove(x, y, duration); }
    int Button(int button, bool down, bool force) override {
        return force && !down ? MockHardwareMgr.ForceReleaseMouseButton(button) : MockHardwareMgr.RecordButton(button, down);
    }
    int ButtonState(std::uint32_t mask, bool force) override { return MockHardwareMgr.SetMouseButtonStateMask(mask, force); }
    int ReleaseButtons() override { return MockHardwareMgr.ForceReleaseMouseButtons(); }
    int Keyboard(unsigned char modifiers, const std::vector<unsigned char>& usages, OutputIntent intent) override {
        return MockHardwareMgr.RecordKeyboardReport(modifiers, usages, intent);
    }
    int Mask(std::uint32_t mask, std::shared_ptr<OutputCommandCompletion> completion) override {
        const int status = mask ? MockHardwareMgr.MaskMouse(mask) : MockHardwareMgr.UnmaskAll();
        if (completion) {
            std::lock_guard lock(completion->mutex);
            completion->status = status;
            completion->completed = true;
            completion->cv.notify_all();
        }
        return status;
    }
    int RecoverMaskTimeout(const std::shared_ptr<OutputCommandCompletion>&,
        std::shared_ptr<OutputCommandCompletion>& cleanup) override {
        cleanup.reset();
        return MockHardwareMgr.UnmaskAll();
    }
};
}
IOutputDevice& MockDevice() {
    static SimulatedDevice device;
    return device;
}
MockHardwareSnapshot SnapshotMock() { return MockHardwareMgr.Snapshot(); }
void SetMockFaultMode(MockFaultMode mode) { MockHardwareMgr.SetFaultMode(mode); }
void ResetMock() { MockHardwareMgr.Reset(); }
std::vector<MockEvent> RecentMockEvents() { return MockHardwareMgr.RecentEvents(); }
void SetMockPhysicalButtons(std::uint32_t buttons) { MockHardwareMgr.SetPhysicalMouseButtons(buttons); }
}
