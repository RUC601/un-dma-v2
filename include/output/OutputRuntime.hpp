#pragma once

#include "output/OutputTypes.hpp"
#include "output/SerialProtocol.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

namespace V2::Output
{
    enum class OutputRuntimeBackend : int {
        None = 0,
        Network,
        Serial,
        Mock,
        CatBox
    };

    struct OutputNetworkEndpoint {
        std::string ip;
        std::uint16_t commandPort = 0;
        std::string mac;
        std::uint16_t monitorPort = 0;

        bool operator==(const OutputNetworkEndpoint&) const = default;
    };

    struct OutputSerialEndpoint {
        std::string port;
        uint32_t baud = 115200;
        V2::Output::SerialProtocol protocol=V2::Output::SerialProtocol::KmBox;

        bool operator==(const OutputSerialEndpoint&) const = default;
    };

    struct CatBoxEndpoint {
        std::string ip;
        std::uint16_t commandPort=0;
        std::string uuid;
        std::uint16_t monitorPort=0;
        bool operator==(const CatBoxEndpoint&) const = default;
    };
    using OutputRuntimeEndpoint = std::variant<
        std::monostate,
        OutputNetworkEndpoint,
        OutputSerialEndpoint, CatBoxEndpoint>;

    struct OutputRuntimeDescriptor {
        OutputRuntimeBackend backend = OutputRuntimeBackend::None;
        OutputRuntimeEndpoint endpoint{};

        static OutputRuntimeDescriptor Disabled();
        static OutputRuntimeDescriptor Network(
            std::string ip,
            std::uint16_t commandPort,
            std::string mac,
            std::uint16_t monitorPort);
        static OutputRuntimeDescriptor Serial(std::string port, uint32_t baud = 115200, V2::Output::SerialProtocol protocol=V2::Output::SerialProtocol::KmBox);
        static OutputRuntimeDescriptor Mock();
        static OutputRuntimeDescriptor CatBox(std::string ip, uint16_t port, std::string uuid, uint16_t monitorPort);

        bool operator==(const OutputRuntimeDescriptor&) const = default;
    };

    bool SupportsKeyboardOutput(const OutputRuntimeDescriptor& descriptor);
    bool SupportsMouseMask(const OutputRuntimeDescriptor& descriptor);
    bool SupportsTimedMove(const OutputRuntimeDescriptor& descriptor);
    PhysicalInputSnapshot ActivePhysicalInput();

    struct OutputRuntimeAppliedState {
        OutputRuntimeDescriptor descriptor{};
        std::uint64_t generation = 0;
    };

    inline bool IsRuntimeReady(
        int status,
        const OutputRuntimeAppliedState& applied,
        bool outputGateOpen) noexcept
    {
        return status == success && outputGateOpen &&
            applied.descriptor.backend != OutputRuntimeBackend::None;
    }

    class IOutputRuntimeBackendOps
    {
    public:
        virtual ~IOutputRuntimeBackendOps() = default;

        virtual void OnOutputGateChanged(bool open) noexcept;
        virtual bool IsTransportReady() const noexcept { return true; }
        virtual int ReleaseAll(
            OutputRuntimeBackend backend,
            std::chrono::milliseconds timeout) = 0;
        virtual int Shutdown(
            OutputRuntimeBackend backend,
            std::chrono::milliseconds timeout) = 0;
        virtual int Initialize(const OutputRuntimeDescriptor& descriptor) = 0;
    };

    class OutputProductionBackendOps final : public IOutputRuntimeBackendOps
    {
        std::atomic<OutputRuntimeBackend> activeBackend_{OutputRuntimeBackend::None};
    public:
        bool IsTransportReady() const noexcept override;
        int ReleaseAll(
            OutputRuntimeBackend backend,
            std::chrono::milliseconds timeout) override;
        int Shutdown(
            OutputRuntimeBackend backend,
            std::chrono::milliseconds timeout) override;
        int Initialize(const OutputRuntimeDescriptor& descriptor) override;
    };

    class OutputRuntimeController
    {
    public:
        explicit OutputRuntimeController(IOutputRuntimeBackendOps& backendOps);

        int Reconcile(
            const OutputRuntimeDescriptor& desired,
            std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
        int ReleaseActive(
            std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
        int ShutdownActive(
            std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
        void PauseOutputForTransition();
        int DispatchActive(
            const std::function<int(const OutputRuntimeAppliedState&)>& dispatch);

        OutputRuntimeDescriptor Desired() const;
        OutputRuntimeAppliedState Applied() const;
        bool IsOutputGateOpen() const noexcept;
        bool CanDispatch(std::uint64_t generation) const noexcept;
        int LastError() const;

    private:
        void SetOutputGate(bool open) noexcept;

        IOutputRuntimeBackendOps& backendOps_;
        mutable std::mutex reconcileMutex_;
        OutputRuntimeDescriptor desired_{};
        OutputRuntimeDescriptor active_{};
        std::atomic<std::uint64_t> generation_{ 0 };
        std::atomic<bool> outputGateOpen_{ false };
        int lastError_ = success;
    };

    OutputRuntimeController& RuntimeController();
    using RuntimePreReconcileHook = std::function<void()>;
    void SetRuntimePreReconcileHook(RuntimePreReconcileHook hook);
    OutputRuntimeDescriptor RuntimeDescriptorFromConfig();
    int ReconcileRuntimeFromConfig(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
    OutputRuntimeAppliedState ActiveRuntimeSnapshot();
    OutputConnectionState ActiveRuntimeConnectionState();
    bool IsRuntimeOutputGateOpen() noexcept;
    bool CanDispatchRuntimeGeneration(std::uint64_t generation) noexcept;
    int ReleaseActiveRuntime(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
    int ShutdownActiveRuntime(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500));

    int DispatchMouseMove(int x, int y, int runtimeMs = 0);
    int DispatchMouseMoveForGeneration(
        std::uint64_t expectedGeneration,
        int x,
        int y,
        int runtimeMs = 0);
    int DispatchMouseButton(int button, bool down);
    int DispatchMouseButtonForGeneration(
        std::uint64_t expectedGeneration,
        int button,
        bool down);
    int DispatchForceReleaseMouseButtonForGeneration(
        std::uint64_t expectedGeneration,
        int button);
    int DispatchMouseButtonStateMask(std::uint32_t mask, bool force = false);
    int DispatchForceReleaseMouseButtons();
    int DispatchForceReleaseMouseButton(int button);
    int DispatchMouseMask(std::uint32_t mask);
    int DispatchMouseUnmask();
    int DispatchMouseMaskForGeneration(
        std::uint64_t expectedGeneration,
        std::uint32_t mask);
    int DispatchMouseMaskAndWaitForGeneration(
        std::uint64_t expectedGeneration,
        std::uint32_t mask,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
    int DispatchMouseUnmaskForGeneration(
        std::uint64_t expectedGeneration);
    int DispatchMouseUnmaskAndWaitForGeneration(
        std::uint64_t expectedGeneration,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
    int DispatchKeyboardReport(
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages);
    int DispatchKeyboardReportForGeneration(
        std::uint64_t expectedGeneration,
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages,
        OutputIntent intent = OutputIntent::Normal);
    int DispatchKeyboardKey(unsigned char hidCode, bool down);
}
