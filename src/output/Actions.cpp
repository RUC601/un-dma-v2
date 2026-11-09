#include "Actions.hpp"
#include "Game/HeroSettings.hpp"
#include "Game/snapshots/SnapshotAccess.hpp"
#include "Utils/Diagnostics.hpp"
#include <atomic>
#include <cstdio>
#include <chrono>
#include <thread>
namespace OW {
    bool DmaKeyToMouseButton(uint32_t key, int& button) {
        switch (key) {
        case 0x1: button = 0; return true;
        case 0x2: button = 1; return true;
        case 0x4: button = 2; return true;
        default: return false;
        }
    }

    int ClampOutputAutomoveRuntimeMs(int moveTimeMs) {
        return std::clamp(moveTimeMs, 0, 20);
    }

    const char* OutputTransportName() {
        switch (V2::Output::RuntimeController().Applied().descriptor.backend) {
        case V2::Output::OutputRuntimeBackend::Network: return "network";
        case V2::Output::OutputRuntimeBackend::Serial:  return "serial";
        case V2::Output::OutputRuntimeBackend::CatBox:  return "catbox";
        case V2::Output::OutputRuntimeBackend::Mock:    return "mock";
        case V2::Output::OutputRuntimeBackend::None:    return "none";
        default:                                  return "unknown";
        }
    }

    bool ShouldSuppressOutput(
        const char* action,
        OutputIntent intent) {
        if(intent==OutputIntent::Normal && V2::HostInteraction::paused.load(std::memory_order_acquire))return true;
        if (intent == OutputIntent::Normal &&
            CalibrationBlocksNormalOutput(Config::calibrationInProgress.load(std::memory_order_acquire)))
            return true;
        if (intent == OutputIntent::Normal &&
            !RuntimeOutputSessionEnabled()) {
            static std::atomic_flag sessionLogged = ATOMIC_FLAG_INIT;
            if (!sessionLogged.test_and_set(std::memory_order_relaxed)) {
                Diagnostics::Aim(
                    "output.output suppressed action=%s reason=process_session_closed intent=%s",
                    action ? action : "unknown",
                    ToString(intent));
            }
            return true;
        }
        if (!ShouldSuppressOutputForMenu(Config::OutputSuppressedByMenu(), intent))
            return false;

        static std::atomic_flag logged = ATOMIC_FLAG_INIT;
        if (!logged.test_and_set(std::memory_order_relaxed)) {
            Diagnostics::Aim("output.output suppressed action=%s reason=menu_open intent=%s",
                action ? action : "unknown",
                ToString(intent));
        }
        return true;
    }

    int EnqueueOutputPixelMoveForGeneration(
        int pixelX,
        int pixelY,
        int automoveRuntimeMs,
        std::uint64_t expectedGeneration,
        std::uint64_t expectedConnectionEpoch) {
        if (ShouldSuppressOutput("mouse_move"))
            return Config::kOutputSuppressedStatus;

        const OutputRuntimeState runtime = CurrentOutputRuntimeState();
        if (!runtime.outputGateOpen || expectedGeneration == 0 ||
            runtime.backendGeneration != expectedGeneration ||
            (expectedConnectionEpoch != 0 &&
             (!ProcessConnection::IsConnected() ||
              ProcessConnection::ConnectionEpoch() != expectedConnectionEpoch))) {
            return err_queue_stopped;
        }
        if (!V2::Output::SupportsTimedMove(V2::Output::ActiveRuntimeSnapshot().descriptor)) automoveRuntimeMs=0;
        return V2::Output::DispatchMouseMoveForGeneration(
            expectedGeneration,
            pixelX,
            pixelY,
            automoveRuntimeMs);
    }

    int EnqueueOutputPixelMove(
        int pixelX,
        int pixelY,
        int automoveRuntimeMs) {
        const OutputRuntimeState runtime = CurrentOutputRuntimeState();
        return EnqueueOutputPixelMoveForGeneration(
            pixelX,
            pixelY,
            automoveRuntimeMs,
            runtime.backendGeneration);
    }

    MouseMoveQuantizationState& DefaultMouseMoveQuantizationState() {
        thread_local MouseMoveQuantizationState state{};
        return state;
    }

    MouseMoveQuantizationResult SendMouseMove(
        const Vector3& delta,
        int moveTimeMs,
        const MouseMoveQuantizationOptions& quantization) {
        MouseMoveQuantizationResult result{};
        const std::uint64_t connectionEpoch =
            ProcessConnection::ConnectionEpoch();
        const OutputRuntimeState outputRuntime =
            CurrentOutputRuntimeState();
        if (connectionEpoch == 0 || !ProcessConnection::IsConnected() ||
            !outputRuntime.outputGateOpen ||
            outputRuntime.backendGeneration == 0) {
            Diagnostics::Aim(
                "mouse.move early_return reason=stale_or_unavailable_output_session connectionEpoch=%llu generation=%llu gate=%d",
                static_cast<unsigned long long>(connectionEpoch),
                static_cast<unsigned long long>(outputRuntime.backendGeneration),
                outputRuntime.outputGateOpen ? 1 : 0);
            return result;
        }
        if (moveTimeMs < 0) moveTimeMs = Config::outputInputDelayMs;
        const int automoveRuntimeMs = ClampOutputAutomoveRuntimeMs(moveTimeMs);
        Diagnostics::Aim("mouse.move request delta_rad=(%.9f,%.9f,%.9f) moveTimeMs=%d automoveRuntimeMs=%d enabled=%d deviceType=%d",
            delta.X,
            delta.Y,
            delta.Z,
            moveTimeMs,
            automoveRuntimeMs,
            Config::outputEnabled ? 1 : 0,
            Config::outputDeviceType);
        const V2::Output::OutputRuntimeAppliedState applied = V2::Output::RuntimeController().Applied();
        if (applied.descriptor.backend == V2::Output::OutputRuntimeBackend::None ||
            !V2::Output::RuntimeController().CanDispatch(applied.generation)) {
            static std::atomic_flag once = ATOMIC_FLAG_INIT;
            if (!once.test_and_set(std::memory_order_relaxed)) {
                std::printf("[OUTPUT] SendMouseMove called without an active Output runtime!\n");
            }
            Diagnostics::Aim("mouse.move early_return reason=no_active_runtime lastError=%d",
                V2::Output::RuntimeController().LastError());
            return result;
        }
        if (ShouldSuppressOutput("mouse_move")) {
            Diagnostics::Aim("mouse.move early_return reason=menu_open_suppressed");
            return result;
        }

        {
            const float baseCountsPerRadian = Config::OutputBaseCountsPerRadian();
            const float syncScale = Config::OutputGameSensitivityScale();
            float sensitivity = Config::OutputYawCountsPerRadian();
            float pitchSensitivity = Config::OutputPitchCountsPerRadian();

            if (!std::isfinite(sensitivity) || sensitivity <= 0.0f) {
                Diagnostics::Aim("mouse.convert warning invalid_counts_per_rad computed=%.9f fallback=%.9f autoScale=%d gameSens=%.9f effectiveGameSens=%.9f refSens=%.9f pitchCountsPerRad=%.9f",
                    sensitivity,
                    baseCountsPerRadian,
                    Config::autoScaleByGameSensitivity ? 1 : 0,
                    Config::gameMouseSensitivity,
                    Config::EffectiveGameMouseSensitivity(),
                    Config::referenceGameSensitivity,
                    pitchSensitivity);
                sensitivity = baseCountsPerRadian;
                pitchSensitivity = sensitivity;
            }

            static std::atomic<int> calls{0};
            MouseMoveQuantizationState& quantizationState = quantization.state
                ? *quantization.state
                : DefaultMouseMoveQuantizationState();

            // delta.X = pitch (vertical), delta.Y = yaw (horizontal).
            // Positive KMBox X drives the measured yaw negative, so yaw correction is inverted here.
            const float scaledYaw   = -delta.Y * sensitivity;         // yaw -> horizontal X
            const float pitchScale = std::clamp(Config::View_aimbotPitchScale(), 0.1f, 3.0f);
            const float scaledPitch = delta.X * pitchSensitivity * pitchScale; // pitch -> vertical Y
            const float fallbackScaledYaw =
                -quantization.fallbackDirectionDelta.Y * sensitivity;
            const float fallbackScaledPitch =
                quantization.fallbackDirectionDelta.X * pitchSensitivity * pitchScale;
            const float accumBeforeX = quantizationState.residualX;
            const float accumBeforeY = quantizationState.residualY;
            result = QuantizeMouseMoveCounts(
                scaledYaw,
                scaledPitch,
                fallbackScaledYaw,
                fallbackScaledPitch,
                quantizationState,
                quantization.forceNonZero);
            const int pixelX = result.pixelX;
            const int pixelY = result.pixelY;

            const int callCount = calls.fetch_add(1, std::memory_order_relaxed) + 1;
            Diagnostics::Aim("mouse.convert call=%d delta_rad_pitch=%.9f delta_rad_yaw=%.9f baseCountsPerRad=%.6f yawCountsPerRad=%.6f pitchCountsPerRad=%.6f autoScale=%d syncScale=%.6f pitchScale=%.6f scaled_counts=(yaw=%.9f,pitch=%.9f) fallback_counts=(yaw=%.9f,pitch=%.9f) accum_before=(%.9f,%.9f) counts=(%d,%d) accum_after=(%.9f,%.9f) forced_min_step=%d",
                callCount,
                delta.X,
                delta.Y,
                baseCountsPerRadian,
                sensitivity,
                pitchSensitivity,
                Config::autoScaleByGameSensitivity ? 1 : 0,
                syncScale,
                pitchScale,
                scaledYaw,
                scaledPitch,
                fallbackScaledYaw,
                fallbackScaledPitch,
                accumBeforeX,
                accumBeforeY,
                pixelX,
                pixelY,
                quantizationState.residualX,
                quantizationState.residualY,
                result.forcedMinimumStep ? 1 : 0);
            if (Config::aimVerboseLog &&
                (callCount <= 50 || pixelX != 0 || pixelY != 0)) {
                std::printf("[OUTPUT] #%d pitch=%.6f yaw=%.6f yawCountsPerRad=%.1f pitchCountsPerRad=%.1f counts=(%d,%d) accum=(%.3f,%.3f)\n",
                    callCount, delta.X, delta.Y, sensitivity, pitchSensitivity,
                    pixelX, pixelY,
                    quantizationState.residualX,
                    quantizationState.residualY);
            }

            if (pixelX == 0 && pixelY == 0) {
                Diagnostics::Aim("mouse.move early_return reason=zero_counts scaled=(yaw_%.9f,pitch_%.9f) accum_after=(%.9f,%.9f) forced_min_step=%d note=%s",
                    scaledYaw,
                    scaledPitch,
                    quantizationState.residualX,
                    quantizationState.residualY,
                    result.forcedMinimumStep ? 1 : 0,
                    quantization.forceNonZero
                        ? "magnetic_no_valid_direction"
                        : "integer_truncation_waiting_for_accumulator");
                return result;
            }

            if (result.forcedMinimumStep) {
                static std::atomic<DWORD> lastForcedMinimumStepLogTick{0};
                const DWORD now = GetTickCount();
                if (Config::aimVerboseLog ||
                    lastForcedMinimumStepLogTick == 0 ||
                    now - lastForcedMinimumStepLogTick >= 250) {
                    Diagnostics::Aim("mouse.move magnetic_quantize forced_min_step=1 counts=(%d,%d) raw_delta=(%.9f,%.9f) residual=(%.9f,%.9f)",
                        pixelX,
                        pixelY,
                        quantization.fallbackDirectionDelta.X,
                        quantization.fallbackDirectionDelta.Y,
                        quantizationState.residualX,
                        quantizationState.residualY);
                    lastForcedMinimumStepLogTick = now;
                }
            }

            // ---- Micro-split mouse movement ----
            const int splitBehavior = Config::ClampAimBehaviorIndex(Config::View_aimBehavior());
            const bool splitConfigured = Config::AimBehaviorMoveSplitEnabled(splitBehavior);
            // NET commands are already paced and coalesced by the transport queue. Splitting
            // here only blocks the aim thread and the queue then joins the chunks again.
            // Serial keeps caller-side micro steps because they are emitted independently.
            const bool splitEnabled = splitConfigured &&
                applied.descriptor.backend != V2::Output::OutputRuntimeBackend::Network;
            const int maxPixels = Config::AimBehaviorMoveSplitMaxPixels(splitBehavior);
            const int delayUs = Config::AimBehaviorMoveSplitDelayUs(splitBehavior);
            const int absX = std::abs(pixelX);
            const int absY = std::abs(pixelY);
            const int maxComponent = (std::max)(absX, absY);
            const int steps = splitEnabled
                ? (std::max)(1, (maxComponent + maxPixels - 1) / maxPixels)
                : 1;

            if (steps <= 1) {
                const int status = EnqueueOutputPixelMoveForGeneration(
                    pixelX,
                    pixelY,
                    automoveRuntimeMs,
                    outputRuntime.backendGeneration,
                    connectionEpoch);
                result.status = status;
                result.dispatched = status == success;
                Diagnostics::Aim("mouse.enqueue transport=%s command=%s pixel=(%d,%d) runtimeMs=%d behavior=%d splitConfigured=%d splitApplied=%d status=%d",
                    OutputTransportName(),
                    automoveRuntimeMs > 0 ? "automove" : "move",
                    pixelX,
                    pixelY,
                    automoveRuntimeMs,
                    splitBehavior,
                    splitConfigured ? 1 : 0,
                    splitEnabled ? 1 : 0,
                    status);
                OutputMotionTelemetry::RecordMouseMove(
                    delta,
                    pixelX,
                    pixelY,
                    automoveRuntimeMs,
                    status,
                    false,
                    1);
            } else {
                // Split into micro-movements
                Diagnostics::Aim("mouse.move.split total_pixels=(%d,%d) steps=%d max_pixels_per_step=%d delay_us=%d runtimeMs=%d command=%s behavior=%d",
                    pixelX,
                    pixelY,
                    steps,
                    maxPixels,
                    delayUs,
                    automoveRuntimeMs,
                    automoveRuntimeMs > 0 ? "automove" : "move",
                    splitBehavior);
                int remainingX = pixelX;
                int remainingY = pixelY;
                int lastStatus = 0;
                int completedSteps = 0;
                for (int i = 0; i < steps; i++) {
                    const int curX = remainingX / (steps - i);
                    const int curY = remainingY / (steps - i);
                    remainingX -= curX;
                    remainingY -= curY;

                    const int status = EnqueueOutputPixelMoveForGeneration(
                        curX,
                        curY,
                        automoveRuntimeMs,
                        outputRuntime.backendGeneration,
                        connectionEpoch);
                    lastStatus = status;
                    Diagnostics::Aim("mouse.enqueue.split transport=%s command=%s step=%d/%d cur=(%d,%d) runtimeMs=%d status=%d",
                        OutputTransportName(),
                        automoveRuntimeMs > 0 ? "automove" : "move",
                        i + 1,
                        steps,
                        curX,
                        curY,
                        automoveRuntimeMs,
                        status);

                    if (status != success) {
                        Diagnostics::Aim(
                            "mouse.move.split_abort step=%d/%d status=%d expectedGeneration=%llu connectionEpoch=%llu",
                            i + 1,
                            steps,
                            status,
                            static_cast<unsigned long long>(
                                outputRuntime.backendGeneration),
                            static_cast<unsigned long long>(connectionEpoch));
                        break;
                    }
                    ++completedSteps;

                    if (i < steps - 1 && delayUs > 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(delayUs));
                    }
                }
                Diagnostics::Aim("mouse.move.split_complete steps=%d total=(%d,%d)",
                    completedSteps, pixelX, pixelY);
                OutputMotionTelemetry::RecordMouseMove(
                    delta,
                    pixelX,
                    pixelY,
                    automoveRuntimeMs,
                    lastStatus,
                    true,
                    completedSteps);
                result.status = lastStatus;
                result.dispatched = completedSteps > 0;
            }
            return result;
        }
    }

    void SendMouseMove(const Vector3& delta, int moveTimeMs) {
        MouseMoveQuantizationOptions quantization{};
        quantization.state = &DefaultMouseMoveQuantizationState();
        (void)SendMouseMove(delta, moveTimeMs, quantization);
    }

    void SendMouseMove(float deltaX, float deltaY, int moveTimeMs) {
        SendMouseMove(Vector3(deltaX, deltaY, 0.0f), moveTimeMs);
    }

    bool ActionOutputSucceeded(ActionOutputStatus status) {
        return status == ActionOutputStatus::Sent;
    }

    ActionOutputStatus UnavailableActionOutputStatus() {
        return V2::Output::RuntimeController().Desired().backend == V2::Output::OutputRuntimeBackend::None
            ? ActionOutputStatus::Disabled
            : ActionOutputStatus::TransportError;
    }

    std::string DefaultMouseButtonOwnerKey(OutputOwnerSource ownerSource, int button) {
        return "target.mouse." +
            std::to_string(static_cast<unsigned int>(ownerSource)) + "." +
            std::to_string(button);
    }

    const char* GameActionOwnerKey(GameAction action) {
        switch (action) {
        case GameAction::PrimaryFire:   return "game_action.primary_fire";
        case GameAction::SecondaryFire: return "game_action.secondary_fire";
        case GameAction::MiddleMouse:   return "game_action.middle_mouse";
        case GameAction::Ability1:      return "game_action.ability1";
        case GameAction::Ability2:      return "game_action.ability2";
        case GameAction::Ultimate:      return "game_action.ultimate";
        case GameAction::Melee:         return "game_action.melee";
        case GameAction::MoveForward:   return "game_action.move_forward";
        default:                        return nullptr;
        }
    }

    ActionOutputStatus SetOwnedOutputControl(
        const std::string& ownerKey,
        OutputOwnerSource ownerSource,
        OutputControl control,
        bool down,
        const char* outputName,
        bool requireKeyboardOutput,
        OutputUnownedControlCallback ownerlessRelease,
        std::uint64_t expectedGeneration,
        bool requireNewPress) {
        if(control.kind==OutputControlKind::MouseButton&&control.code>2)return ActionOutputStatus::UnsupportedTransport;
        const V2::Output::OutputRuntimeAppliedState applied = V2::Output::RuntimeController().Applied();
        if (applied.descriptor.backend == V2::Output::OutputRuntimeBackend::None ||
            !V2::Output::RuntimeController().CanDispatch(applied.generation) ||
            (expectedGeneration != 0 &&
             applied.generation != expectedGeneration)) {
            return UnavailableActionOutputStatus();
        }
        const OutputIntent intent = OutputIntentForState(down);
        if (ShouldSuppressOutput(outputName, intent))
            return ActionOutputStatus::Suppressed;
        if (requireKeyboardOutput &&
            !V2::Output::SupportsKeyboardOutput(applied.descriptor)) {
            return ActionOutputStatus::UnsupportedTransport;
        }

        OutputScheduler& scheduler = RuntimeOutputScheduler();
        if (expectedGeneration != 0 && down &&
            !scheduler.SynchronizeRuntime(
                { expectedGeneration, true })) {
            return ActionOutputStatus::TransportError;
        }
        bool accepted = false;
        if (expectedGeneration != 0) {
            accepted = !down && ownerlessRelease
                ? scheduler.ReleaseManualControlOrExecuteIfUnownedForGeneration(
                    ownerKey,
                    ownerSource,
                    control,
                    expectedGeneration,
                    std::move(ownerlessRelease))
                : scheduler.SetManualControlsForGeneration(
                    ownerKey,
                    ownerSource,
                    down
                        ? std::vector<OutputControl>{ control }
                        : std::vector<OutputControl>{},
                    expectedGeneration,
                    requireNewPress && down);
        } else {
            accepted = !down && ownerlessRelease
                ? scheduler.ReleaseManualControlOrExecuteIfUnowned(
                    ownerKey,
                    ownerSource,
                    control,
                    std::move(ownerlessRelease))
                : scheduler.SetManualControl(
                    ownerKey,
                    ownerSource,
                    control,
                    down);
        }
        return accepted
            ? ActionOutputStatus::Sent
            : ActionOutputStatus::TransportError;
    }

    ActionOutputStatus SendMouseButtonActionState(
        int button,
        bool down,
        OutputOwnerSource ownerSource,
        const char* ownerKey,
        std::uint64_t expectedGeneration,
        bool requireNewPress) {
        if (button < 0 || button > 2)
            return ActionOutputStatus::InvalidAction;

        if (Config::outputDebugLog) {
            std::printf("[OUTPUT] mouse.button button=%d down=%d transport=%s generation=%llu\n",
                button,
                down ? 1 : 0,
                OutputTransportName(),
                static_cast<unsigned long long>(
                    V2::Output::RuntimeController().Applied().generation));
        }

        const std::string resolvedOwnerKey = ownerKey && *ownerKey
            ? std::string(ownerKey)
            : DefaultMouseButtonOwnerKey(ownerSource, button);
        const auto action=button==0?V2::HeroSettings::Action::Primary:button==1?V2::HeroSettings::Action::Secondary:V2::HeroSettings::Action::Middle;
        const int vk=V2::HeroSettings::Key(SnapshotLocalEntity().HeroID,action);
        const int mappedButton=vk==1?0:vk==2?1:vk==4?2:vk==5?3:vk==6?4:-1;
        const auto hid=V2::HeroSettings::HidUsage(vk);
        if(mappedButton<0&&!hid)return ActionOutputStatus::InvalidAction;
        const auto generation=expectedGeneration?expectedGeneration:V2::Output::ActiveRuntimeSnapshot().generation;
        return SetOwnedOutputControl(resolvedOwnerKey,ownerSource,
            {mappedButton>=0?OutputControlKind::MouseButton:hid>=0xE0?OutputControlKind::KeyboardModifier:OutputControlKind::KeyboardUsage,
             static_cast<std::uint16_t>(mappedButton>=0?mappedButton:hid)},
            down,"hero_action",mappedButton<0,{},generation,requireNewPress);
    }

    ActionOutputStatus ReleaseMouseButtonActionStateOrPhysicalFallback(
        int button,
        OutputOwnerSource ownerSource,
        const char* ownerKey,
        std::uint64_t expectedGeneration,
        bool* physicalFallbackReleased) {
        if (button < 0 || button > 2)
            return ActionOutputStatus::InvalidAction;
        if (physicalFallbackReleased)
            *physicalFallbackReleased = false;

        const std::string resolvedOwnerKey = ownerKey && *ownerKey
            ? std::string(ownerKey)
            : DefaultMouseButtonOwnerKey(ownerSource, button);
        const V2::Output::OutputRuntimeAppliedState applied =
            V2::Output::RuntimeController().Applied();
        if (expectedGeneration == 0 ||
            applied.generation != expectedGeneration ||
            applied.descriptor.backend == V2::Output::OutputRuntimeBackend::None ||
            !V2::Output::RuntimeController().CanDispatch(applied.generation)) {
            return UnavailableActionOutputStatus();
        }
        if (ShouldSuppressOutput(
                "mouse_button",
                OutputIntent::SafetyRelease)) {
            return ActionOutputStatus::Suppressed;
        }

        const int vk=V2::HeroSettings::Key(SnapshotLocalEntity().HeroID,button==0?V2::HeroSettings::Action::Primary:button==1?V2::HeroSettings::Action::Secondary:V2::HeroSettings::Action::Middle);
        const int mappedButton=vk==1?0:vk==2?1:vk==4?2:vk==5?3:vk==6?4:-1;
        const auto hid=V2::HeroSettings::HidUsage(vk);
        if(mappedButton<0&&!hid)return ActionOutputStatus::InvalidAction;
        const OutputControl mappedControl{mappedButton>=0?OutputControlKind::MouseButton:hid>=0xE0?OutputControlKind::KeyboardModifier:OutputControlKind::KeyboardUsage,static_cast<std::uint16_t>(mappedButton>=0?mappedButton:hid)};
        const bool accepted = RuntimeOutputScheduler()
            .ReleaseManualControlOrExecuteIfUnownedForGeneration(
            resolvedOwnerKey,
            ownerSource,
            mappedControl,
            expectedGeneration,
            [mappedButton, physicalFallbackReleased](
                std::uint64_t backendGeneration) {
                // Transport has no isolated physical-key release. Never emit an all-key-up report.
                if(mappedButton<0)return false;
                const bool released =
                    V2::Output::DispatchForceReleaseMouseButtonForGeneration(
                        backendGeneration,
                        mappedButton) == success;
                if (physicalFallbackReleased)
                    *physicalFallbackReleased = released;
                return released;
            });
        return accepted
            ? ActionOutputStatus::Sent
            : ActionOutputStatus::TransportError;
    }

    ActionOutputStatus ReleaseMouseButtonActionStateForGeneration(
        int button,
        OutputOwnerSource ownerSource,
        const char* ownerKey,
        std::uint64_t expectedGeneration) {
        if (button < 0 || button > 2)
            return ActionOutputStatus::InvalidAction;

        const V2::Output::OutputRuntimeAppliedState applied =
            V2::Output::RuntimeController().Applied();
        if (expectedGeneration == 0 ||
            applied.generation != expectedGeneration ||
            applied.descriptor.backend == V2::Output::OutputRuntimeBackend::None ||
            !V2::Output::RuntimeController().CanDispatch(applied.generation)) {
            return UnavailableActionOutputStatus();
        }
        if (ShouldSuppressOutput(
                "mouse_button",
                OutputIntent::SafetyRelease)) {
            return ActionOutputStatus::Suppressed;
        }

        const std::string resolvedOwnerKey = ownerKey && *ownerKey
            ? std::string(ownerKey)
            : DefaultMouseButtonOwnerKey(ownerSource, button);
        const int vk=V2::HeroSettings::Key(SnapshotLocalEntity().HeroID,button==0?V2::HeroSettings::Action::Primary:button==1?V2::HeroSettings::Action::Secondary:V2::HeroSettings::Action::Middle);
        const int mappedButton=vk==1?0:vk==2?1:vk==4?2:vk==5?3:vk==6?4:-1;
        const auto hid=V2::HeroSettings::HidUsage(vk);
        if(mappedButton<0&&!hid)return ActionOutputStatus::InvalidAction;
        const OutputControl mappedControl{mappedButton>=0?OutputControlKind::MouseButton:hid>=0xE0?OutputControlKind::KeyboardModifier:OutputControlKind::KeyboardUsage,static_cast<std::uint16_t>(mappedButton>=0?mappedButton:hid)};
        const bool accepted = RuntimeOutputScheduler()
            .ReleaseManualControlOrExecuteIfUnownedForGeneration(
            resolvedOwnerKey,
            ownerSource,
            mappedControl,
            expectedGeneration,
            [](std::uint64_t) { return true; });
        return accepted
            ? ActionOutputStatus::Sent
            : ActionOutputStatus::TransportError;
    }

    void SendMouseButton(
        int button,
        bool down,
        OutputOwnerSource ownerSource,
        const char* ownerKey) {
        (void)SendMouseButtonActionState(button, down, ownerSource, ownerKey);
    }

    static int MappedActionVk(GameAction action) {
        using A=V2::HeroSettings::Action;
        A mapped;
        switch(action) {
        case GameAction::PrimaryFire:mapped=A::Primary;break;
        case GameAction::SecondaryFire:mapped=A::Secondary;break;
        case GameAction::MiddleMouse:mapped=A::Middle;break;
        case GameAction::Ability1:mapped=A::Ability1;break;
        case GameAction::Ability2:mapped=A::Ability2;break;
        case GameAction::Ultimate:mapped=A::Ultimate;break;
        case GameAction::Melee:mapped=A::Melee;break;
        case GameAction::MoveForward:mapped=A::Forward;break;
        default:return 0;
        }
        return V2::HeroSettings::Key(SnapshotLocalEntity().HeroID,mapped);
    }
    unsigned char GameActionKeyboardHid(GameAction action) {return V2::HeroSettings::HidUsage(MappedActionVk(action));}

    ActionOutputStatus SetActionStateForGeneration(
        GameAction action,
        bool down,
        std::uint64_t expectedGeneration) {
        const char* ownerKey = GameActionOwnerKey(action);
        if (!ownerKey)
            return ActionOutputStatus::InvalidAction;

        const int vk=MappedActionVk(action);
        int button=-1;
        if(vk==VK_LBUTTON)button=0;else if(vk==VK_RBUTTON)button=1;else if(vk==VK_MBUTTON)button=2;
        else if(vk==VK_XBUTTON1)button=3;else if(vk==VK_XBUTTON2)button=4;
        const unsigned char hid=V2::HeroSettings::HidUsage(vk);
        if(button<0&&!hid)return ActionOutputStatus::InvalidAction;
        const auto generation=expectedGeneration?expectedGeneration:V2::Output::ActiveRuntimeSnapshot().generation;
        return SetOwnedOutputControl(ownerKey,OutputOwnerSource::GameAction,
            {button>=0?OutputControlKind::MouseButton:hid>=0xE0?OutputControlKind::KeyboardModifier:OutputControlKind::KeyboardUsage,
             static_cast<std::uint16_t>(button>=0?button:hid)},down,button>=0?"mouse_button":"keyboard",button<0,{},generation);
    }

    ActionOutputStatus SetActionState(GameAction action, bool down) {
        return SetActionStateForGeneration(action, down, 0);
    }

    ActionOutputStatus PulseAction(GameAction action, DWORD durationMs) {
        const std::uint64_t operationGeneration =
            V2::Output::ActiveRuntimeSnapshot().generation;
        const ActionOutputStatus pressStatus = SetActionStateForGeneration(
            action,
            true,
            operationGeneration);
        if (!ActionOutputSucceeded(pressStatus))
            return pressStatus;

        Sleep(durationMs);
        return SetActionStateForGeneration(
            action,
            false,
            operationGeneration);
    }

    void ForceReleaseMouseButtons() {
        if (ShouldSuppressOutput(
                "force_release_buttons", OutputIntent::SafetyRelease))
            return;
        (void)V2::Output::DispatchForceReleaseMouseButtons();
    }

    void ForceReleaseMouseButton(int button) {
        if (ShouldSuppressOutput(
                "force_release_button", OutputIntent::SafetyRelease))
            return;
        (void)V2::Output::DispatchForceReleaseMouseButton(button);
    }

    bool SendMouseButtonStateMask(uint32_t stateMask, bool force) {
        if (ShouldSuppressOutput("mouse_button_state"))
            return false;
        return V2::Output::DispatchMouseButtonStateMask(stateMask & 0x7u, force) == success;
    }

    bool MaskPhysicalMouseButtons(uint32_t mask) {
        if (ShouldSuppressOutput("mouse_mask"))
            return false;
        return V2::Output::DispatchMouseMask(mask & 0x7Fu) == success;
    }

    bool UnmaskPhysicalMouseButtons() {
        if (ShouldSuppressOutput(
                "mouse_unmask", OutputIntent::SafetyRelease))
            return false;
        return V2::Output::DispatchMouseUnmask() == success;
    }
}
