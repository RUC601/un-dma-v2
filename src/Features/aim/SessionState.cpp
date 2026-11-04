#include "BehaviorController.hpp"
#include "Utils/Diagnostics.hpp"
using namespace OW;
namespace AimbotDetail {
    void LogMagneticTriggerQuantizationReset(
        const char* reason,
        bool hadState) {
        static DWORD lastLogTick = 0;
        const DWORD now = GetTickCount();
        if (!OW::Config::aimVerboseLog &&
            lastLogTick != 0 &&
            now - lastLogTick < 250) {
            return;
        }
        Diagnostics::Aim("magnetic_trigger.quantization state_reset=1 state_reset_reason=%s had_state=%d",
            reason ? reason : "unknown",
            hadState ? 1 : 0);
        lastLogTick = now;
    }

    void ResetMagneticTriggerQuantization(
        RuntimeState& state,
        const char* reason) {
        const bool hadState = OW::HasMouseMoveQuantizationState(
            state.magneticMoveQuantization);
        OW::ResetMouseMoveQuantizationState(state.magneticMoveQuantization);
        if (hadState)
            LogMagneticTriggerQuantizationReset(reason, true);
    }

    void ResetTrackingSession(RuntimeState& state) {
        // Motion cleanup must not rearm aimHoldWindows; only physical release does.
        ResetMagneticTriggerQuantization(state, "tracking_session_reset");
        state.trackingSessionActive = false;
        state.trackingSessionHeroId = 0;
        state.trackingSessionSlotIndex = -1;
        state.trackingSessionSlotId = 0;
        state.trackingSessionAimKey = 0;
        state.trackingTargetKey = 0;
        state.trackingTargetActorKey = 0;
        state.trackingTargetMissingSinceTick = 0;
        state.trackingInDeadzone = false;
        OW::ResetAimStartLimiter(state.trackingStartLimiter);
    }

    void PauseOrdinaryFlickMovement(RuntimeState& state) {
        const bool wasMoving = state.ordinaryFlick.moving;
        FlickRepeat::PauseMovement(state.ordinaryFlick);
        if (wasMoving) OW::ResetAimSmoothingState();
    }

    void EndOrdinaryFlickActivation(RuntimeState& state) {
        if (state.ordinaryFlick.moving) PauseOrdinaryFlickMovement(state);
        FlickRepeat::EndActivation(state.ordinaryFlick);
    }

    FlickRepeat::Phase OrdinaryFlickPhase(const RuntimeState& state,
                                                DWORD now) {
        return FlickRepeat::Resolve(state.ordinaryFlick.timeline, now,
            OW::Config::View_aimbotKeepFiring(), OW::Config::flickRepeatWindowMs,
            OW::Config::ClampFlickPostFireDelayMs(OW::Config::View_aimbotFlickPostFireDelayMs()),
            state.ordinaryFlick.shotLatched);
    }
}
