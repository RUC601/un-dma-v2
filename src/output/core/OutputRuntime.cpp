#include "output/OutputRuntime.hpp"
#include "output/OutputDevice.hpp"
#include "output/DeviceRegistry.hpp"
#include "output/DeviceCompatibility.hpp"
#include "output/OutputServices.hpp"
#include "output/KeyboardReport.hpp"
#include "output/TimerResolution.hpp"
#include "Utils/Diagnostics.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace V2::Output {
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
                    "[OUTPUT] Timed out waiting for %s transport completion. timeout_ms=%lld",
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


    OutputDeviceCapabilities CapabilitiesFor(const OutputRuntimeDescriptor& descriptor) {
        auto* device = ResolveOutputDevice(descriptor);
        if (device) return device->Capabilities();
        OutputDeviceCapabilities disabled{};
        disabled.move = disabled.buttons = disabled.release = false;
        disabled.maxDelta = 0;
        return disabled;
    }

    bool SupportsKeyboardOutput(const OutputRuntimeDescriptor& descriptor) {
        return CapabilitiesFor(descriptor).keyboard;
    }
    bool SupportsMouseMask(const OutputRuntimeDescriptor& descriptor) {
        return CapabilitiesFor(descriptor).mouseMask;
    }
    bool SupportsTimedMove(const OutputRuntimeDescriptor& descriptor) {
        return CapabilitiesFor(descriptor).timedMove;
    }
    PhysicalInputSnapshot ActivePhysicalInput() {
        const auto active = ActiveRuntimeSnapshot();
        auto* device = ResolveOutputDevice(active.descriptor);
        return active.generation && device ? device->PhysicalInput() : PhysicalInputSnapshot{};
    }

    void IOutputRuntimeBackendOps::OnOutputGateChanged(bool) noexcept {}

    int OutputProductionBackendOps::ReleaseAll(
        OutputRuntimeBackend backend, std::chrono::milliseconds timeout) {
        if (backend == OutputRuntimeBackend::None) return success;
        auto* device = activeDevice_.load();
        return device ? device->ReleaseAll(timeout) : err_queue_stopped;
    }
    int OutputProductionBackendOps::Shutdown(
        OutputRuntimeBackend backend, std::chrono::milliseconds timeout) {
        auto* device = activeDevice_.load();
        const int status = device ? device->Shutdown(timeout) : success;
        activeDevice_.store(nullptr);
        ReleaseTimerResolution();
        return status;
    }
    bool OutputProductionBackendOps::IsTransportReady() const noexcept {
        auto* device = activeDevice_.load();
        return device && device->ConnectionState() == OutputConnectionState::Connected;
    }
    int OutputProductionBackendOps::Initialize(const OutputRuntimeDescriptor& descriptor) {
        for (const auto& plan : PureDevicePlans()) {
            Diagnostics::Info("[OUTPUT-PLAN] key=%s stage=%s transport=%s pure_identity=%d current_un_backend=%d",
                plan.key, ToString(plan.stage), plan.transport,
                plan.hasPureIdentityBranch ? 1 : 0,
                plan.hasCurrentUnBackend ? 1 : 0);
        }
        auto* device = ResolveOutputDevice(descriptor);
        activeDevice_.store(device);
        if (descriptor.backend == OutputRuntimeBackend::None) return success;
        const int status = device ? device->Initialize(descriptor) : err_net_cmd;
        if (device) {
            const auto capabilities = device->Capabilities();
            Diagnostics::Info(
                "[OUTPUT] identity tag=%s confirmed=%d capability_source=%s move=%d buttons=%d keyboard=%d monitor=%d release=%d timed_move=%d",
                capabilities.identityTag ? capabilities.identityTag : "unknown",
                capabilities.identityConfirmed ? 1 : 0,
                capabilities.capabilitySource ? capabilities.capabilitySource : "unknown",
                capabilities.move ? 1 : 0, capabilities.buttons ? 1 : 0,
                capabilities.keyboard ? 1 : 0, capabilities.physicalMonitor ? 1 : 0,
                capabilities.release ? 1 : 0, capabilities.timedMove ? 1 : 0);
        }
        return status;
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


    int OutputRuntimeController::DispatchCleanupForGeneration(
        std::uint64_t expectedGeneration, bool pauseOutput,
        const std::function<int(const OutputRuntimeAppliedState&)>& cleanup) {
        if (!cleanup) return err_net_cmd;
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        const auto generation = generation_.load(std::memory_order_acquire);
        if (expectedGeneration == 0 || generation != expectedGeneration ||
            active_.backend == OutputRuntimeBackend::None) return err_queue_stopped;
        if (pauseOutput) SetOutputGate(false);
        return cleanup({active_, generation});
    }

    int ReconcileRuntimeFromConfig(std::chrono::milliseconds timeout) {
        return ReconcileProductionRuntime(RuntimeDescriptorFromConfig(), timeout);
    }
    OutputRuntimeAppliedState ActiveRuntimeSnapshot() { return RuntimeController().Applied(); }
    OutputConnectionState ActiveRuntimeConnectionState() {
        const auto active = RuntimeController().Applied();
        if (!RuntimeController().CanDispatch(active.generation))
            return OutputConnectionState::Disconnected;
        auto* device = ResolveOutputDevice(active.descriptor);
        return device ? device->ConnectionState() : OutputConnectionState::Disconnected;
    }
    bool IsRuntimeOutputGateOpen() noexcept { return RuntimeController().IsOutputGateOpen(); }
    bool CanDispatchRuntimeGeneration(std::uint64_t generation) noexcept {
        return RuntimeController().CanDispatch(generation);
    }
    int ReleaseActiveRuntime(std::chrono::milliseconds timeout) {
        return RuntimeController().ReleaseActive(timeout);
    }
    int ShutdownActiveRuntime(std::chrono::milliseconds timeout) {
        return ReconcileProductionRuntime(OutputRuntimeDescriptor::Disabled(), timeout);
    }

    namespace {
        template<class Action>
        int DispatchDevice(std::optional<std::uint64_t> expectedGeneration, Action&& action) {
            if (expectedGeneration && *expectedGeneration == 0) return err_queue_stopped;
            return RuntimeController().DispatchActive(
                [&](const OutputRuntimeAppliedState& active) -> int {
                    if (expectedGeneration && active.generation != *expectedGeneration)
                        return err_queue_stopped;
                    auto* device = ResolveOutputDevice(active.descriptor);
                    return device ? action(*device) : err_queue_stopped;
                });
        }

        int DispatchTrackedMask(std::uint64_t generation, std::uint32_t mask,
            std::chrono::milliseconds timeout, bool recoverOnTimeout) {
            if (generation == 0 || timeout < std::chrono::milliseconds::zero()) return err_net_cmd;
            timeout = BoundedTransportCompletionWait(timeout);
            auto completion = std::make_shared<OutputCommandCompletion>();
            IOutputDevice* submittedDevice = nullptr;
            bool pauseOnTimeout = false;
            const int queued = DispatchDevice(generation, [&](IOutputDevice& device) {
                submittedDevice = &device;
                pauseOnTimeout = device.Capabilities().maskTimeoutClosesGate;
                return device.Mask(mask, completion);
            });
            if (queued != success) return queued;
            // A device switch can proceed while this worker completion is pending.
            const int completed = WaitForTransportCompletion(completion, timeout, "mouse mask");
            if (completed != err_completion_timeout || !recoverOnTimeout) return completed;

            std::shared_ptr<OutputCommandCompletion> cleanupCompletion;
            const int cleanupStatus = RuntimeController().DispatchCleanupForGeneration(
                generation, pauseOnTimeout, [&](const OutputRuntimeAppliedState&) {
                    return submittedDevice->RecoverMaskTimeout(completion, cleanupCompletion);
                });
            if (cleanupStatus == success && cleanupCompletion) {
                const int status = WaitForTransportCompletion(cleanupCompletion, timeout, "mask cleanup");
                if (status != success) Diagnostics::Warn("[OUTPUT] Mask cleanup remains pending or failed. status=%d", status);
            } else if (cleanupStatus != success && cleanupStatus != err_queue_stopped) {
                Diagnostics::Warn("[OUTPUT] Could not queue mask cleanup. status=%d", cleanupStatus);
            }
            return err_completion_timeout;
        }
    }

    int DispatchMouseMove(int x, int y, int runtimeMs) {
        return DispatchDevice(std::nullopt, [&](IOutputDevice& device) { return device.Move(x, y, runtimeMs); });
    }
    int DispatchMouseMoveForGeneration(std::uint64_t generation, int x, int y, int runtimeMs) {
        return DispatchDevice(generation, [&](IOutputDevice& device) { return device.Move(x, y, runtimeMs); });
    }
    int DispatchMouseButton(int button, bool down) {
        if (button < 0 || button > 4) return err_net_cmd;
        return DispatchDevice(std::nullopt, [&](IOutputDevice& device) { return device.Button(button, down); });
    }
    int DispatchMouseButtonForGeneration(std::uint64_t generation, int button, bool down) {
        if (button < 0 || button > 4) return err_net_cmd;
        if (SuppressOutput(OutputIntentForState(down))) return err_output_suppressed;
        return DispatchDevice(generation, [&](IOutputDevice& device) { return device.Button(button, down); });
    }
    int DispatchForceReleaseMouseButtonForGeneration(std::uint64_t generation, int button) {
        if (button < 0 || button > 4) return err_net_cmd;
        return DispatchDevice(generation, [&](IOutputDevice& device) { return device.Button(button, false, true); });
    }
    int DispatchMouseButtonStateMask(std::uint32_t mask, bool force) {
        return DispatchDevice(std::nullopt, [&](IOutputDevice& device) { return device.ButtonState(mask & 0x1Fu, force); });
    }
    int DispatchForceReleaseMouseButtons() {
        return DispatchDevice(std::nullopt, [](IOutputDevice& device) { return device.ReleaseButtons(); });
    }
    int DispatchForceReleaseMouseButton(int button) {
        if (button < 0 || button > 4) return err_net_cmd;
        return DispatchDevice(std::nullopt, [&](IOutputDevice& device) { return device.Button(button, false, true); });
    }
    int DispatchMouseMask(std::uint32_t mask) {
        mask &= 0x7Fu;
        if (!mask) return err_net_cmd;
        return DispatchDevice(std::nullopt, [&](IOutputDevice& device) { return device.Mask(mask); });
    }
    int DispatchMouseUnmask() {
        return DispatchDevice(std::nullopt, [](IOutputDevice& device) { return device.Mask(0); });
    }
    int DispatchMouseMaskForGeneration(std::uint64_t generation, std::uint32_t mask) {
        mask &= 0x7Fu;
        if (!mask) return err_net_cmd;
        return DispatchDevice(generation, [&](IOutputDevice& device) { return device.Mask(mask); });
    }
    int DispatchMouseMaskAndWaitForGeneration(std::uint64_t generation, std::uint32_t mask,
        std::chrono::milliseconds timeout) {
        mask &= 0x7Fu;
        if (!mask) return err_net_cmd;
        return DispatchTrackedMask(generation, mask, timeout, true);
    }
    int DispatchMouseUnmaskForGeneration(std::uint64_t generation) {
        return DispatchDevice(generation, [](IOutputDevice& device) { return device.Mask(0); });
    }
    int DispatchMouseUnmaskAndWaitForGeneration(std::uint64_t generation,
        std::chrono::milliseconds timeout) {
        return DispatchTrackedMask(generation, 0, timeout, false);
    }
    int DispatchKeyboardReport(unsigned char modifiers, const std::vector<unsigned char>& usages) {
        if (!ValidateKeyboardReport(usages)) return err_net_cmd;
        const auto intent = modifiers == 0 && usages.empty() ? OutputIntent::SafetyRelease : OutputIntent::Normal;
        if (SuppressOutput(intent)) return err_output_suppressed;
        return DispatchDevice(std::nullopt, [&](IOutputDevice& device) { return device.Keyboard(modifiers, usages, intent); });
    }
    int DispatchKeyboardReportForGeneration(std::uint64_t generation, unsigned char modifiers,
        const std::vector<unsigned char>& usages, OutputIntent intent) {
        if (!ValidateKeyboardReport(usages)) return err_net_cmd;
        if (SuppressOutput(intent)) return err_output_suppressed;
        return DispatchDevice(generation, [&](IOutputDevice& device) { return device.Keyboard(modifiers, usages, intent); });
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
