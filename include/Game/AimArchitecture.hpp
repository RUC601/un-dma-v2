#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <Windows.h>

#include "Game/Entity.hpp"
#include "Game/PredictionAlgorithm.hpp"
#include "Game/PredictionProfile.hpp"
#include "Game/AimBehaviorPolicy.hpp"
#include "Game/AimPlan.hpp"
#include "Game/ContourTargeting.hpp"
#include "Game/TargetVisibility.hpp"
#include "Utils/Types.hpp"

namespace OW {

enum class AimClass : int {
    HitscanSingle = 0,
    HitscanAuto,
    HitscanBurst,
    ProjectileSingle,
    ProjectileAuto,
    ProjectileExplosive,
    Shotgun,
    Beam,
    Targeted,
    Melee,
    Movement,
    Unknown,
};

enum class PredictionOverrideMode : int {
    Auto = 0,
    ForceOn = 1,
    ForceOff = 2,
};

// Both prediction paths share projectile data. Target motion is sampled from
// the target only. Local velocity never becomes part of projectile muzzle
// velocity or target lead input.
enum class RuntimeVariantRequirement : int {
    None = 0,
    PerkOn,
};

enum class TraceMode : int {
    Strict = 0,
    Relaxed = 1,
    Off = 2,
};

enum class UnlockMode : int {
    Anytime = 0,
    OnRelease = 1,
    NeverUntilTimeout = 2,
};

enum class SmoothingControllerType : int {
    Linear = 0,
    PID = 1,
    Bezier = 2,
    PiecewiseCurve = 3,
    AccelLimited = 4,
    Constant = 5,
    HybridPID = 6,
    Shuwu = 7,
    ErrorResponseCurve = 8,
    KoiProportional = 9,
};

enum class ExecutionSource : int {
    GlobalAim = 0,
    Trigger = 1,
    HeroSkill = 2,
    SequenceInternal = 3,
};

struct FirePolicy {
    FirePolicyType type = FirePolicyType::ManualOnly;
    float releaseDelayMs = 0.0f;
    float burstDurationMs = 0.0f;
    float minCharge = 0.0f;
    float maxCharge = 100.0f;
};

inline constexpr int kWeaponControlUseActionButton = -2;
inline constexpr int kWeaponControlNoButton = -1;

struct WeaponControlSpec {
    int generatedFireButton = kWeaponControlUseActionButton;
    int trackingHoldButton = kWeaponControlUseActionButton;
    int stanceHoldButton = kWeaponControlNoButton;
};

struct AimBehavior {
    AimBehaviorType type = AimBehaviorType::Tracking;
    float behaviorTolerance = 0.0f;
    float maxAimTimeMs = 0.0f;
    float minDistance = 0.0f;
    float maxDistance = 0.0f;
    float headshotDistanceGate = 0.0f;
};

struct TargetLockPolicy {
    TraceMode traceMode = TraceMode::Strict;
    UnlockMode unlockMode = UnlockMode::Anytime;
    float minLockMs = 0.0f;
    float maxLockMs = 0.0f;
    float retargetHysteresis = 0.0f;
};

struct SmoothingControllerProfile {
    SmoothingControllerType type = SmoothingControllerType::Linear;
    float speed = 0.0f;
    float acceleration = 0.0f;
    float pitchScale = 1.0f;
};

struct WeaponSpec {
    uint64_t heroId = 0;
    std::string_view heroName{};
    std::string_view weaponId{};
    std::string_view weaponName{};
    int action = 0;
    int order = 0;
    AimClass aimClass = AimClass::Unknown;
    ProjectileSpec projectile{};
    FirePolicy firePolicy{};
    AimBehaviorType defaultBehavior = AimBehaviorType::Tracking;
    RuntimeVariantRequirement variantRequirement = RuntimeVariantRequirement::None;
    std::string_view variantId{};
    std::string_view replacesWeaponId{};
    WeaponControlSpec control{};
    std::string_view sourceUrl{};
    std::string_view sourceNote{};
    float confidence = 0.0f;
};

struct BoneHitboxSpec {
    int boneId = 0;
    std::string_view boneName{};
    float radiusWorld = 0.0f;
    Vector3 centerOffsetLocal{};
    std::string_view shape{"sphere"};
    std::string_view sourceUrl{};
    std::string_view sourceNote{};
    float confidence = 0.0f;
};

struct HeroGeometrySpec {
    uint64_t heroId = 0;
    std::string_view heroName{};
    const BoneHitboxSpec* bones = nullptr;
    int boneCount = 0;
};

struct EntityMotionState {
    enum class Kind : int {
        Unknown,
        Grounded,
        AirborneRising,
        AirborneApex,
        AirborneFalling,
        Strafing,
        SuddenStop,
        TeleportOrInvalid,
    };

    Kind kind = Kind::Unknown;
    Vector3 worldVelocity{};
    Vector2 screenVelocity{};
    float verticalVelocity = 0.0f;
    float confidence = 0.0f;
};

struct NativeHitWindowInput {
    Vector3 displacement{};
    Vector3 targetVelocity{};
    float launchUpBias = 0.0f;
    float projectileRadius = -1.0f;
    float hitboxScalePercent = 100.0f;
    float projectileSpeed = 0.0f;
    float gravityAcceleration = 0.0f;
    float maxFlightSeconds = 0.0f;
    bool gravity = false;
};

struct TargetCandidate {
    Contour::Observation contour{};
    AimPlan::Effective plan{};
    bool planPreferred = false;
    bool valid = false;
    int entityIndex = -1;
    uint64_t entityKey = 0;
    c_entity entitySnapshot{};
    int boneId = 0;
    Vector3 rawAimPoint{};
    Vector3 predictedAimPoint{};
    Vector3 aimPoint{};
    Vector2 screenPoint{};
    float distance = 0.0f;
    float fovScore = (std::numeric_limits<float>::max)();
    float effectiveFovDeg = 0.0f;
    int dynamicFovPresetId = -1;
    bool dynamicFov = false;
    bool predictedFovEntry = false;
    float fovEntryHorizonMs = 0.0f;
    float currentFovScore = (std::numeric_limits<float>::max)();
    float projectedFovScore = (std::numeric_limits<float>::max)();
    float effectiveHitWindow = 0.0f;
    NativeHitWindowInput nativeHitWindow{};
    EntityMotionState motion{};
    TargetLockPolicy lockPolicy{};
    const WeaponSpec* weaponSpec = nullptr;
};

inline PredictionOverrideMode ClampPredictionOverride(int value)
{
    return static_cast<PredictionOverrideMode>(std::clamp(value, 0, 2));
}

inline FirePolicyType ClampFirePolicy(int value)
{
    return static_cast<FirePolicyType>(std::clamp(value, 0, 6));
}

inline TraceMode ClampTraceMode(int value)
{
    return static_cast<TraceMode>(std::clamp(value, 0, 2));
}

inline UnlockMode ClampUnlockMode(int value)
{
    return static_cast<UnlockMode>(std::clamp(value, 0, 2));
}

inline SmoothingControllerType ClampSmoothingController(int value)
{
    return static_cast<SmoothingControllerType>(std::clamp(value, 0, AimPlan::kMotionMethodCount - 1));
}

inline bool ResolvePredictionEnabled(PredictionOverrideMode mode,
                                     const WeaponSpec* weapon,
                                     bool legacyFallback)
{
    // V2 prediction is always part of the target-point pipeline.  Keep the
    // old arguments so archived configs and call sites remain source
    // compatible, but do not let Auto/Force Off become a second runtime
    // truth.  A zero-speed profile still produces the valid pre-fire motion
    // estimate and cast-delay path without inventing projectile flight.
    (void)mode;
    (void)weapon;
    (void)legacyFallback;
    return true;
}

inline int LegacyAimModeFromBehavior(AimBehaviorType behavior)
{
    return behavior == AimBehaviorType::Tracking ? 0 : 1;
}

inline bool AllowsGeneratedAimFire(FirePolicyType policy)
{
    return policy != FirePolicyType::ManualOnly;
}

inline constexpr bool IsKeepFiringPolicy(FirePolicyType policy) noexcept
{
    return policy == FirePolicyType::KeepFiring;
}

inline constexpr bool IsSingleShotPolicy(FirePolicyType policy) noexcept
{
    return policy == FirePolicyType::SingleShot;
}

inline constexpr bool IsTrackingWithHoldPolicy(FirePolicyType policy) noexcept
{
    return policy == FirePolicyType::TrackingWithHold;
}

inline bool AimActivationAllowed(bool activationHotkeyDown,
                                 bool requireActionHeld,
                                 bool actionButtonDown)
{
    return activationHotkeyDown && (!requireActionHeld || actionButtonDown);
}

inline constexpr bool AllowsGeneratedTrackingHold(FirePolicyType policy,
                                                   bool activationIsPrimaryMouseAction,
                                                   bool activationMatchesOutputButton)
{
    // Tracking With Hold owns the selected Action Profile's output button
    // during the tracking session.  The activation key starts that session
    // and is not required to be the same physical mouse button.
    (void)activationIsPrimaryMouseAction;
    (void)activationMatchesOutputButton;
    return policy == FirePolicyType::TrackingWithHold;
}

inline constexpr bool ShouldApplyDisabledHeroTriggerFallback(
    bool hasPresentHeroTriggerSlot,
    bool hasEnabledHeroTriggerFallback) noexcept
{
    return hasPresentHeroTriggerSlot && !hasEnabledHeroTriggerFallback;
}

static_assert(AllowsGeneratedTrackingHold(
    FirePolicyType::HoldWhileTracking, true, false));
static_assert(AllowsGeneratedTrackingHold(
    FirePolicyType::HoldWhileTracking, true, true));
static_assert(AllowsGeneratedTrackingHold(
    FirePolicyType::HoldWhileTracking, false, false));
static_assert(ShouldApplyDisabledHeroTriggerFallback(true, false));

} // namespace OW
