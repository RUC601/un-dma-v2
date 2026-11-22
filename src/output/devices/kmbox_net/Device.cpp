#include "Device.hpp"
#include "Backend.hpp"
#include "output/OutputDevice.hpp"
#include "output/OutputRuntime.hpp"
#include "output/TimerResolution.hpp"
#include "Utils/Diagnostics.hpp"

namespace V2::Output {
namespace {
class NetDevice final : public IOutputDevice {
public:
    int Initialize(const OutputRuntimeDescriptor& descriptor) override {
        const auto* endpoint = std::get_if<OutputNetworkEndpoint>(&descriptor.endpoint);
        if (!endpoint || endpoint->ip.empty() || !endpoint->commandPort ||
            endpoint->mac.empty() || !endpoint->monitorPort) return err_net_cmd;
        (void)EnsureTimerResolution();
        const int status = KmBoxNetMgr.InitDevice(endpoint->ip, endpoint->commandPort, endpoint->mac);
        if (status != success) return status;
        const int recovery = KmBoxNetMgr.RecoverMousePassthrough();
        if (recovery != success)
            Diagnostics::Warn("[OUTPUT-RUNTIME] Network passthrough recovery warning. status=%d", recovery);
        const int monitor = KmBoxNetMgr.KeyBoard.StartMonitor(endpoint->monitorPort);
        if (monitor != success)
            Diagnostics::Warn("[OUTPUT-RUNTIME] Network monitor start warning. port=%u status=%d",
                static_cast<unsigned>(endpoint->monitorPort), monitor);
        return success;
    }
    int ReleaseAll(std::chrono::milliseconds timeout) override { return KmBoxNetMgr.ReleaseAllOutputAndWait(timeout); }
    int Shutdown(std::chrono::milliseconds timeout) override { return KmBoxNetMgr.Shutdown(timeout); }
    OutputConnectionState ConnectionState() const override { return KmBoxNetMgr.GetConnectionState(); }
    OutputDeviceCapabilities Capabilities() const override {
        OutputDeviceCapabilities value;
        value.keyboard = value.physicalMonitor = value.mouseMask = value.timedMove = true;
        value.identityConfirmed = ConnectionState() == OutputConnectionState::Connected;
        value.identityTag = "KMBox Net endpoint echo";
        value.capabilitySource = "runtime-command-and-monitor";
        value.callerMoveSplit = false;
        value.maskTimeoutClosesGate = false;
        return value;
    }
    PhysicalInputSnapshot PhysicalInput() override { return KmBoxNetMgr.KeyBoard.PhysicalInput(); }
    int Move(int x, int y, int duration) override {
        return duration > 0 ? KmBoxNetMgr.Mouse.Move_Auto(x, y, duration) : KmBoxNetMgr.Mouse.Move(x, y);
    }
    int Button(int button, bool down, bool force) override {
        if (force && !down) return KmBoxNetMgr.ForceReleaseMouseButton(button);
        switch (button) {
        case 0: return KmBoxNetMgr.Mouse.Left(down);
        case 1: return KmBoxNetMgr.Mouse.Right(down);
        case 2: return KmBoxNetMgr.Mouse.Middle(down);
        default: return err_net_cmd;
        }
    }
    int ButtonState(std::uint32_t mask, bool force) override { return KmBoxNetMgr.SetMouseButtonStateMask(mask, force); }
    int ReleaseButtons() override { return KmBoxNetMgr.ForceReleaseMouseButtons(); }
    int Keyboard(unsigned char modifiers, const std::vector<unsigned char>& usages, OutputIntent intent) override {
        return KmBoxNetMgr.SendKeyboardReport(modifiers, usages, intent);
    }
    int Mask(std::uint32_t mask, std::shared_ptr<OutputCommandCompletion> completion) override {
        return mask ? KmBoxNetMgr.MaskMouseTracked(mask, completion) : KmBoxNetMgr.UnmaskAllTracked(completion);
    }
    int RecoverMaskTimeout(const std::shared_ptr<OutputCommandCompletion>& timedOut,
        std::shared_ptr<OutputCommandCompletion>& cleanup) override {
        cleanup.reset();
        if (KmBoxNetMgr.CancelQueuedMouseMask(timedOut)) return success;
        cleanup = std::make_shared<OutputCommandCompletion>();
        return KmBoxNetMgr.QueueMouseUnmaskCleanup(cleanup);
    }
};
}
IOutputDevice& KmBoxNetDevice() {
    static NetDevice device;
    return device;
}
}
