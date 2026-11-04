#include "output/OutputRuntime.hpp"

#include "output/MockBackend.hpp"
#include "output/CatBoxBackend.hpp"
#include "output/KmBoxNetBackend.hpp"
#include "output/SerialTransport.hpp"
#include "output/TimerResolution.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"

#include <algorithm>
#include <utility>

namespace V2::Output
{
    namespace
    {
        std::mutex g_runtimeFacadeMutex;
        std::mutex g_preReconcileHookMutex;
        RuntimePreReconcileHook g_preReconcileHook;
        constexpr auto kMaxTransportCompletionWait =
            std::chrono::milliseconds(500);

        std::chrono::milliseconds BoundedTransportCompletionWait(
            std::chrono::milliseconds timeout)
        {
            return (std::min)(timeout, kMaxTransportCompletionWait);
        }

        int WaitForTransportCompletion(
            const std::shared_ptr<OutputCommandCompletion>& completion,
            std::chrono::milliseconds timeout,
            const char* operation)
        {
            std::unique_lock<std::mutex> completionLock(completion->mutex);
            if (!completion->cv.wait_for(
                    completionLock,
                    timeout,
                    [&completion]() { return completion->completed; })) {
                Diagnostics::Warn(
                    "[KMBOX-NET] Timed out waiting for %s transport completion. timeout_ms=%lld",
                    operation ? operation : "command",
                    static_cast<long long>(timeout.count()));
                return err_completion_timeout;
            }
            return completion->status;
        }

        void RecordFirstFailure(int& aggregate, int status)
        {
            if (aggregate == success && status != success)
                aggregate = status;
        }

        bool IsReconcileNoOp(
            OutputRuntimeController& controller,
            const OutputRuntimeDescriptor& desired)
        {
            const OutputRuntimeAppliedState applied = controller.Applied();
            return desired == applied.descriptor &&
                (applied.descriptor.backend == OutputRuntimeBackend::None ||
                 controller.IsOutputGateOpen());
        }

        int ReconcileProductionRuntime(
            const OutputRuntimeDescriptor& desired,
            std::chrono::milliseconds timeout)
        {
            std::lock_guard<std::mutex> transitionLock(g_runtimeFacadeMutex);
            OutputRuntimeController& controller = RuntimeController();
            if (!IsReconcileNoOp(controller, desired)) {
                // Linearize the transaction's "pause new output" step before
                // producer cancellation. Direct move/mask paths do not pass
                // through the scheduler hook and must see the closed gate too.
                controller.PauseOutputForTransition();
                RuntimePreReconcileHook hook;
                {
                    std::lock_guard<std::mutex> hookLock(
                        g_preReconcileHookMutex);
                    hook = g_preReconcileHook;
                }
                if (hook)
                    hook();
            }
            return controller.Reconcile(desired, timeout);
        }
    }

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
    bool SupportsKeyboardOutput(const OutputRuntimeDescriptor& d) {
        if(d.backend==OutputRuntimeBackend::Serial) return SerialMgr.SupportsKeyboard();
        return d.backend==OutputRuntimeBackend::Network || d.backend==OutputRuntimeBackend::Mock || d.backend==OutputRuntimeBackend::CatBox;
    }
    bool SupportsMouseMask(const OutputRuntimeDescriptor& d) {
        if(d.backend==OutputRuntimeBackend::Serial) return SerialMgr.SupportsMask();
        return d.backend==OutputRuntimeBackend::Network || d.backend==OutputRuntimeBackend::Mock || d.backend==OutputRuntimeBackend::CatBox;
    }
    bool SupportsTimedMove(const OutputRuntimeDescriptor& d) {
        if(d.backend!=OutputRuntimeBackend::Serial) return d.backend!=OutputRuntimeBackend::None;
        auto* s=std::get_if<OutputSerialEndpoint>(&d.endpoint);return s && s->protocol==SerialProtocol::KmBox;
    }
    PhysicalInputSnapshot ActivePhysicalInput() {
        const auto active=ActiveRuntimeSnapshot();
        if(active.generation==0) return {};
        if(active.descriptor.backend==OutputRuntimeBackend::Mock) return MockHardwareMgr.PhysicalInput();
        if(active.descriptor.backend==OutputRuntimeBackend::Network) return KmBoxNetMgr.KeyBoard.PhysicalInput();
        if(active.descriptor.backend==OutputRuntimeBackend::Serial) return SerialMgr.PhysicalInput();
        if(active.descriptor.backend==OutputRuntimeBackend::CatBox) return CatBoxMgr.PhysicalInput();
        return {};
    }
    void IOutputRuntimeBackendOps::OnOutputGateChanged(bool) noexcept
    {
    }

    int OutputProductionBackendOps::ReleaseAll(
        OutputRuntimeBackend backend,
        std::chrono::milliseconds timeout)
    {
        switch (backend) {
        case OutputRuntimeBackend::None:
            return success;
        case OutputRuntimeBackend::Network:
            return KmBoxNetMgr.ReleaseAllOutputAndWait(timeout);
        case OutputRuntimeBackend::Serial:
            return SerialMgr.ReleaseAllOutputAndWait(timeout);
        case OutputRuntimeBackend::CatBox:
            return CatBoxMgr.ReleaseAllOutputAndWait(timeout);
        case OutputRuntimeBackend::Mock:
            return MockHardwareMgr.ReleaseAllOutputAndWait(timeout);
        default:
            return err_net_cmd;
        }
    }

    int OutputProductionBackendOps::Shutdown(
        OutputRuntimeBackend backend,
        std::chrono::milliseconds timeout)
    {
        int status = success;
        switch (backend) {
        case OutputRuntimeBackend::None:
            break;
        case OutputRuntimeBackend::Network:
            status = KmBoxNetMgr.Shutdown(timeout);
            break;
        case OutputRuntimeBackend::Serial:
            status = SerialMgr.Shutdown(timeout);
            break;
        case OutputRuntimeBackend::CatBox:
            status = CatBoxMgr.Shutdown(timeout);
            break;
        case OutputRuntimeBackend::Mock:
            status = MockHardwareMgr.Shutdown(timeout);
            break;
        default:
            status = err_net_cmd;
            break;
        }
        ReleaseTimerResolution();
        activeBackend_.store(OutputRuntimeBackend::None);
        return status;
    }

    bool OutputProductionBackendOps::IsTransportReady() const noexcept
    {
        switch(activeBackend_.load()) {
        case OutputRuntimeBackend::Network:return KmBoxNetMgr.GetConnectionState()==OutputConnectionState::Connected;
        case OutputRuntimeBackend::Serial:return SerialMgr.GetConnectionState()==OutputConnectionState::Connected;
        case OutputRuntimeBackend::CatBox:return CatBoxMgr.GetConnectionState()==OutputConnectionState::Connected;
        case OutputRuntimeBackend::Mock:return MockHardwareMgr.IsInitialized();
        default:return false;
        }
    }

    int OutputProductionBackendOps::Initialize(const OutputRuntimeDescriptor& descriptor)
    {
        activeBackend_.store(descriptor.backend);
        switch (descriptor.backend) {
        case OutputRuntimeBackend::None:
            return success;
        case OutputRuntimeBackend::Network: {
            const auto* endpoint = std::get_if<OutputNetworkEndpoint>(&descriptor.endpoint);
            if (!endpoint || endpoint->ip.empty() || endpoint->commandPort == 0 ||
                endpoint->mac.empty() || endpoint->monitorPort == 0) {
                return err_net_cmd;
            }
            (void)EnsureTimerResolution();

            int status = KmBoxNetMgr.InitDevice(
                endpoint->ip,
                static_cast<WORD>(endpoint->commandPort),
                endpoint->mac);
            if (status != success)
                return status;

            const int recoveryStatus = KmBoxNetMgr.RecoverMousePassthrough();
            if (recoveryStatus != success) {
                Diagnostics::Warn(
                    "[OUTPUT-RUNTIME] Network passthrough recovery warning. status=%d",
                    recoveryStatus);
            }

            const int monitorStatus = KmBoxNetMgr.KeyBoard.StartMonitor(
                static_cast<WORD>(endpoint->monitorPort));
            if (monitorStatus != success) {
                Diagnostics::Warn(
                    "[OUTPUT-RUNTIME] Network monitor start warning. port=%u status=%d",
                    static_cast<unsigned int>(endpoint->monitorPort),
                    monitorStatus);
            }
            return success;
        }
        case OutputRuntimeBackend::Serial: {
            const auto* endpoint = std::get_if<OutputSerialEndpoint>(&descriptor.endpoint);
            if (!endpoint)
                return err_net_cmd;
            (void)EnsureTimerResolution();
            return SerialMgr.init(endpoint->port,endpoint->baud,endpoint->protocol);
        }
        case OutputRuntimeBackend::CatBox: {
            const auto* e=std::get_if<CatBoxEndpoint>(&descriptor.endpoint);
            return e ? CatBoxMgr.Initialize(e->ip,e->commandPort,e->uuid,e->monitorPort) : err_net_cmd;
        }
        case OutputRuntimeBackend::Mock:
            if (!std::holds_alternative<std::monostate>(descriptor.endpoint))
                return err_net_cmd;
            (void)EnsureTimerResolution();
            return MockHardwareMgr.Initialize();
        default:
            return err_net_cmd;
        }
    }

    OutputRuntimeController::OutputRuntimeController(IOutputRuntimeBackendOps& backendOps)
        : backendOps_(backendOps)
    {
    }

    int OutputRuntimeController::Reconcile(
        const OutputRuntimeDescriptor& desired,
        std::chrono::milliseconds timeout)
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        desired_ = desired;

        if (desired == active_ &&
            (active_.backend == OutputRuntimeBackend::None ||
             (outputGateOpen_.load(std::memory_order_acquire) && backendOps_.IsTransportReady()))) {
            lastError_ = success;
            return success;
        }

        SetOutputGate(false);
        const OutputRuntimeDescriptor previous = active_;
        int transitionStatus = success;

        if (previous.backend != OutputRuntimeBackend::None) {
            RecordFirstFailure(
                transitionStatus,
                backendOps_.ReleaseAll(previous.backend, timeout));
            RecordFirstFailure(
                transitionStatus,
                backendOps_.Shutdown(previous.backend, timeout));
            active_ = OutputRuntimeDescriptor::Disabled();
        }

        if (transitionStatus != success) {
            lastError_ = transitionStatus;
            return transitionStatus;
        }

        if (desired.backend == OutputRuntimeBackend::None) {
            active_ = OutputRuntimeDescriptor::Disabled();
            generation_.fetch_add(1, std::memory_order_acq_rel);
            lastError_ = success;
            return success;
        }

        const int initializeStatus = backendOps_.Initialize(desired);
        if (initializeStatus != success) {
            // Initialization may have partially opened a transport. Roll it back
            // before publishing the safe, gated-off None state.
            (void)backendOps_.Shutdown(desired.backend, timeout);
            active_ = OutputRuntimeDescriptor::Disabled();
            lastError_ = initializeStatus;
            return initializeStatus;
        }

        active_ = desired;
        generation_.fetch_add(1, std::memory_order_acq_rel);
        lastError_ = success;
        SetOutputGate(true);
        return success;
    }

    int OutputRuntimeController::ReleaseActive(std::chrono::milliseconds timeout)
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        if (active_.backend == OutputRuntimeBackend::None)
            return success;

        SetOutputGate(false);
        const int status = backendOps_.ReleaseAll(active_.backend, timeout);
        lastError_ = status;
        if (status == success) {
            // A successful all-up is an output-session boundary even though
            // the transport remains initialized. Invalidate every producer
            // token captured before the cleanup before reopening dispatch.
            generation_.fetch_add(1, std::memory_order_acq_rel);
            SetOutputGate(true);
        }
        return status;
    }

    int OutputRuntimeController::ShutdownActive(std::chrono::milliseconds timeout)
    {
        return Reconcile(OutputRuntimeDescriptor::Disabled(), timeout);
    }

    void OutputRuntimeController::PauseOutputForTransition()
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        SetOutputGate(false);
    }

    int OutputRuntimeController::DispatchActive(
        const std::function<int(const OutputRuntimeAppliedState&)>& dispatch)
    {
        if (!dispatch)
            return err_net_cmd;

        std::lock_guard<std::mutex> lock(reconcileMutex_);
        const std::uint64_t generation = generation_.load(std::memory_order_acquire);
        if (active_.backend == OutputRuntimeBackend::None ||
            !outputGateOpen_.load(std::memory_order_acquire) ||
            !CanDispatch(generation)) {
            return err_queue_stopped;
        }

        return dispatch({ active_, generation });
    }

    OutputRuntimeDescriptor OutputRuntimeController::Desired() const
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        return desired_;
    }

    OutputRuntimeAppliedState OutputRuntimeController::Applied() const
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        return { active_, generation_.load(std::memory_order_acquire) };
    }

    bool OutputRuntimeController::IsOutputGateOpen() const noexcept
    {
        return outputGateOpen_.load(std::memory_order_acquire) && backendOps_.IsTransportReady();
    }

    bool OutputRuntimeController::CanDispatch(std::uint64_t generation) const noexcept
    {
        return outputGateOpen_.load(std::memory_order_acquire) &&
            backendOps_.IsTransportReady() &&
            generation_.load(std::memory_order_acquire) == generation;
    }

    int OutputRuntimeController::LastError() const
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        return lastError_;
    }

    void OutputRuntimeController::SetOutputGate(bool open) noexcept
    {
        outputGateOpen_.store(open, std::memory_order_release);
        backendOps_.OnOutputGateChanged(open);
    }

    OutputRuntimeController& RuntimeController()
    {
        static OutputProductionBackendOps backendOps;
        static OutputRuntimeController controller(backendOps);
        return controller;
    }

    void SetRuntimePreReconcileHook(RuntimePreReconcileHook hook)
    {
        std::lock_guard<std::mutex> lock(g_preReconcileHookMutex);
        g_preReconcileHook = std::move(hook);
    }

    OutputRuntimeDescriptor RuntimeDescriptorFromConfig()
    {
#ifdef UNLEASHED_OBSERVATION_ONLY
        return OutputRuntimeDescriptor::Disabled();
#endif
        std::lock_guard<std::mutex> lock(OW::Config::mutex);
        if (!OW::Config::outputEnabled)
            return OutputRuntimeDescriptor::Disabled();

        switch (OW::Config::outputDeviceType) {
        case 0: {
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
        case 1:
            return OutputRuntimeDescriptor::Serial(OW::Config::outputComPort);
        case 2:
            return OutputRuntimeDescriptor::Mock();
        case 3:
            return OutputRuntimeDescriptor::Serial(OW::Config::outputComPort,4000000,V2::Output::SerialProtocol::Makcu);
        case 4:
            return OutputRuntimeDescriptor::Serial(OW::Config::outputComPort,115200,V2::Output::SerialProtocol::Makcu);
        case 6:
            return OutputRuntimeDescriptor::CatBox(OW::Config::catBoxIp,
                OW::Config::IsValidOutputUdpPort(OW::Config::catBoxPort) ? static_cast<uint16_t>(OW::Config::catBoxPort) : 0,
                OW::Config::catBoxUuid,
                OW::Config::IsValidOutputUdpPort(OW::Config::catBoxMonitorPort) ? static_cast<uint16_t>(OW::Config::catBoxMonitorPort) : 0);
        case 5:
            return OutputRuntimeDescriptor::Serial(OW::Config::outputComPort,115200,V2::Output::SerialProtocol::Ferrum);
        default:
            return OutputRuntimeDescriptor::Disabled();
        }
    }

    int ReconcileRuntimeFromConfig(std::chrono::milliseconds timeout)
    {
        const OutputRuntimeDescriptor desired = RuntimeDescriptorFromConfig();
        return ReconcileProductionRuntime(desired, timeout);
    }

    OutputRuntimeAppliedState ActiveRuntimeSnapshot()
    {
        return RuntimeController().Applied();
    }

    OutputConnectionState ActiveRuntimeConnectionState()
    {
        const OutputRuntimeAppliedState active = RuntimeController().Applied();
        if (!RuntimeController().CanDispatch(active.generation))
            return OutputConnectionState::Disconnected;

        switch (active.descriptor.backend) {
        case OutputRuntimeBackend::Network:
            return KmBoxNetMgr.GetConnectionState();
        case OutputRuntimeBackend::CatBox:
            return CatBoxMgr.GetConnectionState();
        case OutputRuntimeBackend::Serial:
            return SerialMgr.GetConnectionState();
        case OutputRuntimeBackend::Mock:
            return MockHardwareMgr.IsInitialized()
                ? OutputConnectionState::Connected
                : OutputConnectionState::Disconnected;
        default:
            return OutputConnectionState::Disconnected;
        }
    }

    bool IsRuntimeOutputGateOpen() noexcept
    {
        return RuntimeController().IsOutputGateOpen();
    }

    bool CanDispatchRuntimeGeneration(std::uint64_t generation) noexcept
    {
        return RuntimeController().CanDispatch(generation);
    }

    int ReleaseActiveRuntime(std::chrono::milliseconds timeout)
    {
        return RuntimeController().ReleaseActive(timeout);
    }

    int ShutdownActiveRuntime(std::chrono::milliseconds timeout)
    {
        return ReconcileProductionRuntime(
            OutputRuntimeDescriptor::Disabled(), timeout);
    }

    int DispatchMouseMove(int x, int y, int runtimeMs)
    {
        return RuntimeController().DispatchActive(
            [=](const OutputRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return runtimeMs > 0
                        ? KmBoxNetMgr.Mouse.Move_Auto(x, y, runtimeMs)
                        : KmBoxNetMgr.Mouse.Move(x, y);
                case OutputRuntimeBackend::Serial:
                    return runtimeMs > 0
                        ? SerialMgr.MoveTimed(x, y, runtimeMs)
                        : SerialMgr.Move(x, y);
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Move(x,y,runtimeMs); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordMove(x, y, runtimeMs);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseMoveForGeneration(
        std::uint64_t expectedGeneration,
        int x,
        int y,
        int runtimeMs)
    {
        if (expectedGeneration == 0)
            return err_queue_stopped;

        return RuntimeController().DispatchActive(
            [=](const OutputRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return runtimeMs > 0
                        ? KmBoxNetMgr.Mouse.Move_Auto(x, y, runtimeMs)
                        : KmBoxNetMgr.Mouse.Move(x, y);
                case OutputRuntimeBackend::Serial:
                    return runtimeMs > 0
                        ? SerialMgr.MoveTimed(x, y, runtimeMs)
                        : SerialMgr.Move(x, y);
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Move(x,y,runtimeMs); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordMove(x, y, runtimeMs);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseButton(int button, bool down)
    {
        if (button < 0 || button > 2)
            return err_net_cmd;

        return RuntimeController().DispatchActive(
            [=](const OutputRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    if (button == 0)
                        return KmBoxNetMgr.Mouse.Left(down);
                    if (button == 1)
                        return KmBoxNetMgr.Mouse.Right(down);
                    return KmBoxNetMgr.Mouse.Middle(down);
                case OutputRuntimeBackend::Serial:
                    if (button == 0)
                        return SerialMgr.Left(down);
                    if (button == 1)
                        return SerialMgr.Right(down);
                    return SerialMgr.Middle(down);
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Button(button,down); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordButton(button, down);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseButtonForGeneration(
        std::uint64_t expectedGeneration,
        int button,
        bool down)
    {
        if (button < 0 || button > 2)
            return err_net_cmd;

        const OutputIntent intent = OutputIntentForState(down);
        if (ShouldSuppressOutputForMenu(
                OW::Config::OutputSuppressedByMenu(), intent)) {
            return err_output_suppressed;
        }

        return RuntimeController().DispatchActive(
            [=](const OutputRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    if (button == 0)
                        return KmBoxNetMgr.Mouse.Left(down);
                    if (button == 1)
                        return KmBoxNetMgr.Mouse.Right(down);
                    return KmBoxNetMgr.Mouse.Middle(down);
                case OutputRuntimeBackend::Serial:
                    if (button == 0)
                        return SerialMgr.Left(down);
                    if (button == 1)
                        return SerialMgr.Right(down);
                    return SerialMgr.Middle(down);
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Button(button,down); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordButton(button, down);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchForceReleaseMouseButtonForGeneration(
        std::uint64_t expectedGeneration,
        int button)
    {
        if (button < 0 || button > 2)
            return err_net_cmd;

        return RuntimeController().DispatchActive(
            [=](const OutputRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return KmBoxNetMgr.ForceReleaseMouseButton(button);
                case OutputRuntimeBackend::Serial:
                    if (button == 0)
                        return SerialMgr.Left(false);
                    if (button == 1)
                        return SerialMgr.Right(false);
                    return SerialMgr.Middle(false);
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Button(button,false); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.ForceReleaseMouseButton(button);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseButtonStateMask(std::uint32_t mask, bool force)
    {
        const std::uint32_t effectiveMask = mask & 0x7u;
        return RuntimeController().DispatchActive(
            [=](const OutputRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return KmBoxNetMgr.SetMouseButtonStateMask(effectiveMask, force);
                case OutputRuntimeBackend::CatBox: { int result=success;for(int b=0;b<3;++b) RecordFirstFailure(result,CatBoxMgr.Button(b,(effectiveMask&(1u<<b))!=0));return result; }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.SetMouseButtonStateMask(effectiveMask, force);
                case OutputRuntimeBackend::Serial: {
                    int result=success; RecordFirstFailure(result,SerialMgr.Left((effectiveMask&1)!=0));RecordFirstFailure(result,SerialMgr.Right((effectiveMask&2)!=0));RecordFirstFailure(result,SerialMgr.Middle((effectiveMask&4)!=0));return result; }
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchForceReleaseMouseButtons()
    {
        return RuntimeController().DispatchActive(
            [](const OutputRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return KmBoxNetMgr.ForceReleaseMouseButtons();
                case OutputRuntimeBackend::Serial:
                {
                    int status = success;
                    RecordFirstFailure(status, SerialMgr.Left(false));
                    RecordFirstFailure(status, SerialMgr.Right(false));
                    RecordFirstFailure(status, SerialMgr.Middle(false));
                    return status;
                }
                case OutputRuntimeBackend::CatBox: { int result=success; for(int b=0;b<3;++b) RecordFirstFailure(result,CatBoxMgr.Button(b,false)); return result; }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.ForceReleaseMouseButtons();
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchForceReleaseMouseButton(int button)
    {
        if (button < 0 || button > 2)
            return err_net_cmd;

        return RuntimeController().DispatchActive(
            [=](const OutputRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return KmBoxNetMgr.ForceReleaseMouseButton(button);
                case OutputRuntimeBackend::Serial:
                    if (button == 0)
                        return SerialMgr.Left(false);
                    if (button == 1)
                        return SerialMgr.Right(false);
                    return SerialMgr.Middle(false);
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Button(button,false); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.ForceReleaseMouseButton(button);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseMask(std::uint32_t mask)
    {
        const std::uint32_t effectiveMask = mask & 0x7Fu;
        return RuntimeController().DispatchActive(
            [=](const OutputRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return KmBoxNetMgr.MaskMouse(effectiveMask);
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Mask(effectiveMask); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.MaskMouse(effectiveMask);
                case OutputRuntimeBackend::Serial:
                    return SerialMgr.MaskMouse(effectiveMask);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseUnmask()
    {
        return RuntimeController().DispatchActive(
            [](const OutputRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return KmBoxNetMgr.UnmaskAll();
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Mask(0); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.UnmaskAll();
                case OutputRuntimeBackend::Serial:
                    return SerialMgr.MaskMouse(0);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseMaskForGeneration(
        std::uint64_t expectedGeneration,
        std::uint32_t mask)
    {
        const std::uint32_t effectiveMask = mask & 0x7Fu;
        if (effectiveMask == 0)
            return err_net_cmd;
        return RuntimeController().DispatchActive(
            [=](const OutputRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return KmBoxNetMgr.MaskMouse(effectiveMask);
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Mask(effectiveMask); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.MaskMouse(effectiveMask);
                case OutputRuntimeBackend::Serial:
                    return SerialMgr.MaskMouse(effectiveMask);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseMaskAndWaitForGeneration(
        std::uint64_t expectedGeneration,
        std::uint32_t mask,
        std::chrono::milliseconds timeout)
    {
        const std::uint32_t effectiveMask = mask & 0x7Fu;
        if (effectiveMask == 0 || expectedGeneration == 0 ||
            timeout < std::chrono::milliseconds::zero()) {
            return err_net_cmd;
        }
        timeout = BoundedTransportCompletionWait(timeout);

        auto completion = std::make_shared<OutputCommandCompletion>();
        bool waitForTransportWorker = false;
        const int enqueueStatus = RuntimeController().DispatchActive(
            [=, &waitForTransportWorker](
                const OutputRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    waitForTransportWorker = true;
                    return KmBoxNetMgr.MaskMouseTracked(effectiveMask, completion);
                case OutputRuntimeBackend::CatBox: { waitForTransportWorker=true;return CatBoxMgr.Mask(effectiveMask,completion); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.MaskMouse(effectiveMask);
                case OutputRuntimeBackend::Serial:
                    waitForTransportWorker=true;return SerialMgr.MaskMouse(effectiveMask,completion);
                default:
                    return err_queue_stopped;
                }
            });
        if (enqueueStatus != success || !waitForTransportWorker)
            return enqueueStatus;

        // DispatchActive has returned here, so neither the runtime reconcile
        // mutex nor the network queue mutex is held while transport completes.
        const int completionStatus = WaitForTransportCompletion(
            completion,
            timeout,
            "mouse mask");
        if (completionStatus != err_completion_timeout)
            return completionStatus;

        // If the worker has not popped the mask, remove it by token. Otherwise
        // it may still arrive after this call returns, so place a safety
        // unmask directly behind the in-flight command and wait for it. The
        // cleanup stays queued even if its own bounded wait expires.
        const auto timedOut=ActiveRuntimeSnapshot();
        if(timedOut.descriptor.backend!=OutputRuntimeBackend::Network) {
            RuntimeController().PauseOutputForTransition();
            if(timedOut.descriptor.backend==OutputRuntimeBackend::Serial) (void)SerialMgr.MaskMouse(0);
            if(timedOut.descriptor.backend==OutputRuntimeBackend::CatBox) (void)CatBoxMgr.Mask(0);
            return err_completion_timeout;
        }
        if (KmBoxNetMgr.CancelQueuedMouseMask(completion))
            return err_completion_timeout;

        auto cleanupCompletion = std::make_shared<OutputCommandCompletion>();
        const int cleanupEnqueueStatus = RuntimeController().DispatchActive(
            [=](const OutputRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                if (active.descriptor.backend != OutputRuntimeBackend::Network)
                    return err_net_cmd;
                return KmBoxNetMgr.QueueMouseUnmaskCleanup(cleanupCompletion);
            });
        if (cleanupEnqueueStatus == success) {
            const int cleanupStatus = WaitForTransportCompletion(
                cleanupCompletion,
                timeout,
                "timed-out mouse mask cleanup");
            if (cleanupStatus != success) {
                Diagnostics::Warn(
                    "[KMBOX-NET] Timed-out mouse mask cleanup remains pending or failed. status=%d",
                    cleanupStatus);
            }
        } else if (cleanupEnqueueStatus != err_queue_stopped) {
            Diagnostics::Warn(
                "[KMBOX-NET] Could not queue timed-out mouse mask cleanup. status=%d",
                cleanupEnqueueStatus);
        }
        return err_completion_timeout;
    }

    int DispatchMouseUnmaskForGeneration(
        std::uint64_t expectedGeneration)
    {
        return RuntimeController().DispatchActive(
            [=](const OutputRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return KmBoxNetMgr.UnmaskAll();
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Mask(0); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.UnmaskAll();
                case OutputRuntimeBackend::Serial:
                    return SerialMgr.MaskMouse(0);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseUnmaskAndWaitForGeneration(
        std::uint64_t expectedGeneration,
        std::chrono::milliseconds timeout)
    {
        if (expectedGeneration == 0 ||
            timeout < std::chrono::milliseconds::zero()) {
            return err_net_cmd;
        }
        timeout = BoundedTransportCompletionWait(timeout);

        auto completion = std::make_shared<OutputCommandCompletion>();
        bool waitForTransportWorker = false;
        const int enqueueStatus = RuntimeController().DispatchActive(
            [=, &waitForTransportWorker](
                const OutputRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    waitForTransportWorker = true;
                    return KmBoxNetMgr.UnmaskAllTracked(completion);
                case OutputRuntimeBackend::CatBox: { waitForTransportWorker=true;return CatBoxMgr.Mask(0,completion); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.UnmaskAll();
                case OutputRuntimeBackend::Serial:
                    waitForTransportWorker=true;return SerialMgr.MaskMouse(0,completion);
                default:
                    return err_queue_stopped;
                }
            });
        if (enqueueStatus != success || !waitForTransportWorker)
            return enqueueStatus;

        return WaitForTransportCompletion(
            completion,
            timeout,
            "mouse unmask");
    }

    int DispatchKeyboardReport(
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages)
    {
        soft_keyboard_t validated{};
        if (!KmBoxNetBackend::BuildKeyboardReport(
                modifierMask, usages, validated)) {
            return err_net_cmd;
        }

        const OutputIntent intent = modifierMask == 0 && usages.empty()
            ? OutputIntent::SafetyRelease
            : OutputIntent::Normal;
        if (ShouldSuppressOutputForMenu(
                OW::Config::OutputSuppressedByMenu(), intent)) {
            return err_output_suppressed;
        }

        return RuntimeController().DispatchActive(
            [modifierMask, usages, intent](
                const OutputRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return KmBoxNetMgr.SendKeyboardReport(modifierMask, usages);
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Keyboard(modifierMask,usages,intent); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordKeyboardReport(
                        modifierMask, usages, intent);
                case OutputRuntimeBackend::Serial:
                    return SerialMgr.SendKeyboardReport(modifierMask,usages,intent);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchKeyboardReportForGeneration(
        std::uint64_t expectedGeneration,
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages,
        OutputIntent intent)
    {
        soft_keyboard_t validated{};
        if (!KmBoxNetBackend::BuildKeyboardReport(
                modifierMask, usages, validated)) {
            return err_net_cmd;
        }

        if (ShouldSuppressOutputForMenu(
                OW::Config::OutputSuppressedByMenu(), intent)) {
            return err_output_suppressed;
        }

        return RuntimeController().DispatchActive(
            [expectedGeneration, modifierMask, usages, intent](
                const OutputRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case OutputRuntimeBackend::Network:
                    return KmBoxNetMgr.SendKeyboardReport(
                        modifierMask, usages, intent);
                case OutputRuntimeBackend::CatBox: { return CatBoxMgr.Keyboard(modifierMask,usages,intent); }
                case OutputRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordKeyboardReport(
                        modifierMask, usages, intent);
                case OutputRuntimeBackend::Serial:
                    return SerialMgr.SendKeyboardReport(modifierMask,usages,intent);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchKeyboardKey(unsigned char hidCode, bool down)
    {
        const bool isModifier =
            hidCode >= KEY_LEFTCONTROL && hidCode <= KEY_RIGHT_GUI;
        const bool isUsage = hidCode >= KEY_A && hidCode < KEY_LEFTCONTROL;
        if (!isModifier && !isUsage)
            return err_net_cmd;

        if (!down)
            return DispatchKeyboardReport(0, {});
        if (isModifier) {
            const auto modifierBit = static_cast<unsigned char>(
                1u << (hidCode - KEY_LEFTCONTROL));
            return DispatchKeyboardReport(modifierBit, {});
        }
        return DispatchKeyboardReport(0, { hidCode });
    }
}
