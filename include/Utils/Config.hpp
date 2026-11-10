#pragma once

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <mutex>
#include <memory>
#include <cassert>
#include <utility>
#include <unordered_map>
#include <vector>
#include <imgui.h>

#include "Game/AimStartLimiter.hpp"
#include "Game/AimPlan.hpp"
#include "Game/AimBehaviorMode.hpp"
#include "Game/GameData.hpp"
#include "Utils/InputLabels.hpp"
#include "Game/TriggerBoneSelection.hpp"

// -----------------------------------------------------------------------
// OW::Config -- Single source of truth for all cheat configuration.
// All variables are inline (C++17) globals.
// -----------------------------------------------------------------------

namespace OW { namespace Config {

    // Odd while a full profile is loading; even when its settings are stable.
    inline std::atomic<std::uint64_t> configLoadGeneration{ 0 };

    inline constexpr int kAimBoneChest = 0;
    inline constexpr int kAimBoneHead  = 1;
    inline constexpr int kAimBoneNeck  = 2;
    inline constexpr int kAimBoneClosest = 3; // Choose the closest valid core aim sphere.
    inline constexpr int kMaxHeroPresetSlots = 12;
    inline constexpr float kMinFovDeg = 0.0f;
    inline constexpr float kMaxFovDeg = 180.0f;
    inline constexpr float kDefaultFovDeg = 15.0f;
    inline constexpr float kDefaultAimFovDeg = 5.0f;
    inline constexpr float kLegacyDefaultHitboxRadius = 0.13f;
    inline constexpr float kMinHitboxScalePercent = 0.0f;
    inline constexpr float kMaxHitboxScalePercent = 150.0f;
    inline constexpr float kDefaultHitboxScalePercent = 100.0f;
    inline constexpr int kFovModeFixed = 0;
    inline constexpr int kFovModeDynamicPreset = 1;
    inline constexpr int kFovModeCount = 2;
    inline constexpr int kMaxDynamicFovPoints = 5;
    inline constexpr float kMinDynamicFovDistanceM = 0.0f;
    inline constexpr float kMaxDynamicFovDistanceM = 500.0f;

    int NormalizeAimBone(int aimBone);
    const char* AimBoneName(int aimBone);
    inline float ClampFovDeg(float fovDeg)
    {
        return std::clamp(std::isfinite(fovDeg) ? fovDeg : kDefaultFovDeg,
                          kMinFovDeg,
                          kMaxFovDeg);
    }

    inline float FovCircleRenderAngleDeg(float fovDeg)
    {
        return ClampFovDeg(fovDeg);
    }

    inline float LegacyFovApertureToAngleDeg(float fovApertureDeg)
    {
        return ClampFovDeg(std::isfinite(fovApertureDeg)
            ? fovApertureDeg * 0.5f
            : kDefaultFovDeg);
    }

    inline float ClampHitboxScalePercent(float scalePercent)
    {
        return std::clamp(std::isfinite(scalePercent) ? scalePercent : kDefaultHitboxScalePercent,
                          kMinHitboxScalePercent,
                          kMaxHitboxScalePercent);
    }

    inline float HitboxScaleMultiplier(float scalePercent)
    {
        return ClampHitboxScalePercent(scalePercent) / 100.0f;
    }

    inline int ClampFovMode(int value)
    {
        return std::clamp(value, 0, kFovModeCount - 1);
    }

    inline float ClampDynamicFovDistanceM(float distanceM)
    {
        return std::clamp(std::isfinite(distanceM) ? distanceM : 0.0f,
                          kMinDynamicFovDistanceM,
                          kMaxDynamicFovDistanceM);
    }

    struct RuntimeDrawFovState {
        bool active = false;
        float fovDeg = kDefaultFovDeg;
        int slotKind = 0;
        int slotIndex = -1;
    };

    inline std::atomic<float> runtimeDrawFovDeg{ kDefaultFovDeg };
    inline std::atomic<bool> runtimeDrawFovActive{ false };
    inline std::atomic<int> runtimeDrawFovSlotKind{ 0 };
    inline std::atomic<int> runtimeDrawFovSlotIndex{ -1 };

    inline RuntimeDrawFovState SnapshotRuntimeDrawFov()
    {
        return {
            runtimeDrawFovActive.load(std::memory_order_acquire),
            ClampFovDeg(runtimeDrawFovDeg.load(std::memory_order_acquire)),
            runtimeDrawFovSlotKind.load(std::memory_order_acquire),
            runtimeDrawFovSlotIndex.load(std::memory_order_acquire)
        };
    }

    inline void SetRuntimeDrawFov(float fovDeg, int slotKind = 0, int slotIndex = -1)
    {
        runtimeDrawFovDeg.store(ClampFovDeg(fovDeg), std::memory_order_release);
        runtimeDrawFovSlotKind.store(slotKind, std::memory_order_release);
        runtimeDrawFovSlotIndex.store(slotIndex, std::memory_order_release);
        runtimeDrawFovActive.store(true, std::memory_order_release);
    }

    inline void RestoreRuntimeDrawFov(const RuntimeDrawFovState& state)
    {
        runtimeDrawFovDeg.store(ClampFovDeg(state.fovDeg), std::memory_order_release);
        runtimeDrawFovSlotKind.store(state.slotKind, std::memory_order_release);
        runtimeDrawFovSlotIndex.store(state.slotIndex, std::memory_order_release);
        runtimeDrawFovActive.store(state.active, std::memory_order_release);
    }

    inline float RuntimeDrawFovOrDefault(float fallbackFovDeg)
    {
        return runtimeDrawFovActive.load(std::memory_order_acquire)
            ? ClampFovDeg(runtimeDrawFovDeg.load(std::memory_order_acquire))
            : ClampFovDeg(fallbackFovDeg);
    }

    inline std::string configFileName = "config.ini";
    inline std::string lastConfigProfile = "config.ini";
    inline constexpr int kUiLanguageEnglish = 0;
    inline constexpr int kUiLanguageChineseSimplified = 1;
    inline constexpr int kUiLanguageCount = 2;
    inline int uiLanguage = kUiLanguageEnglish;
    inline int overlayFrameLimit = 0; // 0 = unlimited; canvas only, menu stays at ~60 Hz.
    inline int ClampUiLanguage(int value)
    {
        return std::clamp(value, 0, kUiLanguageCount - 1);
    }
    std::string ConfigDirectoryPath();
    std::string ConfigPath();
    bool InitializeActiveConfigProfile(const std::string& directory = {});
    bool PersistActiveConfigProfile(const std::string& profileName, const std::string& directory = {});
    std::string LastPersistenceError();
#ifdef UNLEASHED_SELF_TESTS
    int RunPersistenceSelfTest();
    int RunConfigConcurrencySelfTest();
#endif
    std::string HeroConfigPath();
    std::string HeroConfigPath(const std::string& configPath);

    // ---- Synchronisation ----
    inline std::mutex mutex;
    struct ConfigValues;
    inline thread_local bool configAuthority = false;
    inline thread_local ConfigValues* configView = nullptr;
    struct ScopedConfigRead {
        std::unique_ptr<ConfigValues> value;
        ConfigValues* previous;
        ScopedConfigRead();
        ~ScopedConfigRead();
        ScopedConfigRead(const ScopedConfigRead&) = delete;
    };


    // ---- Aim modes ----
    inline bool enableAimbot  = true;
    inline bool triggerbot    = false;
    inline decltype(triggerbot)& View_triggerbot();
    inline bool triggerbot2   = false;
    inline decltype(triggerbot2)& View_triggerbot2();
    inline bool Tracking      = false;
    inline decltype(Tracking)& View_Tracking();
    inline bool Tracking2     = false;
    inline bool Flick         = false;
    inline decltype(Flick)& View_Flick();
    inline bool Flick2        = false;

    // ---- Triggerbot (primary) ----
    inline int   triggerbotMode = 0;
    inline decltype(triggerbotMode)& View_triggerbotMode();          // 0=Hold, 1=Toggle, 2=Always
    inline int   triggerbotKey  = 1;
    inline decltype(triggerbotKey)& View_triggerbotKey();          // key index (reuses activation key VK list; None maps to VK 0)
    inline float triggerbotShotInterval = 0.0f;
    inline decltype(triggerbotShotInterval)& View_triggerbotShotInterval();   // scaled 0-100 → 0-500ms (slider value)
    inline bool  triggerbotChargeAware  = false;
    inline decltype(triggerbotChargeAware)& View_triggerbotChargeAware();  // wait for charge before firing
    inline float triggerbotMinCharge    = 30.0f;
    inline decltype(triggerbotMinCharge)& View_triggerbotMinCharge();  // minimum charge % (0-100, for charge-aware)
    inline bool  triggerbotDisableWhileReloading = false;
    inline decltype(triggerbotDisableWhileReloading)& View_triggerbotDisableWhileReloading();
    inline bool  triggerbotIgnoreInvisible = true;
    inline decltype(triggerbotIgnoreInvisible)& View_triggerbotIgnoreInvisible();
    inline TriggerBoneMask triggerbotBoneMask = kDefaultTriggerBoneMask;
    inline decltype(triggerbotBoneMask)& View_triggerbotBoneMask();

    // ---- Triggerbot (secondary / triggerbot2) ----
    inline int   triggerbotMode2 = 0;
    inline int   triggerbotKey2  = 1;
    inline float triggerbotShotInterval2 = 0.0f;
    inline bool  triggerbotChargeAware2  = false;
    inline float triggerbotMinCharge2    = 30.0f;
    inline bool  triggerbotDisableWhileReloading2 = false;
    inline bool  triggerbotIgnoreInvisible2 = true;
    inline TriggerBoneMask triggerbotBoneMask2 = kDefaultTriggerBoneMask;

    // ---- Triggerbot runtime state (not persisted) ----
    inline bool  triggerbotToggleActive  = false;  // current toggle state (mode=Toggle)
    inline bool  triggerbotToggleActive2 = false;
    inline DWORD triggerbotLastFireTick  = 0;      // GetTickCount of last primary fire
    inline DWORD triggerbotLastFireTick2 = 0;      // GetTickCount of last secondary fire

    // ---- Prediction ----
    inline bool projectile_arc  = false; // Ballistic arc correction for projectiles
    inline bool Prediction      = false;
    inline decltype(Prediction)& View_Prediction();
    inline bool Gravitypredit   = false;
    inline float predit_level   = 110.f;
    inline bool hanzoautospeed  = false;

    // ---- Keys ----
    inline int AimKey      = 0x01;   // VK_LBUTTON
    inline int aim_key     = 1;
    inline decltype(aim_key)& View_aim_key();      // Left Mouse
    inline int aim_key2    = 1;      // Left Mouse
    inline int togglekey   = 0;
    inline int MenuToggleKey = VK_HOME;
    inline int CacheRefreshKey = VK_F8;

    // ---- General aim ----
    inline float Fov        = kDefaultAimFovDeg;
    inline decltype(Fov)& View_Fov();
    inline float Fov2       = kDefaultFovDeg;
    inline decltype(Fov2)& View_Fov2();
    inline float minFov1    = kDefaultAimFovDeg;
    inline decltype(minFov1)& View_minFov1();
    inline float minFov2    = kDefaultFovDeg;
    inline decltype(minFov2)& View_minFov2();
    inline float Smooth     = 20.0f;
    inline decltype(Smooth)& View_Smooth();
    inline bool  autoscalefov = false;
    inline decltype(autoscalefov)& View_autoscalefov();
    inline float hitbox     = kDefaultHitboxScalePercent;
    inline decltype(hitbox)& View_hitbox(); // scale applied to resolved bone+projectile window
    inline float hitbox2    = kDefaultHitboxScalePercent;
    inline float missbox    = 0.6f;

    inline float Tracking_smooth  = 0.1f;
    inline decltype(Tracking_smooth)& View_Tracking_smooth();
    inline float Tracking_smooth2 = 0.1f;
    inline float Flick_smooth     = 0.1f;
    inline decltype(Flick_smooth)& View_Flick_smooth();
    inline float Flick_smooth2    = 0.1f;
    inline float accvalue   = 0.1f;
    inline float accvalue2  = 0.1f;
    inline float bladespeed = 0.1f;

    inline int  TargetBone  = kAimBoneHead;
    inline decltype(TargetBone)& View_TargetBone(); // aim-bone choice mirror: 0=chest, 1=head, 2=neck
    inline int  Bone        = kAimBoneHead;
    inline decltype(Bone)& View_Bone();
    inline int  Bone2       = kAimBoneHead;
    inline bool autobone    = false;
    inline decltype(autobone)& View_autobone();
    inline bool autobone2   = false;
    inline SkeletonBoneMask aimbotBoneMask = kDefaultAimBoneMask;
    inline decltype(aimbotBoneMask)& View_aimbotBoneMask();
    inline SkeletonBoneMask aimbotBoneMask2 = kDefaultAimBoneMask;
    inline bool switch_team  = false;
    inline bool switch_team2 = false;
    inline std::string BoneName  = "Head";
    inline decltype(BoneName)& View_BoneName();
    inline std::string BoneName2 = "Head";

    inline bool lockontarget    = false;
    inline bool trackcompensate = false;
    inline float comarea   = 0.01f;
    inline float comspeed  = 0.5f;

    inline bool aiaim       = false;
    inline bool targetdelay = false;
    inline int  targetdelaytime = 200;
    inline bool hitboxdelayshoot = false;
    inline int  hiboxdelaytime   = 200;

    inline bool dontshot   = false;
    inline int  shotcount  = 0;
    inline int  shotmanydont = 3;

    // ---- Aimbot UI options ----
    inline constexpr int kAimBehaviorTracking = 0;
    inline constexpr int kAimBehaviorFlick = 1;
    inline constexpr int kAimBehaviorFlick2nd = 2;
    inline constexpr int kAimBehaviorReacquire = 3;
    inline constexpr int kAimBehaviorMagneticTrigger = 4;
    inline constexpr int kAimBehaviorCount = 5;
    inline bool  aimbotAutoshot = false;
    inline decltype(aimbotAutoshot)& View_aimbotAutoshot();
    inline bool  aimbotKeepFiring = false;
    inline decltype(aimbotKeepFiring)& View_aimbotKeepFiring();
    inline bool  aimbotRequireActionHeld = false;
    inline decltype(aimbotRequireActionHeld)& View_aimbotRequireActionHeld();
    inline int   aimbotPredictionMode = 0;
    inline decltype(aimbotPredictionMode)& View_aimbotPredictionMode(); // 0=Auto, 1=Force On, 2=Force Off
    inline bool  aimbotPredictFovEntry = false;
    inline decltype(aimbotPredictFovEntry)& View_aimbotPredictFovEntry();
    inline float aimbotFovEntryPredictionMs = 60.0f;
    inline decltype(aimbotFovEntryPredictionMs)& View_aimbotFovEntryPredictionMs();
    inline float aimbotFovEntryMaxOutsideDeg = 1.5f;
    inline decltype(aimbotFovEntryMaxOutsideDeg)& View_aimbotFovEntryMaxOutsideDeg();
    inline int   aimBehavior = kAimBehaviorTracking;
    inline decltype(aimBehavior)& View_aimBehavior(); // 0=Tracking, 1=Flick, 2=Flick2nd, 3=Reacquire, 4=MagneticTrigger
    inline int   aimbotFirePolicy = 0;
    inline decltype(aimbotFirePolicy)& View_aimbotFirePolicy();     // 0=Manual, 1=Hold, 2=Tap, 3=ReleaseDelay, 4=Burst, 5=ChargeRelease
    inline float aimbotTriggerDelay = 0.0f; // triggerbot delay in ms (scaled)
    inline float aimbotMaxHead = 100.0f;
    inline decltype(aimbotMaxHead)& View_aimbotMaxHead();
    inline int   aimMethod = 0;
    inline decltype(aimMethod)& View_aimMethod(); // 0=Linear, 1=PID, 2=Bezier, 3=Piecewise, 4=AccelLimited, 5=Constant, 6=HybridPID, 7=Shuwu, 8=ErrorResponseCurve
    inline int   aimbotSmoothType = 0;
    inline decltype(aimbotSmoothType)& View_aimbotSmoothType(); // 0=Constant Speed, 1=Linear, 2=Bezier
    inline constexpr int kAimMethodCount = 9;
    inline constexpr float kAimConstantAngularSpeedMaxDeg = 3600.0f;
    inline std::array<int, kAimBehaviorCount> aimBehaviorMethod = { 0, 0, 0, 0, 0 };
    inline decltype(aimBehaviorMethod)& View_aimBehaviorMethod();
    inline std::array<int, kAimBehaviorCount> aimBehaviorMethodPreset = { -1, -1, -1, -1, -1 };
    inline decltype(aimBehaviorMethodPreset)& View_aimBehaviorMethodPreset();
    inline std::array<float, kAimBehaviorCount> aimBehaviorBaseSpeed = { 100.0f, 100.0f, 100.0f, 100.0f, 100.0f };
    inline std::array<AimStartLimiterProfile, kAimBehaviorCount> aimBehaviorStartLimiterProfiles{};
    // Legacy behavior acceleration is kept for config compatibility; method-level
    // acceleration now owns the Accel Limited controller tuning.
    inline std::array<float, kAimBehaviorCount> aimBehaviorAcceleration = { 0.1f, 0.1f, 0.1f, 0.1f, 0.1f };
    inline std::array<bool, kAimBehaviorCount> aimBehaviorMoveSplitEnabled = { true, false, false, true, true };
    // Legacy Pixels names/INI keys below represent relative device counts.
    inline std::array<int, kAimBehaviorCount> aimBehaviorMoveSplitMaxPixels = { 4, 50, 50, 4, 4 };
    inline std::array<int, kAimBehaviorCount> aimBehaviorMoveSplitDelayUs = { 800, 0, 0, 800, 800 };
    inline std::array<float, kAimMethodCount> aimMethodAngularSpeedScale = {
        100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f
    };
    inline std::array<int, 2> secondaryAimMethodOverride = { -1, -1 }; // -1=inherit, 0-8=kAimMethod
    inline float aimPidP = 0.5f;
    inline decltype(aimPidP)& View_aimPidP();
    inline float aimPidI = 0.01f;
    inline decltype(aimPidI)& View_aimPidI();
    inline float aimPidD = 0.1f;
    inline decltype(aimPidD)& View_aimPidD();
    inline float aimPidMaxIntegral = 10.0f;
    inline decltype(aimPidMaxIntegral)& View_aimPidMaxIntegral();
    inline float aimPidDeadzone = 1.0f;
    inline decltype(aimPidDeadzone)& View_aimPidDeadzone();
    inline int   aimBezierControlPoints = 2;
    inline decltype(aimBezierControlPoints)& View_aimBezierControlPoints();
    inline float aimBezierCurvature = 0.5f;
    inline decltype(aimBezierCurvature)& View_aimBezierCurvature();
    inline float aimBezierSpeed = 50.0f;
    inline decltype(aimBezierSpeed)& View_aimBezierSpeed();
    inline float aimPiecewiseNearDegrees = 2.0f;
    inline float aimPiecewiseMidDegrees = 6.0f;
    inline float aimPiecewiseFarDegrees = 12.0f;
    inline float aimPiecewiseNearScale = 0.20f;
    inline float aimPiecewiseMidScale = 0.45f;
    inline float aimPiecewiseFarScale = 0.75f;
    inline float aimAccelLimitedAcceleration = 0.1f;
    inline float aimConstantAngularSpeedDeg = 30.0f;
    // AimMethod-wide ordinary Flick repeat controls; hero slots do not override these.
    inline float flickRepeatWindowMs = 120.0f;
    inline float flickRepeatMaxSpeedDegPerSec = 180.0f;
    inline float aimHybridConstantSpeedDeg = 45.0f;
    inline float aimHybridMaxSpeedDeg = 720.0f;
    inline float aimHybridAccelerationDeg = 1800.0f;
    inline float aimHybridDecelerationDeg = 2600.0f;
    inline float aimHybridNearRadiusDeg = 3.0f;
    inline float aimHybridTargetMotionGain = 0.35f;
    inline float aimHybridSuddenMotionBoost = 2.5f;
    inline float aimHybridDeadzoneDeg = 0.10f;
    inline int aimBehaviorPresetId = -1;
    inline decltype(aimBehaviorPresetId)& View_aimBehaviorPresetId();

    struct AimMethodPreset : AimPlan::Motion {
        int id = -1;
        std::string name = "Method Preset";
    };

    struct AimBehaviorPreset : AimBehaviorTuning {
        int id = -1;
        int defaultBehavior = -1; // Stable identity of the five protected defaults.
        std::string name = "Behavior Preset";
        int behavior = kAimBehaviorTracking;
        int method = 0;
        int methodPresetId = -1; // Legacy defaults source; Slot values are independent.
        AimPlan::Motion motion{};
    };
    inline int nextAimBehaviorPresetId = 1;
    inline int& View_nextAimBehaviorPresetId();
    inline AimModeRuntime aimModeState{};
    inline AimModeRuntime& View_aimModeState();
    const AimBehaviorTuning* ActiveSlotBehaviorTuning();

    struct DynamicFovPoint {
        float distanceM = 0.0f;
        float fovDeg = kDefaultFovDeg;
    };

    struct DynamicFovPreset {
        int id = -1;
        std::string name = "Dynamic FOV";
        bool smooth = true;
        int pointCount = 5;
        std::array<DynamicFovPoint, kMaxDynamicFovPoints> points{{
            { 0.0f, 180.0f },
            { 5.0f, 180.0f },
            { 10.0f, 30.0f },
            { 20.0f, 8.0f },
            { 30.0f, 5.0f },
        }};
    };

    inline std::vector<AimMethodPreset> aimMethodPresets{};
    inline decltype(aimMethodPresets)& View_aimMethodPresets();
    inline std::vector<AimBehaviorPreset> aimBehaviorPresets{};
    inline decltype(aimBehaviorPresets)& View_aimBehaviorPresets();
    inline std::vector<DynamicFovPreset> dynamicFovPresets{};
    inline decltype(dynamicFovPresets)& View_dynamicFovPresets();
    inline int   aimbotFovMode = kFovModeFixed;
    inline decltype(aimbotFovMode)& View_aimbotFovMode();
    inline int   aimbotDynamicFovPresetId = -1;
    inline decltype(aimbotDynamicFovPresetId)& View_aimbotDynamicFovPresetId();
    inline float aimbotStickiness = 100.0f;
    inline decltype(aimbotStickiness)& View_aimbotStickiness();
    inline float aimbotSmoothY = 50.0f;
    inline float aimbotPitchScale = 1.0f;
    inline decltype(aimbotPitchScale)& View_aimbotPitchScale();
    inline float aimbotMaxAim = 100.0f;
    inline decltype(aimbotMaxAim)& View_aimbotMaxAim();
    inline float aimbotMinCharge = 5.0f;
    inline decltype(aimbotMinCharge)& View_aimbotMinCharge();
    inline float aimbotMaxCharge = 100.0f;
    inline decltype(aimbotMaxCharge)& View_aimbotMaxCharge();
    inline bool  aimbotIgnoreInvisible = true;
    inline decltype(aimbotIgnoreInvisible)& View_aimbotIgnoreInvisible();
    inline int   aimbotTrace = 0;
    inline decltype(aimbotTrace)& View_aimbotTrace(); // 0=Strict, 1=Relaxed, 2=Off
    inline int   aimbotUnlock = 0;
    inline decltype(aimbotUnlock)& View_aimbotUnlock(); // 0=Anytime, 1=On Release, 2=Never
    inline float aimbotLockTime = 20.0f;
    inline decltype(aimbotLockTime)& View_aimbotLockTime();
    inline float aimbotMaxDist = 100.0f;
    inline decltype(aimbotMaxDist)& View_aimbotMaxDist();
    inline float aimbotMinDist = 0.0f;
    inline decltype(aimbotMinDist)& View_aimbotMinDist();
    inline int   aimbotAttack = 0;
    inline decltype(aimbotAttack)& View_aimbotAttack(); // action profile: primary/secondary/scoped/unscoped/ability/ultimate
    inline int   aimbotTeam = 0;
    inline decltype(aimbotTeam)& View_aimbotTeam(); // 0=Enemies, 1=Allies, 2=All
    inline thread_local float selectedAimFovDeg = 0;
    inline AimPlan::Plan aimPlan{};
    inline AimPlan::Plan& View_aimPlan();
    // Worker-local selection result; never part of persistent/shared config.
    inline thread_local AimPlan::Effective selectedAimPlan{};
    inline thread_local uint64_t selectedAimPlanTarget = 0;
    inline thread_local uint64_t selectedAimHero = 0;
    inline thread_local uint64_t selectedAimSample = 0;
    inline thread_local uint64_t selectedAimActor=0, selectedAimEpoch=0, selectedAimScene=0;
    struct SlotTargetRules {
        bool active=false, ignoreSombraStealth=false, requireTargetable=false;
        float allyHealthMaxPercent=100;
        SkeletonBoneMask allyBoneMask=kDefaultAimBoneMask;
    };
    inline thread_local SlotTargetRules slotTargetRules{};
    inline thread_local bool targetAimPlanEnabled = false;
    inline thread_local AimMethodPreset selectedAimMethod{};
    inline int   aimbotPriority = 0;
    inline decltype(aimbotPriority)& View_aimbotPriority(); // 0=FOV, 1=HP, 2=Distance, 3=weighted
    inline float aimbotEffectiveHitWindow = kLegacyDefaultHitboxRadius; // runtime-only scaled TargetCandidate window
    inline float aimbotEffectiveHitWindow2 = kLegacyDefaultHitboxRadius;
    inline float aimbotTrackingDeadzone = 0.0f;
    inline decltype(aimbotTrackingDeadzone)& View_aimbotTrackingDeadzone();
    inline float aimbotFlickShotClampMs = 0.0f;
    inline decltype(aimbotFlickShotClampMs)& View_aimbotFlickShotClampMs();
    inline float aimbotFlickPostFireDelayMs = 0.0f;
    inline decltype(aimbotFlickPostFireDelayMs)& View_aimbotFlickPostFireDelayMs();
    inline float aimbotMagneticShotIntervalMs = 0.0f;
    inline decltype(aimbotMagneticShotIntervalMs)& View_aimbotMagneticShotIntervalMs(); // 0 = conservative weapon-class fallback
    inline float aimbotMagneticPostFirePauseMs = 35.0f;
    inline decltype(aimbotMagneticPostFirePauseMs)& View_aimbotMagneticPostFirePauseMs();
    inline float aimbotMagneticRecoveryMs = 90.0f;
    inline decltype(aimbotMagneticRecoveryMs)& View_aimbotMagneticRecoveryMs();
    inline float aimbotMagneticPostFireYawScale = 0.35f;
    inline decltype(aimbotMagneticPostFireYawScale)& View_aimbotMagneticPostFireYawScale();
    inline float aimbotMagneticPostFirePitchScale = 0.0f;
    inline decltype(aimbotMagneticPostFirePitchScale)& View_aimbotMagneticPostFirePitchScale();
    inline float aimbotMagneticPreFireBoostWindowMs = 45.0f;
    inline decltype(aimbotMagneticPreFireBoostWindowMs)& View_aimbotMagneticPreFireBoostWindowMs();
    inline float aimbotMagneticPreFireBoostScale = 1.35f;
    inline decltype(aimbotMagneticPreFireBoostScale)& View_aimbotMagneticPreFireBoostScale();
    inline bool  aimbotFlickTrajectoryWait = false;
    inline decltype(aimbotFlickTrajectoryWait)& View_aimbotFlickTrajectoryWait();
    inline float aimbotFlickTrajectoryWaitMs = 120.0f;
    inline decltype(aimbotFlickTrajectoryWaitMs)& View_aimbotFlickTrajectoryWaitMs();
    inline float aimbotFlickTrajectoryApexWindowMs = 60.0f;
    inline decltype(aimbotFlickTrajectoryApexWindowMs)& View_aimbotFlickTrajectoryApexWindowMs();
    inline bool  aimbotFlick2ndTriggerGate = true;
    inline decltype(aimbotFlick2ndTriggerGate)& View_aimbotFlick2ndTriggerGate();
    inline float aimbotFlick2ndBoxPadding = 8.0f;
    inline decltype(aimbotFlick2ndBoxPadding)& View_aimbotFlick2ndBoxPadding();
    inline float aimbotFlick2ndInnerRadius = 34.0f;
    inline decltype(aimbotFlick2ndInnerRadius)& View_aimbotFlick2ndInnerRadius();
    inline float aimbotFlick2ndInnerSmoothScale = 0.55f;
    inline decltype(aimbotFlick2ndInnerSmoothScale)& View_aimbotFlick2ndInnerSmoothScale();
    inline int   aimbotFlick2ndInnerMethod = 2;
    inline decltype(aimbotFlick2ndInnerMethod)& View_aimbotFlick2ndInnerMethod();
    inline bool  aimbotTwoStage = false;
    inline decltype(aimbotTwoStage)& View_aimbotTwoStage(); // legacy mirror: true only when active behavior is Flick2nd
    inline bool  aimbotTwoStageTriggerGate = true;
    inline decltype(aimbotTwoStageTriggerGate)& View_aimbotTwoStageTriggerGate();
    inline float aimbotTwoStageBoxPadding = 8.0f;
    inline decltype(aimbotTwoStageBoxPadding)& View_aimbotTwoStageBoxPadding();
    inline float aimbotTwoStageInnerRadius = 34.0f;
    inline decltype(aimbotTwoStageInnerRadius)& View_aimbotTwoStageInnerRadius();
    inline float aimbotTwoStageInnerSmoothScale = 0.55f;
    inline decltype(aimbotTwoStageInnerSmoothScale)& View_aimbotTwoStageInnerSmoothScale();
    inline bool  aimOvershootCurve = false;
    inline float aimOvershootGain = 0.25f;
    // Legacy name/INI key: device counts, not screen pixels.
    inline float aimOvershootResetPixels = 56.0f;

    inline int ClampAimBehaviorIndex(int value)
    {
        return std::clamp(value, 0, kAimBehaviorCount - 1);
    }

    inline bool IsTrackingBehavior(int behavior)
    {
        return ClampAimBehaviorIndex(behavior) == kAimBehaviorTracking;
    }

    inline bool IsFlickBehavior(int behavior)
    {
        const int normalized = ClampAimBehaviorIndex(behavior);
        return normalized == kAimBehaviorFlick ||
               normalized == kAimBehaviorFlick2nd;
    }

    inline bool IsFlick2ndBehavior(int behavior)
    {
        return ClampAimBehaviorIndex(behavior) == kAimBehaviorFlick2nd;
    }

    inline bool IsMagneticTriggerBehavior(int behavior)
    {
        return ClampAimBehaviorIndex(behavior) == kAimBehaviorMagneticTrigger;
    }

    inline bool UsesTrackingDeadzone(int behavior)
    {
        const int normalized = ClampAimBehaviorIndex(behavior);
        return normalized == kAimBehaviorTracking ||
               normalized == kAimBehaviorMagneticTrigger;
    }

    inline bool UsesFlickFireControls(int behavior)
    {
        return IsFlickBehavior(behavior) || IsMagneticTriggerBehavior(behavior);
    }

    inline float ClampTrackingDeadzonePixels(float value)
    {
        return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 250.0f);
    }

    inline float TrackingDeadzoneDampingWidthPixels(float radiusPixels)
    {
        const float radius = ClampTrackingDeadzonePixels(radiusPixels);
        if (radius <= 0.0f)
            return 0.0f;
        return std::clamp(radius * 0.5f, 8.0f, 48.0f);
    }

    inline float TrackingDeadzoneDampingScale(float distancePixels, float radiusPixels)
    {
        const float radius = ClampTrackingDeadzonePixels(radiusPixels);
        if (radius <= 0.0f)
            return 1.0f;
        if (!std::isfinite(distancePixels) || distancePixels <= radius)
            return 0.0f;

        const float width = TrackingDeadzoneDampingWidthPixels(radius);
        if (width <= 0.0f)
            return 1.0f;

        const float t = std::clamp((distancePixels - radius) / width, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    inline float ClampFlickShotClampMs(float value)
    {
        return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1000.0f);
    }

    inline float ClampFlickPostFireDelayMs(float value)
    {
        return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 500.0f);
    }

    inline float ClampFovEntryPredictionMs(float value)
    {
        return std::clamp(std::isfinite(value) ? value : 60.0f, 0.0f, 250.0f);
    }

    inline float ClampFovEntryMaxOutsideDeg(float value)
    {
        return std::clamp(std::isfinite(value) ? value : 1.5f, 0.0f, 15.0f);
    }

    inline float ClampMagneticTimingMs(float value, float fallback = 0.0f)
    {
        return std::clamp(std::isfinite(value) ? value : fallback, 0.0f, 2000.0f);
    }

    inline float ClampMagneticShotIntervalMs(float value, float fallback = 0.0f)
    {
        return std::clamp(std::isfinite(value) ? value : fallback, 0.0f, 5000.0f);
    }

    inline float ClampMagneticAxisScale(float value, float fallback)
    {
        return std::clamp(std::isfinite(value) ? value : fallback, 0.0f, 1.0f);
    }

    inline float ClampMagneticBoostScale(float value)
    {
        return std::clamp(std::isfinite(value) ? value : 1.35f, 1.0f, 3.0f);
    }

    inline float ClampTrajectoryWaitMs(float value)
    {
        return std::clamp(std::isfinite(value) ? value : 120.0f, 0.0f, 1000.0f);
    }

    inline float ClampTrajectoryApexWindowMs(float value)
    {
        return std::clamp(std::isfinite(value) ? value : 60.0f, 0.0f, 300.0f);
    }

    inline int ClampAimMethodIndex(int value)
    {
        return std::clamp(value, 0, kAimMethodCount - 1);
    }

    inline int ClampAimMethodOverride(int value)
    {
        return std::clamp(value, -1, kAimMethodCount - 1);
    }

    inline float ClampAimMethodAngularSpeedScalePercent(float value)
    {
        return std::isfinite(value) ? std::clamp(value, 0.0f, 200.0f) : 100.0f;
    }

    inline float ClampAimConstantAngularSpeedDeg(float value)
    {
        return std::isfinite(value)
            ? std::clamp(value, 0.0f, kAimConstantAngularSpeedMaxDeg)
            : 30.0f;
    }

    inline AimMethodPreset* FindMutableAimMethodPreset(int presetId)
    {
        // Caller owns a read/edit scope or the authority lock.
        assert(configAuthority || configView);
        if (presetId < 0)
            return nullptr;
        auto item = std::find_if(View_aimMethodPresets().begin(), View_aimMethodPresets().end(),
            [presetId](const AimMethodPreset& preset) { return preset.id == presetId; });
        return item != View_aimMethodPresets().end() ? &(*item) : nullptr;
    }

    inline const AimMethodPreset* FindAimMethodPreset(int presetId)
    {
        return FindMutableAimMethodPreset(presetId);
    }

    inline AimBehaviorPreset* FindMutableAimBehaviorPreset(int presetId)
    {
        // Caller owns a read/edit scope or the authority lock.
        assert(configAuthority || configView);
        if (presetId < 0)
            return nullptr;
        auto item = std::find_if(View_aimBehaviorPresets().begin(), View_aimBehaviorPresets().end(),
            [presetId](const AimBehaviorPreset& preset) { return preset.id == presetId; });
        return item != View_aimBehaviorPresets().end() ? &(*item) : nullptr;
    }

    inline const AimBehaviorPreset* FindAimBehaviorPreset(int presetId)
    {
        return FindMutableAimBehaviorPreset(presetId);
    }

    inline int DefaultAimBehaviorPresetId(int behavior = kAimBehaviorTracking) {
        for (const auto& preset : View_aimBehaviorPresets())
            if (preset.defaultBehavior == behavior) return preset.id;
        return -1;
    }

    inline int ClampAimMethodPresetId(int presetId)
    {
        ScopedConfigRead configRead;
        return FindAimMethodPreset(presetId) ? presetId : -1;
    }

    inline int ClampAimBehaviorPresetId(int presetId)
    {
        ScopedConfigRead configRead;
        return FindAimBehaviorPreset(presetId) ? presetId : -1;
    }

    inline DynamicFovPreset* FindMutableDynamicFovPreset(int presetId)
    {
        // Caller owns a read/edit scope or the authority lock.
        assert(configAuthority || configView);
        if (presetId < 0)
            return nullptr;
        auto item = std::find_if(View_dynamicFovPresets().begin(), View_dynamicFovPresets().end(),
            [presetId](const DynamicFovPreset& preset) { return preset.id == presetId; });
        return item != View_dynamicFovPresets().end() ? &(*item) : nullptr;
    }

    inline const DynamicFovPreset* FindDynamicFovPreset(int presetId)
    {
        return FindMutableDynamicFovPreset(presetId);
    }

    inline int ClampDynamicFovPresetId(int presetId)
    {
        ScopedConfigRead configRead;
        return FindDynamicFovPreset(presetId) ? presetId : -1;
    }

    inline DynamicFovPreset ValidateDynamicFovPresetValue(DynamicFovPreset preset, int fallbackIndex = -1)
    {
        if (preset.name.empty()) {
            preset.name = "Dynamic FOV";
            if (fallbackIndex >= 0) {
                preset.name += " ";
                preset.name += std::to_string(fallbackIndex + 1);
            }
        }
        if (preset.name.size() > 63)
            preset.name.resize(63);

        preset.pointCount = std::clamp(preset.pointCount, 2, kMaxDynamicFovPoints);
        for (int index = 0; index < preset.pointCount; ++index) {
            preset.points[static_cast<size_t>(index)].distanceM =
                ClampDynamicFovDistanceM(preset.points[static_cast<size_t>(index)].distanceM);
            preset.points[static_cast<size_t>(index)].fovDeg =
                ClampFovDeg(preset.points[static_cast<size_t>(index)].fovDeg);
        }

        std::sort(preset.points.begin(), preset.points.begin() + preset.pointCount,
            [](const DynamicFovPoint& lhs, const DynamicFovPoint& rhs) {
                return lhs.distanceM < rhs.distanceM;
            });

        for (int index = 1; index < preset.pointCount; ++index) {
            DynamicFovPoint& current = preset.points[static_cast<size_t>(index)];
            const DynamicFovPoint& previous = preset.points[static_cast<size_t>(index - 1)];
            if (current.distanceM <= previous.distanceM)
                current.distanceM = ClampDynamicFovDistanceM(previous.distanceM + 0.01f);
        }

        for(int index=preset.pointCount-2;index>=0;--index)
            preset.points[index].distanceM=(std::min)(preset.points[index].distanceM,preset.points[index+1].distanceM-0.01f);
        return preset;
    }

    inline float EvaluateDynamicFovPreset(const DynamicFovPreset& rawPreset,
                                          float distanceM,
                                          float fallbackFovDeg = kDefaultFovDeg)
    {
        const DynamicFovPreset preset = ValidateDynamicFovPresetValue(rawPreset);
        const float distance = ClampDynamicFovDistanceM(distanceM);
        if (preset.pointCount < 2)
            return ClampFovDeg(fallbackFovDeg);

        const DynamicFovPoint& first = preset.points[0];
        if (distance <= first.distanceM)
            return ClampFovDeg(first.fovDeg);

        for (int index = 1; index < preset.pointCount; ++index) {
            const DynamicFovPoint& previous = preset.points[static_cast<size_t>(index - 1)];
            const DynamicFovPoint& current = preset.points[static_cast<size_t>(index)];
            if (distance > current.distanceM)
                continue;

            const float span = current.distanceM - previous.distanceM;
            float t = span > 0.0001f ? (distance - previous.distanceM) / span : 1.0f;
            t = std::clamp(t, 0.0f, 1.0f);
            if (preset.smooth)
                t = t * t * (3.0f - 2.0f * t);
            return ClampFovDeg(previous.fovDeg + (current.fovDeg - previous.fovDeg) * t);
        }

        return ClampFovDeg(preset.points[static_cast<size_t>(preset.pointCount - 1)].fovDeg);
    }

    inline bool IsDynamicAimFovActive()
    {
        ScopedConfigRead configRead;
        return !View_autoscalefov() &&
               ClampFovMode(View_aimbotFovMode()) == kFovModeDynamicPreset &&
               FindDynamicFovPreset(View_aimbotDynamicFovPresetId()) != nullptr;
    }

    inline float ResolveDynamicAimFovForDistance(float fixedFovDeg, float distanceM)
    {
        ScopedConfigRead configRead;
        const float fallback = ClampFovDeg(fixedFovDeg);
        if (!IsDynamicAimFovActive())
            return fallback;
        return EvaluateDynamicFovPreset(
            *FindDynamicFovPreset(View_aimbotDynamicFovPresetId()),
            distanceM,
            fallback);
    }

    inline const AimBehaviorPreset* ActiveAimBehaviorPreset(int behavior)
    {
        const int normalized = ClampAimBehaviorIndex(behavior);
        const AimBehaviorPreset* preset = FindAimBehaviorPreset(View_aimBehaviorPresetId());
        if (!preset || ClampAimBehaviorIndex(preset->behavior) != normalized)
            return nullptr;
        return preset;
    }

    inline const AimMethodPreset* AimMethodPresetForBehavior(int behavior)
    {
        const int normalized = ClampAimBehaviorIndex(behavior);
        if (const AimBehaviorPreset* behaviorPreset = ActiveAimBehaviorPreset(normalized)) {
            return FindAimMethodPreset(behaviorPreset->methodPresetId);
        }

        return FindAimMethodPreset(
            View_aimBehaviorMethodPreset()[static_cast<size_t>(normalized)]);
    }

    inline int AimBehaviorMethod(int behavior)
    {
        if (targetAimPlanEnabled && selectedAimPlan.overrideMotion) return ClampAimMethodIndex(selectedAimMethod.method);
        if (View_aimModeState().enabled && ClampAimBehaviorIndex(behavior)==ClampAimBehaviorIndex(View_aimBehavior())) return ClampAimMethodIndex(View_aimModeState().motion.method);
        ScopedConfigRead configRead;
        const int normalized = ClampAimBehaviorIndex(behavior);
        if (const AimMethodPreset* methodPreset = AimMethodPresetForBehavior(normalized))
            return ClampAimMethodIndex(methodPreset->method);
        if (const AimBehaviorPreset* behaviorPreset = ActiveAimBehaviorPreset(normalized))
            return ClampAimMethodIndex(behaviorPreset->method);
        return ClampAimMethodIndex(View_aimBehaviorMethod()[static_cast<size_t>(normalized)]);
    }

    inline int SecondaryAimMethod(int aimMode)
    {
        const int mode = std::clamp(aimMode, 0, 1);
        const int overrideMethod = ClampAimMethodOverride(secondaryAimMethodOverride[static_cast<size_t>(mode)]);
        return overrideMethod >= 0 ? overrideMethod : AimBehaviorMethod(mode);
    }

    inline float AimBehaviorBaseSpeed(int behavior)
    {
        if (const auto* tuning=ActiveSlotBehaviorTuning(); tuning && ClampAimBehaviorIndex(behavior)==ClampAimBehaviorIndex(View_aimBehavior())) return tuning->baseSpeed;
        const int normalized = ClampAimBehaviorIndex(behavior);
        if (const AimBehaviorPreset* preset = ActiveAimBehaviorPreset(normalized)) {
            const float value = preset->baseSpeed;
            return std::isfinite(value) ? std::clamp(value, 0.0f, 100.0f) : 100.0f;
        }
        const float value = aimBehaviorBaseSpeed[static_cast<size_t>(normalized)];
        return std::isfinite(value) ? std::clamp(value, 0.0f, 100.0f) : 100.0f;
    }

    inline AimStartLimiterProfile ResolveAimStartLimiterProfile(int behavior)
    {
        if (const auto* tuning=ActiveSlotBehaviorTuning(); tuning && ClampAimBehaviorIndex(behavior)==ClampAimBehaviorIndex(View_aimBehavior())) return tuning->startLimiter;
        const int normalized = ClampAimBehaviorIndex(behavior);
        if (const AimBehaviorPreset* preset = ActiveAimBehaviorPreset(normalized))
            return ValidateAimStartLimiterProfile(preset->startLimiter);
        return ValidateAimStartLimiterProfile(
            aimBehaviorStartLimiterProfiles[static_cast<size_t>(normalized)]);
    }

    inline int ClampMoveSplitMaxPixels(int value)
    {
        return std::clamp(value, 1, 50);
    }

    inline int ClampMoveSplitDelayUs(int value)
    {
        return std::clamp(value, 0, 10000);
    }

    inline bool AimBehaviorMoveSplitEnabled(int behavior)
    {
        if (const auto* tuning=ActiveSlotBehaviorTuning(); tuning && ClampAimBehaviorIndex(behavior)==ClampAimBehaviorIndex(View_aimBehavior())) return tuning->moveSplitEnabled;
        const int normalized = ClampAimBehaviorIndex(behavior);
        if (const AimBehaviorPreset* preset = ActiveAimBehaviorPreset(normalized))
            return preset->moveSplitEnabled;
        return aimBehaviorMoveSplitEnabled[static_cast<size_t>(normalized)];
    }

    inline int AimBehaviorMoveSplitMaxPixels(int behavior)
    {
        if (const auto* tuning=ActiveSlotBehaviorTuning(); tuning && ClampAimBehaviorIndex(behavior)==ClampAimBehaviorIndex(View_aimBehavior())) return tuning->moveSplitMaxPixels;
        const int normalized = ClampAimBehaviorIndex(behavior);
        if (const AimBehaviorPreset* preset = ActiveAimBehaviorPreset(normalized))
            return ClampMoveSplitMaxPixels(preset->moveSplitMaxPixels);
        const int value = aimBehaviorMoveSplitMaxPixels[static_cast<size_t>(normalized)];
        return ClampMoveSplitMaxPixels(value);
    }

    inline int AimBehaviorMoveSplitDelayUs(int behavior)
    {
        if (const auto* tuning=ActiveSlotBehaviorTuning(); tuning && ClampAimBehaviorIndex(behavior)==ClampAimBehaviorIndex(View_aimBehavior())) return tuning->moveSplitDelayUs;
        const int normalized = ClampAimBehaviorIndex(behavior);
        if (const AimBehaviorPreset* preset = ActiveAimBehaviorPreset(normalized))
            return ClampMoveSplitDelayUs(preset->moveSplitDelayUs);
        const int value = aimBehaviorMoveSplitDelayUs[static_cast<size_t>(normalized)];
        return ClampMoveSplitDelayUs(value);
    }

    inline float AimMethodAngularSpeedScale(int method)
    {
        const float value = aimMethodAngularSpeedScale[static_cast<size_t>(ClampAimMethodIndex(method))];
        return ClampAimMethodAngularSpeedScalePercent(value) / 100.0f;
    }

    inline const AimMethodPreset* ActiveAimMethodPreset(int method,bool inheritTargetMotion=true)
    {
        const int normalizedMethod = ClampAimMethodIndex(method);
        if (inheritTargetMotion && targetAimPlanEnabled && selectedAimPlan.overrideMotion && selectedAimMethod.method == normalizedMethod) return &selectedAimMethod;
        if (View_aimModeState().enabled) {
            thread_local AimMethodPreset slotMotion;
            const AimPlan::Motion* resolved=nullptr;
            if(View_aimModeState().motion.method==normalizedMethod) resolved=&View_aimModeState().motion;
            else for(const auto& c:View_aimModeState().configs) if(c.presetId==View_aimBehaviorPresetId())
                for(const auto& m:c.motions) if(m.method==normalizedMethod) resolved=&m;
            if(resolved) {static_cast<AimPlan::Motion&>(slotMotion)=*resolved;return &slotMotion;}
        }
        const int normalizedBehavior = ClampAimBehaviorIndex(View_aimBehavior());
        const AimMethodPreset* preset = AimMethodPresetForBehavior(normalizedBehavior);
        if (!preset || ClampAimMethodIndex(preset->method) != normalizedMethod)
            return nullptr;
        return preset;
    }

    inline float RuntimeAimMethodAngularSpeedScale(int method,bool inheritTargetMotion=true)
    {
        if (const AimMethodPreset* preset = ActiveAimMethodPreset(method,inheritTargetMotion))
            return ClampAimMethodAngularSpeedScalePercent(preset->angularSpeedScale) / 100.0f;
        return AimMethodAngularSpeedScale(method);
    }

    inline float AimMethodAcceleration(int method)
    {
        if (ClampAimMethodIndex(method) != 4)
            return 0.0f;
        const float value = aimAccelLimitedAcceleration;
        return std::isfinite(value) ? std::clamp(value, 0.0f, 20.0f) : 0.1f;
    }

    inline float RuntimeAimMethodAcceleration(int method,bool inheritTargetMotion=true)
    {
        if (ClampAimMethodIndex(method) != 4)
            return 0.0f;
        if (const AimMethodPreset* preset = ActiveAimMethodPreset(method,inheritTargetMotion)) {
            const float value = preset->accelLimitedAcceleration;
            return std::isfinite(value) ? std::clamp(value, 0.0f, 20.0f) : 0.1f;
        }
        return AimMethodAcceleration(method);
    }

    inline float AimConstantAngularSpeedDeg()
    {
        return ClampAimConstantAngularSpeedDeg(aimConstantAngularSpeedDeg);
    }

    inline float RuntimeAimConstantAngularSpeedDeg(bool inheritTargetMotion=true)
    {
        if (const AimMethodPreset* preset = ActiveAimMethodPreset(5,inheritTargetMotion))
            return ClampAimConstantAngularSpeedDeg(preset->constantAngularSpeedDeg);
        return AimConstantAngularSpeedDeg();
    }

    inline float AimBehaviorAcceleration(int behavior)
    {
        return RuntimeAimMethodAcceleration(AimBehaviorMethod(behavior));
    }

    inline float AimPiecewiseNearDegrees(const AimMethodPreset* preset = nullptr)
    {
        const float value = preset ? preset->piecewiseNearDegrees : aimPiecewiseNearDegrees;
        return std::isfinite(value) ? std::clamp(value, 0.0f, 30.0f) : 2.0f;
    }

    inline float AimPiecewiseMidDegrees(const AimMethodPreset* preset = nullptr)
    {
        const float raw = preset ? preset->piecewiseMidDegrees : aimPiecewiseMidDegrees;
        const float value = std::isfinite(raw) ? raw : 6.0f;
        return std::clamp(value, AimPiecewiseNearDegrees(preset), 45.0f);
    }

    inline float AimPiecewiseFarDegrees(const AimMethodPreset* preset = nullptr)
    {
        const float raw = preset ? preset->piecewiseFarDegrees : aimPiecewiseFarDegrees;
        const float value = std::isfinite(raw) ? raw : 12.0f;
        return std::clamp(value, AimPiecewiseMidDegrees(preset), 60.0f);
    }

    inline float AimPiecewiseNearScale(const AimMethodPreset* preset = nullptr)
    {
        const float value = preset ? preset->piecewiseNearScale : aimPiecewiseNearScale;
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.20f;
    }

    inline float AimPiecewiseMidScale(const AimMethodPreset* preset = nullptr)
    {
        const float value = preset ? preset->piecewiseMidScale : aimPiecewiseMidScale;
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.45f;
    }

    inline float AimPiecewiseFarScale(const AimMethodPreset* preset = nullptr)
    {
        const float value = preset ? preset->piecewiseFarScale : aimPiecewiseFarScale;
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.75f;
    }

    inline float AimBehaviorSmoothInput(int behavior, float scalePercent, float runtimeScale = 1.0f)
    {
        const float scale = std::isfinite(scalePercent) ? std::clamp(scalePercent, 0.0f, 100.0f) : 100.0f;
        const float extraScale = std::isfinite(runtimeScale) ? std::clamp(runtimeScale, 0.0f, 2.0f) : 1.0f;
        return (AimBehaviorBaseSpeed(behavior) / 100.0f) * (scale / 100.0f) * extraScale;
    }

    // ---- Hero-specific ----
    inline bool GenjiBlade       = false;
    inline bool AutoShiftGenji   = false;
    inline bool widowautounscope = false;

    // ---- Shoot / fire ----
    inline bool  AutoShoot  = false;
    inline int   Shoottime  = 500;
    inline bool  shooted    = false;
    inline bool  shooted2   = false;
    inline int   lasttime   = 0;
    inline float lasthealth = 0.f;
    inline bool  skilled    = false;
    inline int   slasttime  = 0;
    inline bool  sskilled   = false;
    inline bool  reloading  = false;

    // ---- Blade / Genji ----
    inline int  Qstarttime  = 0;
    inline int  Qtime       = 0;
    inline int  lastenemy   = -1;

    inline bool doingdelay      = false;
    inline int  timebeforedelay = 0;

    // ---- Misc auto ----
    inline bool AutoMelee      = false;
    inline float meleehealth   = 30.f;
    inline float meleedistance = 5.f;
    inline bool AutoRMB        = false;
    inline float AutoRMBhealth   = 100.f;
    inline float AutoRMBdistance = 30.f;
    inline bool AutoSkill      = false;
    inline float SkillHealth   = 50.f;
    inline bool AntiAFK        = false;

    // ---- Secondary aim ----
    inline bool secondaim   = false;
    inline bool highPriority = false;

    // ---- ESP toggles ----
    inline bool draw_info       = true;
    inline bool drawbattletag   = false;
    inline bool drawhealth      = false;
    inline bool healthbar       = false;
    inline bool healthbar2      = false;
    inline float healthbartextsize = 16.f;
    inline bool dist            = true;
    inline float visualMaxDist  = 100.f;
    inline bool name            = false;
    inline bool ult             = true;
    inline bool gameOutlines    = false;
    inline bool gameOutlineVisibleOnly = false;
    inline bool gameOutlineRainbow = false;
    inline bool draw_skel       = false;
    inline bool draw_avatar     = true;
    inline bool draw_hitbox     = false;
    inline bool skillinfo       = false;
    inline bool showOverlayInfo = true;
    inline bool showSlotStatus = true;
    // The legacy "ult" key now enables the complete hero status widget.
    inline int heroStatusStyle = 1; // 0=ED compact, 1=custom portrait/skills/ring
    inline float radarPosX = 1.0f;
    inline float radarPosY = 1.0f;
    inline float radarScale = 1.3f;
    inline float slotPosX = 0.0f;
    inline float slotPosY = 0.5f;
    inline float heroStatusPosX = 0.12f;
    inline float heroStatusPosY = 0.0f;
    inline float heroStatusScale = 0.7f;
    // Ultimate / Skill display position
    // Ultimate is normalized to the left side panel; 0/2 are legacy config values.
    inline int ultimateDisplayMode = 1;
    // Ultimate roster team filter: 0=All, 1=Enemy, 2=Ally.
    inline int ultimateRosterFilter = 1;
    inline int skillDisplayMode = 0;
    // Radar corner: 0=BottomRight, 1=BottomLeft, 2=TopRight, 3=TopLeft
    inline int radarCorner = 0;
    inline bool radar           = false;
    inline bool radarline       = false;
    inline bool drawline        = false;
    inline bool draw_fov        = false;
    inline bool drawTrackingDeadzones = false;
    inline bool draw_hp_pack    = false;
    inline bool crosscircle     = false;
    inline bool eyeray          = false;
    inline bool boxPerfMode     = false;
    inline bool boxPerfFastRect = true;

    // Legacy compile shims for untouched Overwatch.hpp references. Not persisted.
    extern bool draw_edge;
    extern bool drawbox3d;
    extern bool manualsave;

    // ---- Outline colours (float4, 0-1 range) ----
    inline ImVec4 enargb        = ImVec4(1.f, 0.f, 0.f, 1.f);
    inline ImVec4 invisnenargb  = ImVec4(1.f, 1.f, 1.f, 1.f);
    inline ImVec4 targetargb    = ImVec4(1.f, 1.f, 0.f, 1.f);
    inline ImVec4 allyargb      = ImVec4(0.f, 0.f, 1.f, 0.f);

    // ---- Box/fov colours ----
    inline ImVec4 EnemyCol   = ImVec4(1.f, 0.f, 0.f, 0.f);
    inline ImVec4 fovcol     = ImVec4(1.f, 1.f, 1.f, 1.f);

    enum class FovRingSlotKind : int {
        Aim = 0
    };

    struct FovRingSlotStyle {
        bool visible = true;
        ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 0.75f);
        float thickness = 1.5f;
        int lineStyle = 0; // 0=solid, 1=dashed
        bool showLabel = false;
    };

    inline ImVec4 DefaultFovRingColor(FovRingSlotKind kind, int slotIndex)
    {
        (void)kind;
        constexpr float palette[][3] = {
            { 1.00f, 0.92f, 0.20f },
            { 0.15f, 0.78f, 1.00f },
            { 1.00f, 0.38f, 0.74f },
            { 0.35f, 1.00f, 0.55f },
            { 1.00f, 0.56f, 0.22f },
            { 0.62f, 0.54f, 1.00f },
            { 0.24f, 0.92f, 0.82f },
            { 1.00f, 0.28f, 0.28f },
            { 0.76f, 1.00f, 0.26f },
            { 0.30f, 0.48f, 1.00f },
            { 1.00f, 0.72f, 0.90f },
            { 0.62f, 1.00f, 0.34f }
        };
        constexpr int paletteCount = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
        const int paletteIndex = std::clamp(slotIndex, 0, kMaxHeroPresetSlots - 1) % paletteCount;
        const int shiftedIndex = paletteIndex;
        return ImVec4(
            palette[shiftedIndex][0],
            palette[shiftedIndex][1],
            palette[shiftedIndex][2],
            0.78f);
    }

    inline FovRingSlotStyle DefaultFovRingStyle(FovRingSlotKind kind, int slotIndex)
    {
        FovRingSlotStyle style{};
        style.visible = true;
        style.color = DefaultFovRingColor(kind, slotIndex);
        style.thickness = 1.5f;
        style.lineStyle = 0;
        style.showLabel = false;
        return style;
    }

    inline std::array<FovRingSlotStyle, kMaxHeroPresetSlots> MakeDefaultFovRingStyles(FovRingSlotKind kind)
    {
        std::array<FovRingSlotStyle, kMaxHeroPresetSlots> styles{};
        for (int slotIndex = 0; slotIndex < kMaxHeroPresetSlots; ++slotIndex)
            styles[static_cast<size_t>(slotIndex)] = DefaultFovRingStyle(kind, slotIndex);
        return styles;
    }

    inline FovRingSlotStyle ClampFovRingStyle(FovRingSlotStyle style, FovRingSlotKind kind, int slotIndex)
    {
        const FovRingSlotStyle fallback = DefaultFovRingStyle(kind, slotIndex);
        auto clampChannel = [](float value, float fallbackValue) {
            return std::clamp(std::isfinite(value) ? value : fallbackValue, 0.0f, 1.0f);
        };
        style.color.x = clampChannel(style.color.x, fallback.color.x);
        style.color.y = clampChannel(style.color.y, fallback.color.y);
        style.color.z = clampChannel(style.color.z, fallback.color.z);
        style.color.w = clampChannel(style.color.w, fallback.color.w);
        style.thickness = std::clamp(std::isfinite(style.thickness) ? style.thickness : fallback.thickness,
                                     0.5f,
                                     6.0f);
        style.lineStyle = std::clamp(style.lineStyle, 0, 1);
        return style;
    }

    inline std::array<FovRingSlotStyle, kMaxHeroPresetSlots> aimFovRingStyles =
        MakeDefaultFovRingStyles(FovRingSlotKind::Aim);

    inline FovRingSlotStyle& FovRingStyleFor(FovRingSlotKind kind, int slotIndex)
    {
        const int clampedSlot = std::clamp(slotIndex, 0, kMaxHeroPresetSlots - 1);
        (void)kind;
        return aimFovRingStyles[static_cast<size_t>(clampedSlot)];
    }

    // ---- Targeting state ----
    inline int  Targetenemyi    = -1;
    inline int  Targetenemyifov = -1;
    inline float health         = 0.f;

    // ---- Game state ----
    inline int  lastheroid   = -2;
    inline std::atomic<bool> Menu{ true };
    inline std::string nowhero = "Unknown";

    // ---- Output devices (legacy INI section: KMBox) ----
    inline char catBoxIp[64]="";
    inline int catBoxPort=0;
    inline char catBoxUuid[32]="";
    inline int catBoxMonitorPort=0;

    inline bool outputEnabled = false;
    inline int  outputDeviceType = 0; // 0=Network, 1=Serial, 2=Mock, 3=MAKCU 4M, 4=MAKCU 115200, 5=Ferrum Software API, 6=CatBox
    inline char outputIp[32] = "192.168.2.188";
    inline int  outputPort = 8808;
    inline int  outputMonitorPort = 8809;
    inline bool outputMonitorPortManualOverride = false;
    inline char outputMac[32] = "12525C53";
    inline char outputComPort[16] = "COM3";
    inline float outputCountsPerRadian = 100.0f;       // KMBox relative mouse counts needed for one radian at the reference game sens
    inline float gameMouseSensitivity = 15.0f;        // manual/effective current in-game sensitivity used for scaling
    inline float referenceGameSensitivity = 15.0f;    // game sens used when counts-per-radian was measured
    inline bool  autoScaleByGameSensitivity = false;  // scale counts by reference/current game sensitivity
    inline bool  autoReadGameMouseSensitivity = false; // read current game sens from DMA when available
    inline float detectedGameMouseSensitivity = 0.0f;  // runtime-only DMA-read current game sens
    inline bool  gameMouseSensitivityAutoDetected = false;
    inline float& outputAimSensitivity = outputCountsPerRadian;     // legacy config/code alias
    inline float& sensReference = referenceGameSensitivity;       // legacy config/code alias
    inline bool&  autoSyncSensitivity = autoScaleByGameSensitivity; // legacy config/code alias
    inline float hostMouseDpi = 1600.0f;       // manual/effective host mouse DPI fallback
    inline float detectedHostMouseDpi = 0.0f;  // runtime-only automatic detection result
    inline bool  hostMouseDpiAutoDetected = false;
    inline int   outputInputDelayMs = 0;
    inline bool outputDebugLog = false;
    inline std::atomic<bool> outputSuppressOutputWhileMenuOpen{ false };
    inline constexpr int kOutputSuppressedStatus = -9010;

    inline bool OutputSuppressedByMenu()
    {
        return
            outputSuppressOutputWhileMenuOpen.load(std::memory_order_acquire) &&
            Menu.load(std::memory_order_acquire);
    }

    inline int RecommendedOutputMonitorPort(int commandPort)
    {
        if (commandPort >= 1 && commandPort < 65535)
            return commandPort + 1;
        if (commandPort == 65535)
            return commandPort - 1;
        return 8809;
    }

    inline bool IsValidOutputUdpPort(int port)
    {
        return port >= 1 && port <= 65535;
    }

    inline int EffectiveOutputMonitorPort()
    {
        const int recommended = RecommendedOutputMonitorPort(outputPort);
        if (!outputMonitorPortManualOverride)
            return recommended;
        if (!IsValidOutputUdpPort(outputMonitorPort) || outputMonitorPort == outputPort)
            return recommended;
        return outputMonitorPort;
    }

    // ---- Diagnostics / Dry-run ----
    inline bool aimDryRun = false;              // Dry-run mode: log everything, don't move cursor
    inline bool aimVerboseLog = false;          // Extra verbose per-tick logging
    inline int aimDryRunLogIntervalMs = 100;    // Min interval between dry-run log lines (avoid spam)

    // ---- Sensitivity auto-calibration ----
    inline std::atomic<bool> calibrationInProgress{ false };
    inline float calibratedCountsPerRadian = 0.0f;       // 0 = not calibrated, use manual outputCountsPerRadian
    inline float calibratedPitchCountsPerRadian = 0.0f;  // Separate pitch calibration if >5% different from yaw
    inline float& calibratedPixelsPerRadian = calibratedCountsPerRadian;           // legacy alias
    inline float& calibratedPixelsPerRadianPitch = calibratedPitchCountsPerRadian; // legacy alias
    inline int calibrationMovePixels = 200;          // KMBox relative counts to send for calibration
    inline int calibrationStabilityWaitMs = 50;      // Wait time before/after move for stable read
    inline int calibrationSampleCount = 3;           // Number of calibration samples to average

    inline float OutputBaseCountsPerRadian()
    {
        return calibratedCountsPerRadian > 0.0f
            ? std::clamp(calibratedCountsPerRadian, 0.1f, 20000.0f)
            : std::clamp(outputCountsPerRadian, 0.1f, 20000.0f);
    }

    inline float OutputPitchBaseCountsPerRadian()
    {
        return calibratedPitchCountsPerRadian > 0.0f
            ? std::clamp(calibratedPitchCountsPerRadian, 0.1f, 20000.0f)
            : OutputBaseCountsPerRadian();
    }

    inline float OutputGameSensitivityScale()
    {
        const float effectiveGameSensitivity =
            (autoReadGameMouseSensitivity &&
             gameMouseSensitivityAutoDetected &&
             std::isfinite(detectedGameMouseSensitivity) &&
             detectedGameMouseSensitivity > 0.0f)
                ? detectedGameMouseSensitivity
                : gameMouseSensitivity;
        if (!autoScaleByGameSensitivity ||
            !std::isfinite(effectiveGameSensitivity) ||
            !std::isfinite(referenceGameSensitivity) ||
            effectiveGameSensitivity <= 0.0f ||
            referenceGameSensitivity <= 0.0f) {
            return 1.0f;
        }
        return referenceGameSensitivity / effectiveGameSensitivity;
    }

    inline float EffectiveGameMouseSensitivity()
    {
        return (autoReadGameMouseSensitivity &&
                gameMouseSensitivityAutoDetected &&
                std::isfinite(detectedGameMouseSensitivity) &&
                detectedGameMouseSensitivity > 0.0f)
            ? detectedGameMouseSensitivity
            : gameMouseSensitivity;
    }

    inline float OutputYawCountsPerRadian()
    {
        return OutputBaseCountsPerRadian() * OutputGameSensitivityScale();
    }

    inline float OutputPitchCountsPerRadian()
    {
        return OutputPitchBaseCountsPerRadian() * OutputGameSensitivityScale();
    }

    // ---- Mouse movement splitting ----
    inline bool moveSplitEnabled = true;       // Enable micro-splitting of mouse moves
    inline int moveSplitMaxPixels = 4;         // Legacy name: relative device counts per micro-move chunk (1-50)
    inline int moveSplitDelayUs = 800;         // Microsecond delay between chunks (100-5000)

    // ---- Per-hero aim/trigger presets ----
    struct TriggerPreset {
        bool enabled = false;
        int action = 0;          // 0=Primary, 1=Secondary, 2=Scoped, 3=Unscoped, 4-6=Abilities, 7=Ultimate
        int mode = 0;            // 0=Hold, 1=Toggle, 2=Always
        int key = 1;             // key index (reuses activation key VK list; None maps to VK 0)
        float shotInterval = 0.0f;
        bool chargeAware = false;
        float minCharge = 30.0f;
        bool disableWhileReloading = false;
        bool ignoreInvisible = true;
        TriggerBoneMask boneMask = kDefaultTriggerBoneMask;
        bool drawHitbox = false;
    };

    constexpr bool ShouldBlockTriggerForReload(bool disableWhileReloading,
                                               bool reloading)
    {
        return disableWhileReloading && reloading;
    }

    struct HeroPreset : AimBehaviorTuning {
        int modeConfigVersion = 0;
        int releaseConfigVersion = 1;
        int resolvedModeId = -2;
        std::vector<SlotBehaviorConfig> behaviorConfigs;
        AimPlan::Plan aimPlan{};
        float fov = kDefaultFovDeg; // angular separation from current view direction, in degrees
        int fovMode = kFovModeFixed; // 0=fixed fov, 1=dynamic preset by distance
        int dynamicFovPresetId = -1;
        int bone = kAimBoneHead;  // aim-bone choice: 0=chest, 1=head, 2=neck
        bool autoBone = false;    // true = choose closest visible skeleton bone at runtime
        SkeletonBoneMask aimBoneMask = kDefaultAimBoneMask;
        float hitbox = kDefaultHitboxScalePercent; // percentage of resolved bone+projectile window
        int aimMode = 0;         // 0=Tracking, 1=Flick
        int aimBehavior = kAimBehaviorTracking; // 0=Tracking, 1=Flick, 2=Flick2nd, 3=Reacquire, 4=MagneticTrigger
        int aimBehaviorPresetId = -1;
        int aimMethod = 0;       // legacy: smoothing method now comes from Misc behavior profiles
        int smoothType = 0;      // legacy
        float pidP = 0.5f;
        float pidI = 0.01f;
        float pidD = 0.1f;
        float pidMaxIntegral = 10.0f;
        float pidDeadzone = 1.0f;
        int bezierControlPoints = 2;
        float bezierCurvature = 0.5f;
        float bezierSpeed = 50.0f;
        int key = 1;             // aim activation key index (reuses activation key VK list)
        bool requireActionHeld = false; // require the physical weapon/skill action before aim activation
        bool prediction = false; // movement prediction
        int predictionMode = 0;   // 0=Auto, 1=Force On, 2=Force Off
        bool predictFovEntry = false;
        float fovEntryPredictionMs = 60.0f;
        float fovEntryMaxOutsideDeg = 1.5f;
        float maxHeadDistance = 100.0f;
        float stickiness = 100.0f;
        int priority = 0;        // 0=FOV, 1=HP, 2=Distance
        int allyKey = OW::Labels::kAimActivationKeyNoneIndex;
        float allyHealthMaxPercent = 100.0f;
        SkeletonBoneMask allyBoneMask = kDefaultAimBoneMask;
        bool ignoreSombraStealth = false;
        bool requireTargetable = false;
        int targetTeam = 0;      // 0=Enemies, 1=Allies, 2=All
        bool ignoreInvisible = true;
        int traceCondition = 0;   // 0=Strict, 1=Relaxed, 2=Off
        int unlockCondition = 0;  // 0=Anytime, 1=On Release, 2=Never
        float lockTime = 20.0f;
        float maxDistance = 100.0f;
        float minDistance = 0.0f;
        TriggerPreset trigger{};
    };

    inline AimPlan::Motion CaptureBehaviorMotion(int behavior, bool classDefaults = false) {
        ScopedConfigRead configRead;
        AimPlan::Motion m;
        const auto* definition=classDefaults ? nullptr : ActiveAimBehaviorPreset(behavior);
        const int methodId=definition?definition->methodPresetId:View_aimBehaviorMethodPreset()[ClampAimBehaviorIndex(behavior)];
        if(const auto* method=FindAimMethodPreset(methodId)) return *method;
        m.method=definition?definition->method:View_aimBehaviorMethod()[ClampAimBehaviorIndex(behavior)];
        m.angularSpeedScale=AimMethodAngularSpeedScale(m.method)*100;
        m.pidP=View_aimPidP();
        m.pidI=View_aimPidI();
        m.pidD=View_aimPidD();
        m.pidMaxIntegral=View_aimPidMaxIntegral();
        m.pidDeadzone=View_aimPidDeadzone();
        m.bezierCurvature=View_aimBezierCurvature();
        m.bezierSpeed=View_aimBezierSpeed();
        m.piecewiseNearDegrees=aimPiecewiseNearDegrees;
        m.piecewiseMidDegrees=aimPiecewiseMidDegrees;
        m.piecewiseFarDegrees=aimPiecewiseFarDegrees;
        m.piecewiseNearScale=aimPiecewiseNearScale;
        m.piecewiseMidScale=aimPiecewiseMidScale;
        m.piecewiseFarScale=aimPiecewiseFarScale;
        m.accelLimitedAcceleration=aimAccelLimitedAcceleration;
        m.constantAngularSpeedDeg=aimConstantAngularSpeedDeg;
        m.hybridConstantSpeedDeg=aimHybridConstantSpeedDeg;
        m.hybridMaxSpeedDeg=aimHybridMaxSpeedDeg;
        m.hybridAccelerationDeg=aimHybridAccelerationDeg;
        m.hybridDecelerationDeg=aimHybridDecelerationDeg;
        m.hybridNearRadiusDeg=aimHybridNearRadiusDeg;
        m.hybridTargetMotionGain=aimHybridTargetMotionGain;
        m.hybridSuddenMotionBoost=aimHybridSuddenMotionBoost;
        m.hybridDeadzoneDeg=aimHybridDeadzoneDeg;
        m.bezierControlPoints=View_aimBezierControlPoints();
        AimPlan::NormalizeMotion(m); return m;
    }
    inline SlotBehaviorConfig& EnsureSlotBehaviorConfig(HeroPreset& p,int id) {
        auto it=std::find_if(p.behaviorConfigs.begin(),p.behaviorConfigs.end(),[&](const auto& c){return c.presetId==id;});
        if(it==p.behaviorConfigs.end()) {p.behaviorConfigs.push_back({id}); return p.behaviorConfigs.back();}
        return *it;
    }
    inline void StoreHeroBehaviorMode(HeroPreset& p) {
        if(p.modeConfigVersion==0 || p.resolvedModeId< -1) return;
        auto& c=EnsureSlotBehaviorConfig(p,p.resolvedModeId);
        auto b=std::find_if(c.behaviors.begin(),c.behaviors.end(),[&](const auto& v){return v.behavior==p.aimBehavior;});
        AimBehaviorTuning t=p; NormalizeBehaviorTuning(t);
        if(b==c.behaviors.end()) c.behaviors.push_back({p.aimBehavior,t}); else b->tuning=t;
        auto motion=p.aimPlan.motion; motion.method=ClampAimMethodIndex(p.aimMethod); AimPlan::NormalizeMotion(motion);
        auto m=std::find_if(c.motions.begin(),c.motions.end(),[&](const auto& v){return v.method==motion.method;});
        if(m==c.motions.end()) c.motions.push_back(motion); else *m=motion;
    }
    inline AimPlan::Motion& EnsureSlotMotion(HeroPreset& p,int method,bool reset=false) {
        auto& c=EnsureSlotBehaviorConfig(p,p.aimBehaviorPresetId);
        auto m=std::find_if(c.motions.begin(),c.motions.end(),[&](const auto& v){return v.method==method;});
        if(m==c.motions.end() || reset) {
            auto defaults=p.aimPlan.motion;
            if(const auto* def=FindAimBehaviorPreset(p.aimBehaviorPresetId)) defaults=def->motion;
            defaults.method=ClampAimMethodIndex(method);AimPlan::NormalizeMotion(defaults);
            if(m==c.motions.end()) {c.motions.push_back(defaults);m=std::prev(c.motions.end());}
            else *m=defaults;
        }
        return *m;
    }
    inline void ResolveHeroBehaviorMode(HeroPreset& p,bool reset=false) {
        ScopedConfigRead configRead;
        const bool legacy=p.modeConfigVersion==0;
        std::erase_if(p.behaviorConfigs,[](const auto& c){return c.presetId>=0&&!FindAimBehaviorPreset(c.presetId);});
        if(p.aimBehaviorPresetId<0 && p.aimPlan.overrideMotion) p.aimMethod=ClampAimMethodIndex(p.aimPlan.motion.method);
        const auto* legacyDefinition=FindAimBehaviorPreset(p.aimBehaviorPresetId);
        if(legacy && legacyDefinition && p.aimPlan.overrideMotion && p.aimPlan.motion.method!=legacyDefinition->method) {
            p.aimBehavior=legacyDefinition->behavior;p.aimBehaviorPresetId=-1;
        }
        StoreHeroBehaviorMode(p);
        const auto* def=FindAimBehaviorPreset(p.aimBehaviorPresetId);
        const int id=def?def->id:-1;
        const int behavior=ClampAimBehaviorIndex(def?def->behavior:p.aimBehavior);
        AimPlan::Motion defaults=def?def->motion:CaptureBehaviorMotion(behavior);
        defaults.method=ClampAimMethodIndex(def?def->method:(p.aimPlan.overrideMotion?p.aimPlan.motion.method:defaults.method));
        if(legacy && p.aimPlan.overrideMotion) defaults=p.aimPlan.motion;
        AimBehaviorTuning tuning=def?static_cast<const AimBehaviorTuning&>(*def):static_cast<const AimBehaviorTuning&>(p);
        if(legacy) {
            tuning=p;
            tuning.baseSpeed=def?def->baseSpeed:aimBehaviorBaseSpeed[behavior];
            tuning.startLimiter=def?def->startLimiter:aimBehaviorStartLimiterProfiles[behavior];
            tuning.moveSplitEnabled=def?def->moveSplitEnabled:aimBehaviorMoveSplitEnabled[behavior];
            tuning.moveSplitMaxPixels=def?def->moveSplitMaxPixels:aimBehaviorMoveSplitMaxPixels[behavior];
            tuning.moveSplitDelayUs=def?def->moveSplitDelayUs:aimBehaviorMoveSplitDelayUs[behavior];
        }
        auto& c=EnsureSlotBehaviorConfig(p,id);
        auto b=std::find_if(c.behaviors.begin(),c.behaviors.end(),[&](const auto& v){return v.behavior==behavior;});
        if(b==c.behaviors.end()) {c.behaviors.push_back({behavior,tuning});b=std::prev(c.behaviors.end());}
        if(reset) b->tuning=tuning;
        auto m=std::find_if(c.motions.begin(),c.motions.end(),[&](const auto& v){return v.method==defaults.method;});
        if(m==c.motions.end()) {c.motions.push_back(defaults);m=std::prev(c.motions.end());}
        if(reset) *m=defaults;
        NormalizeBehaviorTuning(b->tuning);AimPlan::NormalizeMotion(*m);
        static_cast<AimBehaviorTuning&>(p)=b->tuning;
        NormalizeBehaviorTuning(p);
        p.aimBehaviorPresetId=id;p.resolvedModeId=id;p.modeConfigVersion=1;
        p.aimBehavior=behavior;p.aimMode=(behavior==0?0:1);
        p.aimMethod=defaults.method;p.aimPlan.overrideMotion=true;p.aimPlan.motion=*m;
        if(behavior==kAimBehaviorFlick2nd && p.flick2ndInnerMethod>=0 && p.flick2ndInnerMethod!=p.aimMethod)
            EnsureSlotMotion(p,p.flick2ndInnerMethod,reset);
    }

    inline float ResolveHeroPresetFovForDistance(const HeroPreset& preset, float distanceM)
    {
        ScopedConfigRead configRead;
        const float fallback = ClampFovDeg(preset.fov);
        if (ClampFovMode(preset.fovMode) != kFovModeDynamicPreset)
            return fallback;
        const DynamicFovPreset* dynamicPreset = FindDynamicFovPreset(preset.dynamicFovPresetId);
        if (!dynamicPreset)
            return fallback;
        return EvaluateDynamicFovPreset(*dynamicPreset, distanceM, fallback);
    }

    inline float ResolveRuntimeHeroPresetFovForDistance(const HeroPreset& preset, float distanceM)
    {
        ScopedConfigRead configRead;
        const float fallback = ClampFovDeg(preset.fov);
        if (View_autoscalefov())
            return fallback;
        return ResolveHeroPresetFovForDistance(preset, distanceM);
    }

    enum class AimScopeRequirement : int {
        All = 0,
        ScopedOnly = 1,
    };

    inline AimScopeRequirement NormalizeAimScopeRequirement(int value)
    {
        return value == static_cast<int>(AimScopeRequirement::ScopedOnly)
            ? AimScopeRequirement::ScopedOnly
            : AimScopeRequirement::All;
    }

    inline const char* AimScopeRequirementName(AimScopeRequirement requirement)
    {
        return NormalizeAimScopeRequirement(static_cast<int>(requirement)) ==
                AimScopeRequirement::ScopedOnly
            ? "ScopedOnly"
            : "All";
    }

    inline bool AimScopeRequirementMatches(AimScopeRequirement requirement, bool rmbHeld)
    {
        return NormalizeAimScopeRequirement(static_cast<int>(requirement)) ==
                AimScopeRequirement::All ||
            rmbHeld;
    }

    // Higher values win. Activation-key setting 0 is RMB; explicit scoped
    // hotkeys must outrank the RMB-bound scoped tracking baseline.
    inline int AimScopePressedSelectionPriority(AimScopeRequirement requirement,
                                                bool rmbHeld,
                                                int activationKeySetting)
    {
        requirement = NormalizeAimScopeRequirement(static_cast<int>(requirement));
        if (!AimScopeRequirementMatches(requirement, rmbHeld))
            return -1;
        if (requirement == AimScopeRequirement::ScopedOnly)
            return activationKeySetting == 0 ? 2 : 3;
        return 1;
    }

    inline int AimScopeFallbackSelectionPriority(AimScopeRequirement requirement,
                                                 bool rmbHeld)
    {
        requirement = NormalizeAimScopeRequirement(static_cast<int>(requirement));
        if (!AimScopeRequirementMatches(requirement, rmbHeld))
            return -1;
        return requirement == AimScopeRequirement::ScopedOnly ? 1 : 0;
    }

    struct HeroSlotPreset {
        uint64_t stableId = 0; // 持久身份；显示编号和数组位置不是身份。
        std::string name = "Preset";
        bool present = false;
        bool enabled = false;
        AimScopeRequirement scopeRequirement = AimScopeRequirement::All;
        HeroPreset preset{};
    };

    enum class HeroSkillInputChannel : int {
        Primary = 0,
        Secondary = 1
    };

    inline constexpr int kMaxHeroSkillSequenceSteps = 64;

    struct HeroSkillSequenceStep {
        int buttonMask = 1;   // bit0=left, bit1=right, bit2=middle
        int durationMs = 0;
        float speedScale = 1.0f;
        int jitterMs = 0;
    };

    struct HeroSkillTrackingParams {
        int aimBehavior = kAimBehaviorTracking;
        int method = 0;
        float smooth = 0.0f;
        float speedScale = 100.0f;
        float fov = 0.0f;
        int bone = kAimBoneChest;
        float hitbox = 0.0f;     // percentage of resolved bone+projectile window; 0 disables the gate
        int aimBehaviorPresetId = -1;
        float yawScale = 100.0f;
        float pitchScale = 100.0f;
    };

    struct HeroSkillSettings {
        bool enabled = false;
        int key = 0;
        float healthThreshold = 50.0f;
        float enemyHealthThreshold = 50.0f;
        float allyHealthThreshold = 50.0f;
        float distance = 30.0f;
        int mode = 0;
        float cooldown = 0.0f;
        bool cooldownGuard = true;
        bool prediction = false;
        int minTargets = 1;
        float radius = 0.0f;
        float meleeRadius = 0.35f; // extra world-space tolerance around every melee bone
        std::vector<HeroSkillSequenceStep> sequenceSteps{};
        HeroSkillTrackingParams tracking{};
        bool sequencePhaseAwareTracking = false;
        float sequencePostFirePauseMs = 35.0f;
        float sequenceRecoveryMs = 90.0f;
        float sequencePostFireYawScale = 0.35f;
        float sequencePostFirePitchScale = 0.0f;
        float sequencePreFireBoostWindowMs = 45.0f;
        float sequencePreFireBoostScale = 1.35f;
        int pitchDownDurationMs = 45;
        float pitchDownDurationJitter = 10.0f;
        float pitchDownTargetAngle = 90.0f;
        float pitchUpOffsetJitter = 1.5f;
        int fireDelayMs = 50;
        int jumpKeyCode = VK_SPACE;
        bool ammoGuard = false;
        int ammoGuardReserve = 1;
        int skillKey = -1; // Legacy storage only; runtime output uses hero action mapping.
        int runtimeOutputVk = 0; // Resolved per execution, never serialized.
        int runtimeJumpVk = VK_SPACE;
        float projectileSpeed = 0.0f;
        float projectileRadius = 0.0f;
        bool projectileGravity = false;
        float preFireDelayMs = 0.0f; // skill wind-up added to lead prediction
        int targetPriority = 0; // 0 angular error, 1 health percentage, 2 distance
        float assistanceDelayMs = 0.0f; // wait before aim assistance, separate from projectile wind-up
        bool drawSkillFov = false;
        ImVec4 skillFovColor{0.80f, 0.35f, 0.45f, 0.85f};
        float maxAimTimeMs = 650.0f; // 0 disables the acquisition-session timeout
        float punchMaxHeightAbove = 2.0f;
        float punchMaxHeightBelow = 2.0f;
        float punchFullChargeMs = 1000.0f;
        float punchMaxHoldMs = 2000.0f; // estimated total hold until automatic release; calibrate in game
        float punchReleaseLeadMs = 80.0f;
        bool healthTriggerEnabled = true;
        bool threatTriggerEnabled = true;
        int normalThreatMask = 15;
        bool requireVisible = true;
        bool allowInvisibleSombra = false;
        int targetRoleMask = 7; // 1 damage, 2 tank, 4 support; unknown roles pass only with all selected.
        bool pauseAim = true;
        bool pauseTrigger = true;
        bool pauseSkills = true;
        bool automaticAim = true;
        int moveForwardMs = 300;
        int jumpLeadMs = 50;
    };

    using HeroSkillPresetStore = std::unordered_map<uint64_t, std::unordered_map<std::string, HeroSkillSettings>>;

    inline std::unordered_map<uint64_t, std::array<HeroSlotPreset, kMaxHeroPresetSlots>> heroAimPresets;
    inline decltype(heroAimPresets)& View_heroAimPresets();
    inline std::unordered_map<uint64_t, std::array<HeroSlotPreset, kMaxHeroPresetSlots>> heroTriggerPresets;
    inline decltype(heroTriggerPresets)& View_heroTriggerPresets();
    inline HeroSkillPresetStore heroSkillPresets;
    inline int targetPriority = 0;

    // UI-only placeholders for heroes not present in the current local eHero enum.
    inline constexpr uint64_t HERO_PRESET_FREJA  = 0xFFFFFFFFFFFF0001ull;
    inline constexpr uint64_t HERO_PRESET_HAZARD = 0xFFFFFFFFFFFF0002ull;
    inline constexpr uint64_t HERO_PRESET_JUNO   = 0xFFFFFFFFFFFF0003ull;

    // ---- Target-machine display fallback ----
    // Used only when the target viewport cannot be read through DMA.
    inline int manualScreenWidth = 1920;
    inline int manualScreenHeight = 1080;

    // ---- Crosshair tracking circle ----
    inline int locx = 0, locy = 0, therad = 0, pon = 0, crss = 0;

    // ---- Persistence ----
    HeroPreset MakeHeroPresetFromCurrent();
    HeroPreset MakeHeroAimPresetFromCurrent();
    HeroPreset MakeHeroTriggerPresetFromCurrent();
    bool TryGetHeroPreset(uint64_t heroId, HeroPreset& outPreset);
    bool TryGetHeroAimPreset(uint64_t heroId, HeroPreset& outPreset);
    bool TryGetHeroTriggerPreset(uint64_t heroId, HeroPreset& outPreset);
    bool HasHeroPreset(uint64_t heroId);
    bool HasHeroAimPreset(uint64_t heroId);
    bool HasHeroTriggerPreset(uint64_t heroId);
    HeroPreset GetHeroPresetOrDefault(uint64_t heroId);
    HeroPreset GetHeroPresetOrDefault(uint64_t heroId, int slotIndex);
    HeroPreset GetHeroAimPresetOrDefault(uint64_t heroId, int slotIndex);
    HeroPreset GetHeroTriggerPresetOrDefault(uint64_t heroId, int slotIndex);
    void SetHeroPreset(uint64_t heroId, const HeroPreset& preset);
    void SetHeroPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset);
    void SetHeroAimPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset);
    void SetHeroTriggerPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset);
    inline bool HeroUsesScopedWeaponActionSplit(uint64_t heroId)
    {
        switch (heroId) {
        case OW::GameData::MakeHeroId(0x00A): // Widowmaker
        case OW::GameData::MakeHeroId(0x13B): // Ana
        case OW::GameData::MakeHeroId(0x200): // Ashe
        case OW::GameData::MakeHeroId(0x32A): // Freja
            return true;
        default:
            return false;
        }
    }

    inline AimScopeRequirement DefaultAimScopeRequirementForHeroAction(uint64_t heroId,
                                                                       int action)
    {
        return HeroUsesScopedWeaponActionSplit(heroId) && action == 2
            ? AimScopeRequirement::ScopedOnly
            : AimScopeRequirement::All;
    }

    void SetHeroAimSlotScopeRequirement(uint64_t heroId,
                                        int slotIndex,
                                        AimScopeRequirement requirement);
    std::string GetHeroSlotName(uint64_t heroId, int slotIndex);
    std::string GetHeroAimSlotName(uint64_t heroId, int slotIndex);
    std::string GetHeroTriggerSlotName(uint64_t heroId, int slotIndex);
    bool TryGetHeroAimSlot(uint64_t heroId, int slotIndex, HeroSlotPreset& outSlot);
    bool TryGetHeroTriggerSlot(uint64_t heroId, int slotIndex, HeroSlotPreset& outSlot);
    bool IsHeroSlotEnabled(uint64_t heroId, int slotIndex);
    bool IsHeroAimSlotEnabled(uint64_t heroId, int slotIndex);
    bool IsHeroTriggerSlotEnabled(uint64_t heroId, int slotIndex);
    void SetHeroSlotEnabled(uint64_t heroId, int slotIndex, bool enabled);
    void SetHeroAimSlotEnabled(uint64_t heroId, int slotIndex, bool enabled);
    void SetHeroTriggerSlotEnabled(uint64_t heroId, int slotIndex, bool enabled);
    int GetHeroAimSlotCount(uint64_t heroId);
    int GetHeroTriggerSlotCount(uint64_t heroId);
    int AddHeroAimSlot(uint64_t heroId, const HeroPreset& seedPreset);
    int AddHeroTriggerSlot(uint64_t heroId, const HeroPreset& seedPreset);
    bool DeleteHeroAimSlot(uint64_t heroId, int slotIndex);
    bool DeleteHeroTriggerSlot(uint64_t heroId, int slotIndex);
    void NormalizeHeroPresets();
    void ApplyHeroPresetToGlobals(const HeroPreset& preset);
    void ApplyHeroAimPresetToGlobals(const HeroPreset& preset);
    void ApplyHeroTriggerPresetToGlobals(const HeroPreset& preset);
    HeroPreset GetSystemHeroPreset();
    void SetSystemAimPreset(const HeroPreset& preset);
    void SetSystemTriggerPreset(const HeroPreset& preset);
    bool SaveHeroConfig(const std::string& path);
    bool SaveHeroConfigForHero(const std::string& path, uint64_t heroId);
    void LoadHeroConfig(const std::string& path);
    void LoadHeroSkillConfig(const std::string& path);
    bool SaveHeroSkillConfig(const std::string& path);
    bool TryGetHeroSkillSettings(uint64_t heroId, const std::string& skillId, HeroSkillSettings& outSettings);
    HeroSkillSettings GetHeroSkillSettings(uint64_t heroId, const std::string& skillId);
    HeroSkillSettings GetHeroSkillSettings(uint64_t heroId,
                                           const std::string& skillId,
                                           const HeroSkillSettings& defaultSettings);
    void SetHeroSkillSettings(uint64_t heroId, const std::string& skillId, const HeroSkillSettings& settings);
    void SaveHeroPresets(const std::string& path);
    void LoadHeroPresets(const std::string& path);
    bool SaveConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
    void LoadConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
    void NormalizeOutputPorts();
    int EffectiveOutputMonitorPort();
    bool SaveConfig(const std::string& path);
    bool SaveAllConfig(const std::string& path);
    enum class AutoSaveResult { Idle, Saved, Failed };
    AutoSaveResult PollAutoSave(const std::string& path, uint64_t nowMs, bool flush = false);
    bool CreateConfigProfileFromCurrent(const std::string& path);
    // Failed leaves live values untouched; MigrationSaveFailed has loaded the new profile.
    enum class ConfigLoadResult { Failed, Loaded, MigrationSaveFailed };
    ConfigLoadResult LoadConfig(const std::string& path);


    // Authority is written only under Config::mutex. Published versions own all data.
    struct ConfigValues {
        std::uint64_t version = 0;
        AimModeRuntime aimModeState{};
        int nextAimBehaviorPresetId=1;
        decltype(Config::Bone) Bone;
        decltype(Config::BoneName) BoneName;
        decltype(Config::Flick) Flick;
        decltype(Config::Flick_smooth) Flick_smooth;
        decltype(Config::Fov) Fov;
        decltype(Config::Fov2) Fov2;
        decltype(Config::Prediction) Prediction;
        decltype(Config::Smooth) Smooth;
        decltype(Config::TargetBone) TargetBone;
        decltype(Config::Tracking) Tracking;
        decltype(Config::Tracking_smooth) Tracking_smooth;
        decltype(Config::aimBehavior) aimBehavior;
        decltype(Config::aimBehaviorMethod) aimBehaviorMethod;
        decltype(Config::aimBehaviorMethodPreset) aimBehaviorMethodPreset;
        decltype(Config::aimBehaviorPresetId) aimBehaviorPresetId;
        decltype(Config::aimBehaviorPresets) aimBehaviorPresets;
        decltype(Config::aimBezierControlPoints) aimBezierControlPoints;
        decltype(Config::aimBezierCurvature) aimBezierCurvature;
        decltype(Config::aimBezierSpeed) aimBezierSpeed;
        decltype(Config::aimMethod) aimMethod;
        decltype(Config::aimMethodPresets) aimMethodPresets;
        decltype(Config::aimPidD) aimPidD;
        decltype(Config::aimPidDeadzone) aimPidDeadzone;
        decltype(Config::aimPidI) aimPidI;
        decltype(Config::aimPidMaxIntegral) aimPidMaxIntegral;
        decltype(Config::aimPidP) aimPidP;
        decltype(Config::aim_key) aim_key;
        decltype(Config::aimbotAttack) aimbotAttack;
        decltype(Config::aimbotAutoshot) aimbotAutoshot;
        decltype(Config::aimbotBoneMask) aimbotBoneMask;
        decltype(Config::aimbotDynamicFovPresetId) aimbotDynamicFovPresetId;
        decltype(Config::aimbotFirePolicy) aimbotFirePolicy;
        decltype(Config::aimbotFlick2ndBoxPadding) aimbotFlick2ndBoxPadding;
        decltype(Config::aimbotFlick2ndInnerMethod) aimbotFlick2ndInnerMethod;
        decltype(Config::aimbotFlick2ndInnerRadius) aimbotFlick2ndInnerRadius;
        decltype(Config::aimbotFlick2ndInnerSmoothScale) aimbotFlick2ndInnerSmoothScale;
        decltype(Config::aimbotFlick2ndTriggerGate) aimbotFlick2ndTriggerGate;
        decltype(Config::aimbotFlickPostFireDelayMs) aimbotFlickPostFireDelayMs;
        decltype(Config::aimbotFlickShotClampMs) aimbotFlickShotClampMs;
        decltype(Config::aimbotFlickTrajectoryApexWindowMs) aimbotFlickTrajectoryApexWindowMs;
        decltype(Config::aimbotFlickTrajectoryWait) aimbotFlickTrajectoryWait;
        decltype(Config::aimbotFlickTrajectoryWaitMs) aimbotFlickTrajectoryWaitMs;
        decltype(Config::aimbotFovEntryMaxOutsideDeg) aimbotFovEntryMaxOutsideDeg;
        decltype(Config::aimbotFovEntryPredictionMs) aimbotFovEntryPredictionMs;
        decltype(Config::aimbotFovMode) aimbotFovMode;
        decltype(Config::aimbotIgnoreInvisible) aimbotIgnoreInvisible;
        decltype(Config::aimbotKeepFiring) aimbotKeepFiring;
        decltype(Config::aimbotLockTime) aimbotLockTime;
        decltype(Config::aimbotMagneticPostFirePauseMs) aimbotMagneticPostFirePauseMs;
        decltype(Config::aimbotMagneticPostFirePitchScale) aimbotMagneticPostFirePitchScale;
        decltype(Config::aimbotMagneticPostFireYawScale) aimbotMagneticPostFireYawScale;
        decltype(Config::aimbotMagneticPreFireBoostScale) aimbotMagneticPreFireBoostScale;
        decltype(Config::aimbotMagneticPreFireBoostWindowMs) aimbotMagneticPreFireBoostWindowMs;
        decltype(Config::aimbotMagneticRecoveryMs) aimbotMagneticRecoveryMs;
        decltype(Config::aimbotMagneticShotIntervalMs) aimbotMagneticShotIntervalMs;
        decltype(Config::aimbotMaxAim) aimbotMaxAim;
        decltype(Config::aimbotMaxCharge) aimbotMaxCharge;
        decltype(Config::aimbotMaxDist) aimbotMaxDist;
        decltype(Config::aimbotMaxHead) aimbotMaxHead;
        decltype(Config::aimbotMinCharge) aimbotMinCharge;
        decltype(Config::aimbotMinDist) aimbotMinDist;
        decltype(Config::aimbotPitchScale) aimbotPitchScale;
        decltype(Config::aimbotPredictFovEntry) aimbotPredictFovEntry;
        decltype(Config::aimbotPredictionMode) aimbotPredictionMode;
        AimPlan::Plan aimPlan;
        decltype(Config::aimbotPriority) aimbotPriority;
        decltype(Config::aimbotRequireActionHeld) aimbotRequireActionHeld;
        decltype(Config::aimbotSmoothType) aimbotSmoothType;
        decltype(Config::aimbotStickiness) aimbotStickiness;
        decltype(Config::aimbotTeam) aimbotTeam;
        decltype(Config::aimbotTrace) aimbotTrace;
        decltype(Config::aimbotTrackingDeadzone) aimbotTrackingDeadzone;
        decltype(Config::aimbotTwoStage) aimbotTwoStage;
        decltype(Config::aimbotTwoStageBoxPadding) aimbotTwoStageBoxPadding;
        decltype(Config::aimbotTwoStageInnerRadius) aimbotTwoStageInnerRadius;
        decltype(Config::aimbotTwoStageInnerSmoothScale) aimbotTwoStageInnerSmoothScale;
        decltype(Config::aimbotTwoStageTriggerGate) aimbotTwoStageTriggerGate;
        decltype(Config::aimbotUnlock) aimbotUnlock;
        decltype(Config::autobone) autobone;
        decltype(Config::autoscalefov) autoscalefov;
        decltype(Config::dynamicFovPresets) dynamicFovPresets;
        decltype(Config::heroAimPresets) heroAimPresets;
        decltype(Config::heroTriggerPresets) heroTriggerPresets;
        decltype(Config::hitbox) hitbox;
        decltype(Config::minFov1) minFov1;
        decltype(Config::minFov2) minFov2;
        decltype(Config::triggerbot) triggerbot;
        decltype(Config::triggerbot2) triggerbot2;
        decltype(Config::triggerbotBoneMask) triggerbotBoneMask;
        decltype(Config::triggerbotChargeAware) triggerbotChargeAware;
        decltype(Config::triggerbotDisableWhileReloading) triggerbotDisableWhileReloading;
        decltype(Config::triggerbotIgnoreInvisible) triggerbotIgnoreInvisible;
        decltype(Config::triggerbotKey) triggerbotKey;
        decltype(Config::triggerbotMinCharge) triggerbotMinCharge;
        decltype(Config::triggerbotMode) triggerbotMode;
        decltype(Config::triggerbotShotInterval) triggerbotShotInterval;
    };
    inline std::atomic<std::shared_ptr<const ConfigValues>> publishedConfigValues;
    inline std::uint64_t configValuesVersion = 0; // guarded by mutex
    inline ConfigValues CaptureConfigValuesUnlocked() {
        ConfigValues value;
        value.version = configValuesVersion;
        value.Bone = Bone;
        value.BoneName = BoneName;
        value.Flick = Flick;
        value.Flick_smooth = Flick_smooth;
        value.Fov = Fov;
        value.Fov2 = Fov2;
        value.Prediction = Prediction;
        value.Smooth = Smooth;
        value.TargetBone = TargetBone;
        value.Tracking = Tracking;
        value.Tracking_smooth = Tracking_smooth;
        value.aimBehavior = aimBehavior;
        value.aimBehaviorMethod = aimBehaviorMethod;
        value.aimBehaviorMethodPreset = aimBehaviorMethodPreset;
        value.aimBehaviorPresetId = aimBehaviorPresetId;
        value.aimBehaviorPresets = aimBehaviorPresets;
        value.aimBezierControlPoints = aimBezierControlPoints;
        value.aimBezierCurvature = aimBezierCurvature;
        value.aimBezierSpeed = aimBezierSpeed;
        value.nextAimBehaviorPresetId=nextAimBehaviorPresetId;
        value.aimModeState = aimModeState;
        value.aimMethod = aimMethod;
        value.aimMethodPresets = aimMethodPresets;
        value.aimPidD = aimPidD;
        value.aimPidDeadzone = aimPidDeadzone;
        value.aimPidI = aimPidI;
        value.aimPidMaxIntegral = aimPidMaxIntegral;
        value.aimPidP = aimPidP;
        value.aim_key = aim_key;
        value.aimbotAttack = aimbotAttack;
        value.aimbotAutoshot = aimbotAutoshot;
        value.aimbotBoneMask = aimbotBoneMask;
        value.aimbotDynamicFovPresetId = aimbotDynamicFovPresetId;
        value.aimbotFirePolicy = aimbotFirePolicy;
        value.aimbotFlick2ndBoxPadding = aimbotFlick2ndBoxPadding;
        value.aimbotFlick2ndInnerMethod = aimbotFlick2ndInnerMethod;
        value.aimbotFlick2ndInnerRadius = aimbotFlick2ndInnerRadius;
        value.aimbotFlick2ndInnerSmoothScale = aimbotFlick2ndInnerSmoothScale;
        value.aimbotFlick2ndTriggerGate = aimbotFlick2ndTriggerGate;
        value.aimbotFlickPostFireDelayMs = aimbotFlickPostFireDelayMs;
        value.aimbotFlickShotClampMs = aimbotFlickShotClampMs;
        value.aimbotFlickTrajectoryApexWindowMs = aimbotFlickTrajectoryApexWindowMs;
        value.aimbotFlickTrajectoryWait = aimbotFlickTrajectoryWait;
        value.aimbotFlickTrajectoryWaitMs = aimbotFlickTrajectoryWaitMs;
        value.aimbotFovEntryMaxOutsideDeg = aimbotFovEntryMaxOutsideDeg;
        value.aimbotFovEntryPredictionMs = aimbotFovEntryPredictionMs;
        value.aimbotFovMode = aimbotFovMode;
        value.aimbotIgnoreInvisible = aimbotIgnoreInvisible;
        value.aimbotKeepFiring = aimbotKeepFiring;
        value.aimbotLockTime = aimbotLockTime;
        value.aimbotMagneticPostFirePauseMs = aimbotMagneticPostFirePauseMs;
        value.aimbotMagneticPostFirePitchScale = aimbotMagneticPostFirePitchScale;
        value.aimbotMagneticPostFireYawScale = aimbotMagneticPostFireYawScale;
        value.aimbotMagneticPreFireBoostScale = aimbotMagneticPreFireBoostScale;
        value.aimbotMagneticPreFireBoostWindowMs = aimbotMagneticPreFireBoostWindowMs;
        value.aimbotMagneticRecoveryMs = aimbotMagneticRecoveryMs;
        value.aimbotMagneticShotIntervalMs = aimbotMagneticShotIntervalMs;
        value.aimbotMaxAim = aimbotMaxAim;
        value.aimbotMaxCharge = aimbotMaxCharge;
        value.aimbotMaxDist = aimbotMaxDist;
        value.aimbotMaxHead = aimbotMaxHead;
        value.aimbotMinCharge = aimbotMinCharge;
        value.aimbotMinDist = aimbotMinDist;
        value.aimbotPitchScale = aimbotPitchScale;
        value.aimbotPredictFovEntry = aimbotPredictFovEntry;
        value.aimbotPredictionMode = aimbotPredictionMode;
        value.aimPlan = aimPlan;
        value.aimbotPriority = aimbotPriority;
        value.aimbotRequireActionHeld = aimbotRequireActionHeld;
        value.aimbotSmoothType = aimbotSmoothType;
        value.aimbotStickiness = aimbotStickiness;
        value.aimbotTeam = aimbotTeam;
        value.aimbotTrace = aimbotTrace;
        value.aimbotTrackingDeadzone = aimbotTrackingDeadzone;
        value.aimbotTwoStage = aimbotTwoStage;
        value.aimbotTwoStageBoxPadding = aimbotTwoStageBoxPadding;
        value.aimbotTwoStageInnerRadius = aimbotTwoStageInnerRadius;
        value.aimbotTwoStageInnerSmoothScale = aimbotTwoStageInnerSmoothScale;
        value.aimbotTwoStageTriggerGate = aimbotTwoStageTriggerGate;
        value.aimbotUnlock = aimbotUnlock;
        value.autobone = autobone;
        value.autoscalefov = autoscalefov;
        value.dynamicFovPresets = dynamicFovPresets;
        value.heroAimPresets = heroAimPresets;
        value.heroTriggerPresets = heroTriggerPresets;
        value.hitbox = hitbox;
        value.minFov1 = minFov1;
        value.minFov2 = minFov2;
        value.triggerbot = triggerbot;
        value.triggerbot2 = triggerbot2;
        value.triggerbotBoneMask = triggerbotBoneMask;
        value.triggerbotChargeAware = triggerbotChargeAware;
        value.triggerbotDisableWhileReloading = triggerbotDisableWhileReloading;
        value.triggerbotIgnoreInvisible = triggerbotIgnoreInvisible;
        value.triggerbotKey = triggerbotKey;
        value.triggerbotMinCharge = triggerbotMinCharge;
        value.triggerbotMode = triggerbotMode;
        value.triggerbotShotInterval = triggerbotShotInterval;
        return value;
    }
    inline void PublishConfigValuesUnlocked() {
        ++configValuesVersion;
        auto value = std::make_shared<const ConfigValues>(CaptureConfigValuesUnlocked());
        publishedConfigValues.store(std::move(value));
    }
    inline std::shared_ptr<const ConfigValues> SnapshotConfigValues() {
        auto value = publishedConfigValues.load();
        if (!value) {
            std::lock_guard<std::mutex> lock(mutex);
            value = publishedConfigValues.load();
            if (!value) {
                PublishConfigValuesUnlocked();
                value = publishedConfigValues.load();
            }
        }
        return value;
    }
    inline ConfigValues& CurrentConfigValues() {
        if (configView) return *configView;
        // Single-value compatibility reads. Multi-value consumers establish a scope.
        thread_local ConfigValues single;
        single = *SnapshotConfigValues();
        return single;
    }
    inline decltype(Bone)& View_Bone() { return configAuthority ? Bone : CurrentConfigValues().Bone; }
    inline decltype(BoneName)& View_BoneName() { return configAuthority ? BoneName : CurrentConfigValues().BoneName; }
    inline decltype(Flick)& View_Flick() { return configAuthority ? Flick : CurrentConfigValues().Flick; }
    inline decltype(Flick_smooth)& View_Flick_smooth() { return configAuthority ? Flick_smooth : CurrentConfigValues().Flick_smooth; }
    inline decltype(Fov)& View_Fov() { return configAuthority ? Fov : CurrentConfigValues().Fov; }
    inline decltype(Fov2)& View_Fov2() { return configAuthority ? Fov2 : CurrentConfigValues().Fov2; }
    inline decltype(Prediction)& View_Prediction() { return configAuthority ? Prediction : CurrentConfigValues().Prediction; }
    inline decltype(Smooth)& View_Smooth() { return configAuthority ? Smooth : CurrentConfigValues().Smooth; }
    inline decltype(TargetBone)& View_TargetBone() { return configAuthority ? TargetBone : CurrentConfigValues().TargetBone; }
    inline decltype(Tracking)& View_Tracking() { return configAuthority ? Tracking : CurrentConfigValues().Tracking; }
    inline decltype(Tracking_smooth)& View_Tracking_smooth() { return configAuthority ? Tracking_smooth : CurrentConfigValues().Tracking_smooth; }
    inline decltype(aimBehavior)& View_aimBehavior() { return configAuthority ? aimBehavior : CurrentConfigValues().aimBehavior; }
    inline decltype(aimBehaviorMethod)& View_aimBehaviorMethod() { return configAuthority ? aimBehaviorMethod : CurrentConfigValues().aimBehaviorMethod; }
    inline decltype(aimBehaviorMethodPreset)& View_aimBehaviorMethodPreset() { return configAuthority ? aimBehaviorMethodPreset : CurrentConfigValues().aimBehaviorMethodPreset; }
    inline decltype(aimBehaviorPresetId)& View_aimBehaviorPresetId() { return configAuthority ? aimBehaviorPresetId : CurrentConfigValues().aimBehaviorPresetId; }
    inline decltype(aimBehaviorPresets)& View_aimBehaviorPresets() { return configAuthority ? aimBehaviorPresets : CurrentConfigValues().aimBehaviorPresets; }
    inline decltype(aimBezierControlPoints)& View_aimBezierControlPoints() { return configAuthority ? aimBezierControlPoints : CurrentConfigValues().aimBezierControlPoints; }
    inline decltype(aimBezierCurvature)& View_aimBezierCurvature() { return configAuthority ? aimBezierCurvature : CurrentConfigValues().aimBezierCurvature; }
    inline decltype(aimBezierSpeed)& View_aimBezierSpeed() { return configAuthority ? aimBezierSpeed : CurrentConfigValues().aimBezierSpeed; }
    inline decltype(aimMethod)& View_aimMethod() { return configAuthority ? aimMethod : CurrentConfigValues().aimMethod; }
    inline decltype(aimMethodPresets)& View_aimMethodPresets() { return configAuthority ? aimMethodPresets : CurrentConfigValues().aimMethodPresets; }
    inline decltype(aimPidD)& View_aimPidD() { return configAuthority ? aimPidD : CurrentConfigValues().aimPidD; }
    inline decltype(aimPidDeadzone)& View_aimPidDeadzone() { return configAuthority ? aimPidDeadzone : CurrentConfigValues().aimPidDeadzone; }
    inline decltype(aimPidI)& View_aimPidI() { return configAuthority ? aimPidI : CurrentConfigValues().aimPidI; }
    inline decltype(aimPidMaxIntegral)& View_aimPidMaxIntegral() { return configAuthority ? aimPidMaxIntegral : CurrentConfigValues().aimPidMaxIntegral; }
    inline decltype(aimPidP)& View_aimPidP() { return configAuthority ? aimPidP : CurrentConfigValues().aimPidP; }
    inline decltype(aim_key)& View_aim_key() { return configAuthority ? aim_key : CurrentConfigValues().aim_key; }
    inline decltype(aimbotAttack)& View_aimbotAttack() { return configAuthority ? aimbotAttack : CurrentConfigValues().aimbotAttack; }
    inline decltype(aimbotAutoshot)& View_aimbotAutoshot() { return configAuthority ? aimbotAutoshot : CurrentConfigValues().aimbotAutoshot; }
    inline decltype(aimbotBoneMask)& View_aimbotBoneMask() { return configAuthority ? aimbotBoneMask : CurrentConfigValues().aimbotBoneMask; }
    inline decltype(aimbotDynamicFovPresetId)& View_aimbotDynamicFovPresetId() { return configAuthority ? aimbotDynamicFovPresetId : CurrentConfigValues().aimbotDynamicFovPresetId; }
    inline decltype(aimbotFirePolicy)& View_aimbotFirePolicy() { return configAuthority ? aimbotFirePolicy : CurrentConfigValues().aimbotFirePolicy; }
    inline decltype(aimbotFlick2ndBoxPadding)& View_aimbotFlick2ndBoxPadding() { return configAuthority ? aimbotFlick2ndBoxPadding : CurrentConfigValues().aimbotFlick2ndBoxPadding; }
    inline decltype(aimbotFlick2ndInnerMethod)& View_aimbotFlick2ndInnerMethod() { return configAuthority ? aimbotFlick2ndInnerMethod : CurrentConfigValues().aimbotFlick2ndInnerMethod; }
    inline decltype(aimbotFlick2ndInnerRadius)& View_aimbotFlick2ndInnerRadius() { return configAuthority ? aimbotFlick2ndInnerRadius : CurrentConfigValues().aimbotFlick2ndInnerRadius; }
    inline decltype(aimbotFlick2ndInnerSmoothScale)& View_aimbotFlick2ndInnerSmoothScale() { return configAuthority ? aimbotFlick2ndInnerSmoothScale : CurrentConfigValues().aimbotFlick2ndInnerSmoothScale; }
    inline decltype(aimbotFlick2ndTriggerGate)& View_aimbotFlick2ndTriggerGate() { return configAuthority ? aimbotFlick2ndTriggerGate : CurrentConfigValues().aimbotFlick2ndTriggerGate; }
    inline decltype(aimbotFlickPostFireDelayMs)& View_aimbotFlickPostFireDelayMs() { return configAuthority ? aimbotFlickPostFireDelayMs : CurrentConfigValues().aimbotFlickPostFireDelayMs; }
    inline decltype(aimbotFlickShotClampMs)& View_aimbotFlickShotClampMs() { return configAuthority ? aimbotFlickShotClampMs : CurrentConfigValues().aimbotFlickShotClampMs; }
    inline decltype(aimbotFlickTrajectoryApexWindowMs)& View_aimbotFlickTrajectoryApexWindowMs() { return configAuthority ? aimbotFlickTrajectoryApexWindowMs : CurrentConfigValues().aimbotFlickTrajectoryApexWindowMs; }
    inline decltype(aimbotFlickTrajectoryWait)& View_aimbotFlickTrajectoryWait() { return configAuthority ? aimbotFlickTrajectoryWait : CurrentConfigValues().aimbotFlickTrajectoryWait; }
    inline decltype(aimbotFlickTrajectoryWaitMs)& View_aimbotFlickTrajectoryWaitMs() { return configAuthority ? aimbotFlickTrajectoryWaitMs : CurrentConfigValues().aimbotFlickTrajectoryWaitMs; }
    inline decltype(aimbotFovEntryMaxOutsideDeg)& View_aimbotFovEntryMaxOutsideDeg() { return configAuthority ? aimbotFovEntryMaxOutsideDeg : CurrentConfigValues().aimbotFovEntryMaxOutsideDeg; }
    inline decltype(aimbotFovEntryPredictionMs)& View_aimbotFovEntryPredictionMs() { return configAuthority ? aimbotFovEntryPredictionMs : CurrentConfigValues().aimbotFovEntryPredictionMs; }
    inline decltype(aimbotFovMode)& View_aimbotFovMode() { return configAuthority ? aimbotFovMode : CurrentConfigValues().aimbotFovMode; }
    inline decltype(aimbotIgnoreInvisible)& View_aimbotIgnoreInvisible() { return configAuthority ? aimbotIgnoreInvisible : CurrentConfigValues().aimbotIgnoreInvisible; }
    inline decltype(aimbotKeepFiring)& View_aimbotKeepFiring() { return configAuthority ? aimbotKeepFiring : CurrentConfigValues().aimbotKeepFiring; }
    inline decltype(aimbotLockTime)& View_aimbotLockTime() { return configAuthority ? aimbotLockTime : CurrentConfigValues().aimbotLockTime; }
    inline decltype(aimbotMagneticPostFirePauseMs)& View_aimbotMagneticPostFirePauseMs() { return configAuthority ? aimbotMagneticPostFirePauseMs : CurrentConfigValues().aimbotMagneticPostFirePauseMs; }
    inline decltype(aimbotMagneticPostFirePitchScale)& View_aimbotMagneticPostFirePitchScale() { return configAuthority ? aimbotMagneticPostFirePitchScale : CurrentConfigValues().aimbotMagneticPostFirePitchScale; }
    inline decltype(aimbotMagneticPostFireYawScale)& View_aimbotMagneticPostFireYawScale() { return configAuthority ? aimbotMagneticPostFireYawScale : CurrentConfigValues().aimbotMagneticPostFireYawScale; }
    inline decltype(aimbotMagneticPreFireBoostScale)& View_aimbotMagneticPreFireBoostScale() { return configAuthority ? aimbotMagneticPreFireBoostScale : CurrentConfigValues().aimbotMagneticPreFireBoostScale; }
    inline decltype(aimbotMagneticPreFireBoostWindowMs)& View_aimbotMagneticPreFireBoostWindowMs() { return configAuthority ? aimbotMagneticPreFireBoostWindowMs : CurrentConfigValues().aimbotMagneticPreFireBoostWindowMs; }
    inline decltype(aimbotMagneticRecoveryMs)& View_aimbotMagneticRecoveryMs() { return configAuthority ? aimbotMagneticRecoveryMs : CurrentConfigValues().aimbotMagneticRecoveryMs; }
    inline decltype(aimbotMagneticShotIntervalMs)& View_aimbotMagneticShotIntervalMs() { return configAuthority ? aimbotMagneticShotIntervalMs : CurrentConfigValues().aimbotMagneticShotIntervalMs; }
    inline decltype(aimbotMaxAim)& View_aimbotMaxAim() { return configAuthority ? aimbotMaxAim : CurrentConfigValues().aimbotMaxAim; }
    inline decltype(aimbotMaxCharge)& View_aimbotMaxCharge() { return configAuthority ? aimbotMaxCharge : CurrentConfigValues().aimbotMaxCharge; }
    inline decltype(aimbotMaxDist)& View_aimbotMaxDist() { return configAuthority ? aimbotMaxDist : CurrentConfigValues().aimbotMaxDist; }
    inline decltype(aimbotMaxHead)& View_aimbotMaxHead() { return configAuthority ? aimbotMaxHead : CurrentConfigValues().aimbotMaxHead; }
    inline decltype(aimbotMinCharge)& View_aimbotMinCharge() { return configAuthority ? aimbotMinCharge : CurrentConfigValues().aimbotMinCharge; }
    inline decltype(aimbotMinDist)& View_aimbotMinDist() { return configAuthority ? aimbotMinDist : CurrentConfigValues().aimbotMinDist; }
    inline decltype(aimbotPitchScale)& View_aimbotPitchScale() { return configAuthority ? aimbotPitchScale : CurrentConfigValues().aimbotPitchScale; }
    inline decltype(aimbotPredictFovEntry)& View_aimbotPredictFovEntry() { return configAuthority ? aimbotPredictFovEntry : CurrentConfigValues().aimbotPredictFovEntry; }
    inline decltype(aimbotPredictionMode)& View_aimbotPredictionMode() { return configAuthority ? aimbotPredictionMode : CurrentConfigValues().aimbotPredictionMode; }
    inline AimPlan::Plan& View_aimPlan() { return configAuthority ? aimPlan : CurrentConfigValues().aimPlan; }
    inline decltype(aimbotPriority)& View_aimbotPriority() { return configAuthority ? aimbotPriority : CurrentConfigValues().aimbotPriority; }
    inline decltype(aimbotRequireActionHeld)& View_aimbotRequireActionHeld() { return configAuthority ? aimbotRequireActionHeld : CurrentConfigValues().aimbotRequireActionHeld; }
    inline decltype(aimbotSmoothType)& View_aimbotSmoothType() { return configAuthority ? aimbotSmoothType : CurrentConfigValues().aimbotSmoothType; }
    inline decltype(aimbotStickiness)& View_aimbotStickiness() { return configAuthority ? aimbotStickiness : CurrentConfigValues().aimbotStickiness; }
    inline decltype(aimbotTeam)& View_aimbotTeam() { return configAuthority ? aimbotTeam : CurrentConfigValues().aimbotTeam; }
    inline decltype(aimbotTrace)& View_aimbotTrace() { return configAuthority ? aimbotTrace : CurrentConfigValues().aimbotTrace; }
    inline decltype(aimbotTrackingDeadzone)& View_aimbotTrackingDeadzone() { return configAuthority ? aimbotTrackingDeadzone : CurrentConfigValues().aimbotTrackingDeadzone; }
    inline decltype(aimbotTwoStage)& View_aimbotTwoStage() { return configAuthority ? aimbotTwoStage : CurrentConfigValues().aimbotTwoStage; }
    inline decltype(aimbotTwoStageBoxPadding)& View_aimbotTwoStageBoxPadding() { return configAuthority ? aimbotTwoStageBoxPadding : CurrentConfigValues().aimbotTwoStageBoxPadding; }
    inline decltype(aimbotTwoStageInnerRadius)& View_aimbotTwoStageInnerRadius() { return configAuthority ? aimbotTwoStageInnerRadius : CurrentConfigValues().aimbotTwoStageInnerRadius; }
    inline decltype(aimbotTwoStageInnerSmoothScale)& View_aimbotTwoStageInnerSmoothScale() { return configAuthority ? aimbotTwoStageInnerSmoothScale : CurrentConfigValues().aimbotTwoStageInnerSmoothScale; }
    inline decltype(aimbotTwoStageTriggerGate)& View_aimbotTwoStageTriggerGate() { return configAuthority ? aimbotTwoStageTriggerGate : CurrentConfigValues().aimbotTwoStageTriggerGate; }
    inline decltype(aimbotUnlock)& View_aimbotUnlock() { return configAuthority ? aimbotUnlock : CurrentConfigValues().aimbotUnlock; }
    inline decltype(autobone)& View_autobone() { return configAuthority ? autobone : CurrentConfigValues().autobone; }
    inline decltype(autoscalefov)& View_autoscalefov() { return configAuthority ? autoscalefov : CurrentConfigValues().autoscalefov; }
    inline decltype(dynamicFovPresets)& View_dynamicFovPresets() { return configAuthority ? dynamicFovPresets : CurrentConfigValues().dynamicFovPresets; }
    inline decltype(heroAimPresets)& View_heroAimPresets() { return configAuthority ? heroAimPresets : CurrentConfigValues().heroAimPresets; }
    inline decltype(heroTriggerPresets)& View_heroTriggerPresets() { return configAuthority ? heroTriggerPresets : CurrentConfigValues().heroTriggerPresets; }
    inline decltype(hitbox)& View_hitbox() { return configAuthority ? hitbox : CurrentConfigValues().hitbox; }
    inline decltype(minFov1)& View_minFov1() { return configAuthority ? minFov1 : CurrentConfigValues().minFov1; }
    inline decltype(minFov2)& View_minFov2() { return configAuthority ? minFov2 : CurrentConfigValues().minFov2; }
    inline decltype(triggerbot)& View_triggerbot() { return configAuthority ? triggerbot : CurrentConfigValues().triggerbot; }
    inline decltype(triggerbot2)& View_triggerbot2() { return configAuthority ? triggerbot2 : CurrentConfigValues().triggerbot2; }
    inline decltype(triggerbotBoneMask)& View_triggerbotBoneMask() { return configAuthority ? triggerbotBoneMask : CurrentConfigValues().triggerbotBoneMask; }
    inline decltype(triggerbotChargeAware)& View_triggerbotChargeAware() { return configAuthority ? triggerbotChargeAware : CurrentConfigValues().triggerbotChargeAware; }
    inline decltype(triggerbotDisableWhileReloading)& View_triggerbotDisableWhileReloading() { return configAuthority ? triggerbotDisableWhileReloading : CurrentConfigValues().triggerbotDisableWhileReloading; }
    inline decltype(triggerbotIgnoreInvisible)& View_triggerbotIgnoreInvisible() { return configAuthority ? triggerbotIgnoreInvisible : CurrentConfigValues().triggerbotIgnoreInvisible; }
    inline decltype(triggerbotKey)& View_triggerbotKey() { return configAuthority ? triggerbotKey : CurrentConfigValues().triggerbotKey; }
    inline decltype(triggerbotMinCharge)& View_triggerbotMinCharge() { return configAuthority ? triggerbotMinCharge : CurrentConfigValues().triggerbotMinCharge; }
    inline decltype(triggerbotMode)& View_triggerbotMode() { return configAuthority ? triggerbotMode : CurrentConfigValues().triggerbotMode; }
    inline decltype(triggerbotShotInterval)& View_triggerbotShotInterval() { return configAuthority ? triggerbotShotInterval : CurrentConfigValues().triggerbotShotInterval; }
    inline int& View_nextAimBehaviorPresetId() {return configAuthority?nextAimBehaviorPresetId:CurrentConfigValues().nextAimBehaviorPresetId;}
    inline AimModeRuntime& View_aimModeState() { return configAuthority ? aimModeState : CurrentConfigValues().aimModeState; }
    inline const AimBehaviorTuning* ActiveSlotBehaviorTuning() { return View_aimModeState().enabled ? &View_aimModeState().tuning : nullptr; }
    bool AimBehaviorPresetInUse(int id);
    void RefreshBehaviorModeSlots();
    inline ScopedConfigRead::ScopedConfigRead() : previous(configView) {
        if (!previous && !configAuthority) { value = std::make_unique<ConfigValues>(*SnapshotConfigValues()); configView = value.get(); }
    }
    inline ScopedConfigRead::~ScopedConfigRead() { configView = previous; }
    void ApplyLocalHeroAimPreset(const HeroPreset& preset);
    void ApplyLocalHeroTriggerPreset(const HeroPreset& preset);
    struct ScopedLocalConfigOverride {
        ScopedConfigRead read;
        ConfigValues original = CurrentConfigValues();
        ~ScopedLocalConfigOverride() { CurrentConfigValues() = std::move(original); }
    };
    inline bool CommitConfigEditUnlocked(const ConfigValues& before, const ConfigValues& after);
    inline thread_local ConfigValues* configEditBase = nullptr;
    struct ScopedConfigEdit {
        ConfigValues before, value;
        ConfigValues* previous;
        ScopedConfigEdit() : before(*SnapshotConfigValues()), value(before), previous(configView) {
            configView = &value; configEditBase = &before;
        }
        ~ScopedConfigEdit() {
            std::lock_guard<std::mutex> lock(mutex);
            if (CommitConfigEditUnlocked(before, value)) PublishConfigValuesUnlocked();
            configEditBase = nullptr; configView = previous;
        }
    };
    // Used under Config::mutex. Read-only getters skip publication unless they commit a pending UI edit.
    struct ScopedConfigAuthority {
        bool previous = configAuthority;
        bool publish;
        explicit ScopedConfigAuthority(bool writable = true) : publish(writable) {
            if (configEditBase) publish |= CommitConfigEditUnlocked(*configEditBase, *configView);
            configAuthority = true;
        }
        ~ScopedConfigAuthority() {
            if (publish) PublishConfigValuesUnlocked();
            if (configEditBase) { *configView = *publishedConfigValues.load(); *configEditBase = *configView; }
            configAuthority = previous;
        }
    };

    template<class T> inline bool ConfigEqual(const T& a, const T& b) { return a == b; }
    template<class T, size_t N> inline bool ConfigEqual(const std::array<T,N>& a, const std::array<T,N>& b);
    template<class T> inline bool ConfigEqual(const std::vector<T>& a, const std::vector<T>& b);

    inline bool ConfigEqual(const DynamicFovPoint& a, const DynamicFovPoint& b) { return ConfigEqual(a.distanceM, b.distanceM) && ConfigEqual(a.fovDeg, b.fovDeg); }
    inline bool ConfigEqual(const DynamicFovPreset& a, const DynamicFovPreset& b) { return ConfigEqual(a.id, b.id) && ConfigEqual(a.name, b.name) && ConfigEqual(a.smooth, b.smooth) && ConfigEqual(a.pointCount, b.pointCount) && ConfigEqual(a.points, b.points); }
    inline bool ConfigEqual(const AimMethodPreset& a, const AimMethodPreset& b) { return static_cast<const AimPlan::Motion&>(a) == static_cast<const AimPlan::Motion&>(b) && ConfigEqual(a.id, b.id) && ConfigEqual(a.name, b.name) && ConfigEqual(a.method, b.method) && ConfigEqual(a.angularSpeedScale, b.angularSpeedScale) && ConfigEqual(a.pidP, b.pidP) && ConfigEqual(a.pidI, b.pidI) && ConfigEqual(a.pidD, b.pidD) && ConfigEqual(a.pidMaxIntegral, b.pidMaxIntegral) && ConfigEqual(a.pidDeadzone, b.pidDeadzone) && ConfigEqual(a.bezierControlPoints, b.bezierControlPoints) && ConfigEqual(a.bezierCurvature, b.bezierCurvature) && ConfigEqual(a.bezierSpeed, b.bezierSpeed) && ConfigEqual(a.piecewiseNearDegrees, b.piecewiseNearDegrees) && ConfigEqual(a.piecewiseMidDegrees, b.piecewiseMidDegrees) && ConfigEqual(a.piecewiseFarDegrees, b.piecewiseFarDegrees) && ConfigEqual(a.piecewiseNearScale, b.piecewiseNearScale) && ConfigEqual(a.piecewiseMidScale, b.piecewiseMidScale) && ConfigEqual(a.piecewiseFarScale, b.piecewiseFarScale) && ConfigEqual(a.accelLimitedAcceleration, b.accelLimitedAcceleration) && ConfigEqual(a.constantAngularSpeedDeg, b.constantAngularSpeedDeg) && ConfigEqual(a.hybridConstantSpeedDeg, b.hybridConstantSpeedDeg) && ConfigEqual(a.hybridMaxSpeedDeg, b.hybridMaxSpeedDeg) && ConfigEqual(a.hybridAccelerationDeg, b.hybridAccelerationDeg) && ConfigEqual(a.hybridDecelerationDeg, b.hybridDecelerationDeg) && ConfigEqual(a.hybridNearRadiusDeg, b.hybridNearRadiusDeg) && ConfigEqual(a.hybridTargetMotionGain, b.hybridTargetMotionGain) && ConfigEqual(a.hybridSuddenMotionBoost, b.hybridSuddenMotionBoost) && ConfigEqual(a.hybridDeadzoneDeg, b.hybridDeadzoneDeg); }
    inline bool ConfigEqual(const AimStartLimiterProfile& a, const AimStartLimiterProfile& b) { return a.enabled == b.enabled && a.initialCapDegPerSec == b.initialCapDegPerSec && a.capRiseDegPerSec2 == b.capRiseDegPerSec2 && a.restartOnTargetChange == b.restartOnTargetChange; }
    inline bool ConfigEqual(const AimBehaviorPreset& a, const AimBehaviorPreset& b) { return static_cast<const AimBehaviorTuning&>(a)==static_cast<const AimBehaviorTuning&>(b) && a.motion==b.motion && ConfigEqual(a.id, b.id) && ConfigEqual(a.name, b.name) && ConfigEqual(a.defaultBehavior, b.defaultBehavior) && ConfigEqual(a.behavior, b.behavior) && ConfigEqual(a.method, b.method) && ConfigEqual(a.methodPresetId, b.methodPresetId) && ConfigEqual(a.baseSpeed, b.baseSpeed) && ConfigEqual(a.startLimiter, b.startLimiter) && ConfigEqual(a.moveSplitEnabled, b.moveSplitEnabled) && ConfigEqual(a.moveSplitMaxPixels, b.moveSplitMaxPixels) && ConfigEqual(a.moveSplitDelayUs, b.moveSplitDelayUs); }

    template<class T, size_t N> inline bool ConfigEqual(const std::array<T,N>& a, const std::array<T,N>& b) {
        for (size_t i=0;i<N;++i) if (!ConfigEqual(a[i],b[i])) return false; return true;
    }
    template<class T> inline bool ConfigEqual(const std::vector<T>& a, const std::vector<T>& b) {
        if (a.size()!=b.size()) return false;
        for (size_t i=0;i<a.size();++i) if (!ConfigEqual(a[i],b[i])) return false; return true;
    }
    template<class T> inline bool MergeConfigPresets(const std::vector<T>& before, const std::vector<T>& after, std::vector<T>& current) {
        bool changed = false;
        auto find = [](auto& values, int id) { return std::find_if(values.begin(), values.end(), [id](const auto& p) { return p.id == id; }); };
        for (const auto& old : before) if (find(after, old.id) == after.end())
            changed |= std::erase_if(current, [&](const auto& p) { return p.id == old.id; }) != 0;
        for (const auto& value : after) {
            auto old = find(before, value.id);
            if (old != before.end() && ConfigEqual(*old, value)) continue;
            auto item = find(current, value.id);
            if (item == current.end()) { current.push_back(value); changed = true; }
            else if (!ConfigEqual(*item, value)) { *item = value; changed = true; }
        }
        return changed;
    }
    inline bool CommitConfigEditUnlocked(const ConfigValues& before, const ConfigValues& after) {
        bool changed = false;
        const auto assign = [&](const auto& old, const auto& value, auto& current) {
            if (!ConfigEqual(old, value) && !ConfigEqual(current, value)) {
                current = value;
                changed = true;
            }
        };
        // Only edited fields commit. Concurrent edits to the same field: last explicit commit wins.
        assign(before.Bone, after.Bone, Bone);
        assign(before.BoneName, after.BoneName, BoneName);
        assign(before.Flick, after.Flick, Flick);
        assign(before.Flick_smooth, after.Flick_smooth, Flick_smooth);
        assign(before.Fov, after.Fov, Fov);
        assign(before.Fov2, after.Fov2, Fov2);
        assign(before.Prediction, after.Prediction, Prediction);
        assign(before.Smooth, after.Smooth, Smooth);
        assign(before.TargetBone, after.TargetBone, TargetBone);
        assign(before.Tracking, after.Tracking, Tracking);
        assign(before.Tracking_smooth, after.Tracking_smooth, Tracking_smooth);
        assign(before.aimBehavior, after.aimBehavior, aimBehavior);
        assign(before.aimBehaviorMethod, after.aimBehaviorMethod, aimBehaviorMethod);
        assign(before.aimBehaviorMethodPreset, after.aimBehaviorMethodPreset, aimBehaviorMethodPreset);
        assign(before.aimBehaviorPresetId, after.aimBehaviorPresetId, aimBehaviorPresetId);
        changed |= MergeConfigPresets(before.aimBehaviorPresets, after.aimBehaviorPresets, aimBehaviorPresets);
        assign(before.aimBezierControlPoints, after.aimBezierControlPoints, aimBezierControlPoints);
        assign(before.aimBezierCurvature, after.aimBezierCurvature, aimBezierCurvature);
        assign(before.aimBezierSpeed, after.aimBezierSpeed, aimBezierSpeed);
        nextAimBehaviorPresetId=(std::max)(nextAimBehaviorPresetId,after.nextAimBehaviorPresetId);
        assign(before.aimModeState, after.aimModeState, aimModeState);
        assign(before.aimMethod, after.aimMethod, aimMethod);
        changed |= MergeConfigPresets(before.aimMethodPresets, after.aimMethodPresets, aimMethodPresets);
        assign(before.aimPidD, after.aimPidD, aimPidD);
        assign(before.aimPidDeadzone, after.aimPidDeadzone, aimPidDeadzone);
        assign(before.aimPidI, after.aimPidI, aimPidI);
        assign(before.aimPidMaxIntegral, after.aimPidMaxIntegral, aimPidMaxIntegral);
        assign(before.aimPidP, after.aimPidP, aimPidP);
        assign(before.aim_key, after.aim_key, aim_key);
        assign(before.aimbotAttack, after.aimbotAttack, aimbotAttack);
        assign(before.aimbotAutoshot, after.aimbotAutoshot, aimbotAutoshot);
        assign(before.aimbotBoneMask, after.aimbotBoneMask, aimbotBoneMask);
        assign(before.aimbotDynamicFovPresetId, after.aimbotDynamicFovPresetId, aimbotDynamicFovPresetId);
        assign(before.aimbotFirePolicy, after.aimbotFirePolicy, aimbotFirePolicy);
        assign(before.aimbotFlick2ndBoxPadding, after.aimbotFlick2ndBoxPadding, aimbotFlick2ndBoxPadding);
        assign(before.aimbotFlick2ndInnerMethod, after.aimbotFlick2ndInnerMethod, aimbotFlick2ndInnerMethod);
        assign(before.aimbotFlick2ndInnerRadius, after.aimbotFlick2ndInnerRadius, aimbotFlick2ndInnerRadius);
        assign(before.aimbotFlick2ndInnerSmoothScale, after.aimbotFlick2ndInnerSmoothScale, aimbotFlick2ndInnerSmoothScale);
        assign(before.aimbotFlick2ndTriggerGate, after.aimbotFlick2ndTriggerGate, aimbotFlick2ndTriggerGate);
        assign(before.aimbotFlickPostFireDelayMs, after.aimbotFlickPostFireDelayMs, aimbotFlickPostFireDelayMs);
        assign(before.aimbotFlickShotClampMs, after.aimbotFlickShotClampMs, aimbotFlickShotClampMs);
        assign(before.aimbotFlickTrajectoryApexWindowMs, after.aimbotFlickTrajectoryApexWindowMs, aimbotFlickTrajectoryApexWindowMs);
        assign(before.aimbotFlickTrajectoryWait, after.aimbotFlickTrajectoryWait, aimbotFlickTrajectoryWait);
        assign(before.aimbotFlickTrajectoryWaitMs, after.aimbotFlickTrajectoryWaitMs, aimbotFlickTrajectoryWaitMs);
        assign(before.aimbotFovEntryMaxOutsideDeg, after.aimbotFovEntryMaxOutsideDeg, aimbotFovEntryMaxOutsideDeg);
        assign(before.aimbotFovEntryPredictionMs, after.aimbotFovEntryPredictionMs, aimbotFovEntryPredictionMs);
        assign(before.aimbotFovMode, after.aimbotFovMode, aimbotFovMode);
        assign(before.aimbotIgnoreInvisible, after.aimbotIgnoreInvisible, aimbotIgnoreInvisible);
        assign(before.aimbotKeepFiring, after.aimbotKeepFiring, aimbotKeepFiring);
        assign(before.aimbotLockTime, after.aimbotLockTime, aimbotLockTime);
        assign(before.aimbotMagneticPostFirePauseMs, after.aimbotMagneticPostFirePauseMs, aimbotMagneticPostFirePauseMs);
        assign(before.aimbotMagneticPostFirePitchScale, after.aimbotMagneticPostFirePitchScale, aimbotMagneticPostFirePitchScale);
        assign(before.aimbotMagneticPostFireYawScale, after.aimbotMagneticPostFireYawScale, aimbotMagneticPostFireYawScale);
        assign(before.aimbotMagneticPreFireBoostScale, after.aimbotMagneticPreFireBoostScale, aimbotMagneticPreFireBoostScale);
        assign(before.aimbotMagneticPreFireBoostWindowMs, after.aimbotMagneticPreFireBoostWindowMs, aimbotMagneticPreFireBoostWindowMs);
        assign(before.aimbotMagneticRecoveryMs, after.aimbotMagneticRecoveryMs, aimbotMagneticRecoveryMs);
        assign(before.aimbotMagneticShotIntervalMs, after.aimbotMagneticShotIntervalMs, aimbotMagneticShotIntervalMs);
        assign(before.aimbotMaxAim, after.aimbotMaxAim, aimbotMaxAim);
        assign(before.aimbotMaxCharge, after.aimbotMaxCharge, aimbotMaxCharge);
        assign(before.aimbotMaxDist, after.aimbotMaxDist, aimbotMaxDist);
        assign(before.aimbotMaxHead, after.aimbotMaxHead, aimbotMaxHead);
        assign(before.aimbotMinCharge, after.aimbotMinCharge, aimbotMinCharge);
        assign(before.aimbotMinDist, after.aimbotMinDist, aimbotMinDist);
        assign(before.aimbotPitchScale, after.aimbotPitchScale, aimbotPitchScale);
        assign(before.aimbotPredictFovEntry, after.aimbotPredictFovEntry, aimbotPredictFovEntry);
        assign(before.aimbotPredictionMode, after.aimbotPredictionMode, aimbotPredictionMode);
        assign(before.aimPlan, after.aimPlan, aimPlan);
        assign(before.aimbotPriority, after.aimbotPriority, aimbotPriority);
        assign(before.aimbotRequireActionHeld, after.aimbotRequireActionHeld, aimbotRequireActionHeld);
        assign(before.aimbotSmoothType, after.aimbotSmoothType, aimbotSmoothType);
        assign(before.aimbotStickiness, after.aimbotStickiness, aimbotStickiness);
        assign(before.aimbotTeam, after.aimbotTeam, aimbotTeam);
        assign(before.aimbotTrace, after.aimbotTrace, aimbotTrace);
        assign(before.aimbotTrackingDeadzone, after.aimbotTrackingDeadzone, aimbotTrackingDeadzone);
        assign(before.aimbotTwoStage, after.aimbotTwoStage, aimbotTwoStage);
        assign(before.aimbotTwoStageBoxPadding, after.aimbotTwoStageBoxPadding, aimbotTwoStageBoxPadding);
        assign(before.aimbotTwoStageInnerRadius, after.aimbotTwoStageInnerRadius, aimbotTwoStageInnerRadius);
        assign(before.aimbotTwoStageInnerSmoothScale, after.aimbotTwoStageInnerSmoothScale, aimbotTwoStageInnerSmoothScale);
        assign(before.aimbotTwoStageTriggerGate, after.aimbotTwoStageTriggerGate, aimbotTwoStageTriggerGate);
        assign(before.aimbotUnlock, after.aimbotUnlock, aimbotUnlock);
        assign(before.autobone, after.autobone, autobone);
        assign(before.autoscalefov, after.autoscalefov, autoscalefov);
        changed |= MergeConfigPresets(before.dynamicFovPresets, after.dynamicFovPresets, dynamicFovPresets);
        assign(before.hitbox, after.hitbox, hitbox);
        assign(before.minFov1, after.minFov1, minFov1);
        assign(before.minFov2, after.minFov2, minFov2);
        assign(before.triggerbot, after.triggerbot, triggerbot);
        assign(before.triggerbot2, after.triggerbot2, triggerbot2);
        assign(before.triggerbotBoneMask, after.triggerbotBoneMask, triggerbotBoneMask);
        assign(before.triggerbotChargeAware, after.triggerbotChargeAware, triggerbotChargeAware);
        assign(before.triggerbotDisableWhileReloading, after.triggerbotDisableWhileReloading, triggerbotDisableWhileReloading);
        assign(before.triggerbotIgnoreInvisible, after.triggerbotIgnoreInvisible, triggerbotIgnoreInvisible);
        assign(before.triggerbotKey, after.triggerbotKey, triggerbotKey);
        assign(before.triggerbotMinCharge, after.triggerbotMinCharge, triggerbotMinCharge);
        assign(before.triggerbotMode, after.triggerbotMode, triggerbotMode);
        assign(before.triggerbotShotInterval, after.triggerbotShotInterval, triggerbotShotInterval);

        // Deletion and every hero reference repair are one publication.
        auto repair = [&](auto& store) {
            for (auto& hero : store) for (auto& slot : hero.second)
                if (slot.preset.dynamicFovPresetId >= 0 &&
                    std::none_of(dynamicFovPresets.begin(), dynamicFovPresets.end(), [&](const auto& p) { return p.id == slot.preset.dynamicFovPresetId; })) {
                    slot.preset.dynamicFovPresetId = -1; slot.preset.fovMode = kFovModeFixed; changed = true;
                }
        };
        repair(heroAimPresets); repair(heroTriggerPresets);
        if (aimbotDynamicFovPresetId >= 0 && std::none_of(dynamicFovPresets.begin(), dynamicFovPresets.end(), [&](const auto& p) { return p.id == aimbotDynamicFovPresetId; })) {
            aimbotDynamicFovPresetId = -1; aimbotFovMode = kFovModeFixed; changed = true;
        }
        return changed;
    }


}} // namespace OW::Config
