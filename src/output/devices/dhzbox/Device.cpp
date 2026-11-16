#include "Device.hpp"
#include "Backend.hpp"
#include "output/OutputDevice.hpp"
#include "output/OutputRuntime.hpp"

namespace V2::Output {
namespace {
class DhzDevice final : public IOutputDevice {
public:
    int Initialize(const OutputRuntimeDescriptor& descriptor) override {
        const auto* endpoint = std::get_if<DhzBoxEndpoint>(&descriptor.endpoint);
        return endpoint ? DhzBoxMgr.Initialize(endpoint->ip, endpoint->commandPort,
            endpoint->randomShift, endpoint->monitorPort) : err_net_cmd;
    }
    int ReleaseAll(std::chrono::milliseconds timeout) override { return DhzBoxMgr.ReleaseAllOutputAndWait(timeout); }
    int Shutdown(std::chrono::milliseconds timeout) override { return DhzBoxMgr.Shutdown(timeout); }
    OutputConnectionState ConnectionState() const override { return DhzBoxMgr.GetConnectionState(); }
    OutputDeviceCapabilities Capabilities() const override {
        OutputDeviceCapabilities c; c.keyboard = c.physicalMonitor = true; c.mouseMask = false; c.timedMove = false; return c;
    }
    PhysicalInputSnapshot PhysicalInput() override { return DhzBoxMgr.PhysicalInput(); }
    int Move(int x, int y, int) override { return DhzBoxMgr.Move(x, y); }
    int Button(int button, bool down, bool) override { return DhzBoxMgr.Button(button, down); }
    int ButtonState(std::uint32_t mask, bool) override {
        int result = success; for (int i = 0; i < 5; ++i) { const int s = DhzBoxMgr.Button(i, (mask & (1u << i)) != 0); if (result == success) result = s; } return result;
    }
    int ReleaseButtons() override { return ButtonState(0, true); }
    int Keyboard(unsigned char modifiers, const std::vector<unsigned char>& usages, OutputIntent intent) override { return DhzBoxMgr.Keyboard(modifiers, usages, intent); }
    int Mask(std::uint32_t, std::shared_ptr<OutputCommandCompletion>) override { return err_net_cmd; }
    int RecoverMaskTimeout(const std::shared_ptr<OutputCommandCompletion>&, std::shared_ptr<OutputCommandCompletion>& cleanup) override { cleanup.reset(); return err_net_cmd; }
};
}
IOutputDevice& DhzBoxDevice() { static DhzDevice device; return device; }
}
