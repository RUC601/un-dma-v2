#pragma once

#include <cstdint>
#include "Game/AimFirePhase.hpp"
#include "Game/AimSmoothingTiming.hpp"
#include "Utils/Types.hpp"

namespace OW::FlickRepeat {

inline float WindowMs(float value) {
    return std::clamp(AimFirePhase::Finite(value, 120.0f), 1.0f, 1000.0f);
}

inline float MaxSpeedDegPerSec(float value) {
    return std::clamp(AimFirePhase::Finite(value, 180.0f), 1.0f, 3600.0f);
}

// This timeline records software fire submissions, never observed game shots.
struct Timeline {
    bool submitted = false;
    std::uint32_t submittedAt = 0;
    std::uint32_t intervalMs = 0;
};

struct SessionIdentity {
    std::uint64_t heroId = 0;
    int action = -1;
    int slotIndex = -1;
    int aimKey = 0;
    std::uint64_t configGeneration = 0;
    std::uint64_t connectionEpoch = 0;
    std::uint64_t outputEpoch = 0;
    std::uint64_t slotId = 0;
    bool operator==(const SessionIdentity&) const = default;
};

struct State {
    Timeline timeline{};
    SessionIdentity identity{};
    bool identified = false;
    bool shotLatched = false;
    bool moving = false;
    std::uint64_t targetKey = 0;
};

inline void PauseMovement(State& state) {
    state.moving = false;
    state.targetKey = 0;
}

inline void EndActivation(State& state) {
    state.shotLatched = false;
    PauseMovement(state);
}

inline bool PrepareSession(State& state, const SessionIdentity& identity) {
    if (state.identified && state.identity == identity)
        return false;
    // Slot/profile/action changes restart movement, not the weapon's cooldown.
    if (state.identified && state.identity.connectionEpoch != identity.connectionEpoch)
        state.timeline = {};
    EndActivation(state);
    state.identity = identity;
    state.identified = true;
    return true;
}

inline bool PrepareTarget(State& state, std::uint64_t targetKey) {
    const bool reset = !state.moving || state.targetKey != targetKey;
    state.moving = true;
    state.targetKey = targetKey;
    return reset;
}

inline bool RecordSubmission(State& state, bool accepted, std::uint32_t submittedAt,
                             std::uint32_t intervalMs) {
    if (!accepted || (state.timeline.submitted &&
        (state.timeline.intervalMs == 0 ||
         submittedAt - state.timeline.submittedAt < state.timeline.intervalMs)))
        return false;
    state.timeline = { true, submittedAt, intervalMs };
    state.shotLatched = true;
    PauseMovement(state);
    return true;
}

struct Phase {
    bool repeat = false;
    bool movementAllowed = true;
    bool timeAllowsFire = true;
    bool cadenceMissing = false;
    float remainingMs = 0.0f;
};

inline Phase Resolve(const Timeline& timeline, std::uint32_t now,
                     bool keepFiring, float windowMs, float postFireDelayMs,
                     bool repeat = true) {
    Phase phase{};
    if (!timeline.submitted)
        return phase;
    phase.repeat = repeat;
    phase.cadenceMissing = timeline.intervalMs == 0;
    const float elapsed = static_cast<float>(now - timeline.submittedAt);
    const float postDelay = (std::max)(0.0f, AimFirePhase::Finite(postFireDelayMs, 0.0f));
    phase.remainingMs = (std::max)(0.0f, static_cast<float>(timeline.intervalMs) - elapsed);
    phase.timeAllowsFire = !phase.cadenceMissing && phase.remainingMs <= 0.0f && elapsed >= postDelay;
    const float window = (std::min)(WindowMs(windowMs), timeline.intervalMs * 0.5f);
    phase.movementAllowed = !repeat || (keepFiring && !phase.cadenceMissing &&
        phase.remainingMs <= window &&
        elapsed >= postDelay);
    return phase;
}

enum class FireStateEvidence { Missing, KnownProhibited };

enum class FirePath { HitWindow, DelayedTimeout, ForcedMiss };

inline bool AllowsFirePath(bool ordinaryFlick, FirePath path) {
    return !ordinaryFlick || path == FirePath::HitWindow;
}

inline bool AllowsFire(const Phase& phase, FireStateEvidence evidence,
                       bool safetyAllowsFire, bool currentCrosshairHits,
                       FirePath path = FirePath::HitWindow) {
    // shortcut: CN has no validated positive fire-ready reader; use time plus
    // existing negative restrictions until a version-specific reader is proven.
    return AllowsFirePath(true, path) && phase.movementAllowed && phase.timeAllowsFire &&
        evidence != FireStateEvidence::KnownProhibited &&
        safetyAllowsFire && currentCrosshairHits;
}

inline Vector3 Step(Vector3 current, Vector3 latestTarget, float remainingMs,
                    float deltaSeconds, float maxSpeedDegPerSec,
                    bool currentCrosshairHits = false) {
    if (currentCrosshairHits || !std::isfinite(current.X) || !std::isfinite(current.Y) ||
        !std::isfinite(latestTarget.X) || !std::isfinite(latestTarget.Y))
        return current;
    const float dt = std::clamp(AimFirePhase::Finite(deltaSeconds, 0.0f), 0.0f, 0.020f);
    Vector3 error = latestTarget - current;
    error.Z = 0.0f;
    error.Y = std::remainder(error.Y, 2.0f * 3.14159265358979323846f);
    const float distance = error.Size();
    if (!std::isfinite(distance) || distance <= 0.000001f || dt <= 0.0f)
        return current;
    const float remaining = (std::max)(dt, AimFirePhase::Finite(remainingMs, 0.0f) * 0.001f);
    const float cap = MaxSpeedDegPerSec(maxSpeedDegPerSec) * (3.14159265358979323846f / 180.0f);
    const float speed = (std::min)(distance / remaining, cap);
    return current + error * ((std::min)(distance, speed * dt) / distance);
}

} // namespace OW::FlickRepeat
