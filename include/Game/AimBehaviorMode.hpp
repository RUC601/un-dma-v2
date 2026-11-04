#pragma once
#include "Game/AimPlan.hpp"
#include "Game/AimStartLimiter.hpp"
namespace OW::Config {
struct AimBehaviorTuning {
        float smooth = 20.0f;
        bool autoshot = false;
        bool keepFiring = false;
        int firePolicy = 0;
        float pitchScale = 1.0f;
        float maxAimTime = 100.0f;
        float minCharge = 5.0f;
        float maxCharge = 100.0f;
        float trackingDeadzone = 0.0f;
        float flickShotClampMs = 0.0f;
        float flickPostFireDelayMs = 0.0f;
        float magneticShotIntervalMs = 0.0f;
        float magneticPostFirePauseMs = 35.0f;
        float magneticRecoveryMs = 90.0f;
        float magneticPostFireYawScale = 0.35f;
        float magneticPostFirePitchScale = 0.0f;
        float magneticPreFireBoostWindowMs = 45.0f;
        float magneticPreFireBoostScale = 1.35f;
        bool flickTrajectoryWait = false;
        float flickTrajectoryWaitMs = 120.0f;
        float flickTrajectoryApexWindowMs = 60.0f;
        bool flick2ndTriggerGate = true;
        float flick2ndBoxPadding = 8.0f;
        float flick2ndInnerRadius = 34.0f;
        float flick2ndInnerSmoothScale = 0.55f;
        int flick2ndInnerMethod = 2;
    float baseSpeed = 100;
    AimStartLimiterProfile startLimiter{};
    bool moveSplitEnabled = true;
    int moveSplitMaxPixels = 4;
    int moveSplitDelayUs = 800;
    bool releaseToFire = false;
    bool autoCharge = false;
    float chargeFallbackMs = 720;
    int releaseExecution = 1; // 0=aim while held, 1=aim once after auxiliary-key release
    float releaseAimTimeoutMs = 1000;
    bool operator==(const AimBehaviorTuning&) const = default;
};
struct BehaviorValues {
    int behavior = 0;
    AimBehaviorTuning tuning{};
    bool operator==(const BehaviorValues&) const = default;
};
struct SlotBehaviorConfig {
    int presetId = -1;
    std::vector<BehaviorValues> behaviors;
    std::vector<AimPlan::Motion> motions;
    bool operator==(const SlotBehaviorConfig&) const = default;
};
struct AimModeRuntime {
    bool enabled = false;
    AimBehaviorTuning tuning{};
    AimPlan::Motion motion{};
    std::vector<SlotBehaviorConfig> configs;
    bool operator==(const AimModeRuntime&) const = default;
};
inline void NormalizeBehaviorTuning(AimBehaviorTuning& t) {
    using AimPlan::Finite;
    t.smooth=Finite(t.smooth,20,0,100); t.baseSpeed=Finite(t.baseSpeed,100,0,100);
    t.firePolicy=(std::clamp)(t.firePolicy,0,5);
    t.pitchScale=Finite(t.pitchScale,1,.1f,3); t.maxAimTime=Finite(t.maxAimTime,100,0,100);
    t.minCharge=Finite(t.minCharge,5,0,100); t.maxCharge=Finite(t.maxCharge,100,t.minCharge,100);
    t.chargeFallbackMs=Finite(t.chargeFallbackMs,720,0,10000);
    t.releaseExecution=(std::clamp)(t.releaseExecution,0,1);
    t.releaseAimTimeoutMs=Finite(t.releaseAimTimeoutMs,1000,50,5000);
    t.trackingDeadzone=Finite(t.trackingDeadzone,0,0,250);
    t.flickShotClampMs=Finite(t.flickShotClampMs,0,0,1000);
    t.flickPostFireDelayMs=Finite(t.flickPostFireDelayMs,0,0,500);
    t.magneticShotIntervalMs=Finite(t.magneticShotIntervalMs,0,0,5000);
    t.magneticPostFirePauseMs=Finite(t.magneticPostFirePauseMs,35,0,500);
    t.magneticRecoveryMs=Finite(t.magneticRecoveryMs,90,0,1000);
    t.magneticPostFireYawScale=Finite(t.magneticPostFireYawScale,.35f,0,1);
    t.magneticPostFirePitchScale=Finite(t.magneticPostFirePitchScale,0,0,1);
    t.magneticPreFireBoostWindowMs=Finite(t.magneticPreFireBoostWindowMs,45,0,250);
    t.magneticPreFireBoostScale=Finite(t.magneticPreFireBoostScale,1.35f,1,3);
    t.flickTrajectoryWaitMs=Finite(t.flickTrajectoryWaitMs,120,0,3000);
    t.flickTrajectoryApexWindowMs=Finite(t.flickTrajectoryApexWindowMs,60,0,1000);
    t.flick2ndBoxPadding=Finite(t.flick2ndBoxPadding,8,0,80);
    t.flick2ndInnerRadius=Finite(t.flick2ndInnerRadius,34,0,250);
    t.flick2ndInnerSmoothScale=Finite(t.flick2ndInnerSmoothScale,.55f,.1f,1);
    t.flick2ndInnerMethod=(std::clamp)(t.flick2ndInnerMethod,-1,8);
    t.startLimiter=ValidateAimStartLimiterProfile(t.startLimiter);
    t.moveSplitMaxPixels=(std::clamp)(t.moveSplitMaxPixels,1,50);
    t.moveSplitDelayUs=(std::clamp)(t.moveSplitDelayUs,0,10000);
}
}
