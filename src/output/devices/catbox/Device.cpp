#include "Device.hpp"
#include "Backend.hpp"
#include "output/OutputDevice.hpp"
#include "output/OutputRuntime.hpp"

namespace V2::Output {
namespace {
class CatDevice final : public IOutputDevice {
public:
    int Initialize(const OutputRuntimeDescriptor& descriptor) override {
        const auto* endpoint = std::get_if<CatBoxEndpoint>(&descriptor.endpoint);
        return endpoint ? CatBoxMgr.Initialize(endpoint->ip, endpoint->commandPort,
            endpoint->uuid, endpoint->monitorPort) : err_net_cmd;
    }
    int ReleaseAll(std::chrono::milliseconds timeout) override { return CatBoxMgr.ReleaseAllOutputAndWait(timeout); }
    int Shutdown(std::chrono::milliseconds timeout) override { return CatBoxMgr.Shutdown(timeout); }
    OutputConnectionState ConnectionState() const override { return CatBoxMgr.GetConnectionState(); }
    OutputDeviceCapabilities Capabilities() const override {
        OutputDeviceCapabilities value;
        value.keyboard = value.physicalMonitor = value.mouseMask = value.timedMove = true;
        return value;
    }
    PhysicalInputSnapshot PhysicalInput() override { return CatBoxMgr.PhysicalInput(); }
    int Move(int x, int y, int duration) override { return CatBoxMgr.Move(x, y, duration); }
    int Button(int button, bool down, bool) override { return CatBoxMgr.Button(button, down); }
    int ButtonState(std::uint32_t mask, bool) override {
        int result = success;
        for (int button = 0; button < 3; ++button) {
            const int status = CatBoxMgr.Button(button, (mask & (1u << button)) != 0);
            if (result == success) result = status;
        }
        return result;
    }
    int ReleaseButtons() override { return ButtonState(0, true); }
    int Keyboard(unsigned char modifiers, const std::vector<unsigned char>& usages, OutputIntent intent) override {
        return CatBoxMgr.Keyboard(modifiers, usages, intent);
    }
    int Mask(std::uint32_t mask, std::shared_ptr<OutputCommandCompletion> completion) override {
        return CatBoxMgr.Mask(mask, std::move(completion));
    }
    int RecoverMaskTimeout(const std::shared_ptr<OutputCommandCompletion>&,
        std::shared_ptr<OutputCommandCompletion>& cleanup) override {
        cleanup.reset();
        return CatBoxMgr.Mask(0);
    }
};
}
IOutputDevice& CatBoxDevice() {
    static CatDevice device;
    return device;
}
}
