#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace OW::DoomfistPunch {

// OverLab measures self movement separately from enemy knockback.
// https://overlab.cn/zh/wiki/hero/doomfist#rocket-punch
inline constexpr float kNormalSpeed = 45.0f;
inline constexpr float kNormalMaxReach = 20.0f;
inline constexpr float kEmpoweredMultiplier = 1.5f;
inline constexpr std::uint64_t kEmpoweredDurationMs = 20'000;
inline constexpr std::uint64_t kPunchCooldownMs = 4'000;

struct EmpowermentEstimate {
    static constexpr std::uint64_t kMeteorStartupMs = 500;
    static constexpr std::uint64_t kMeteorTargetingMs = 4'000;
    static constexpr std::uint64_t kMeteorLandingMs = 1'000;
    static constexpr std::uint64_t kBlockDurationMs = 2'500;
    static constexpr std::uint64_t kCooldownEdgeMarginMs = 100;

    void MeteorActivated(std::uint64_t at) {
        meteorActivatedAt = at;
        meteorLandingAt = at + kMeteorStartupMs + kMeteorTargetingMs + kMeteorLandingMs;
        blockStartedAt = blockEndsAt = 0;
        empoweredUntil = 0;
    }

    void MeteorConfirmed(std::uint64_t at) {
        // The confirm input selects the landing zone. Impact follows after the windup.
        if (meteorActivatedAt && at >= meteorActivatedAt + kMeteorStartupMs &&
            at <= meteorActivatedAt + kMeteorStartupMs + kMeteorTargetingMs) {
            meteorLandingAt = at + kMeteorLandingMs;
            meteorActivatedAt = 0;
        }
    }

    void Advance(std::uint64_t now) {
        if (meteorLandingAt && now >= meteorLandingAt) {
            empoweredUntil = meteorLandingAt + kEmpoweredDurationMs;
            meteorActivatedAt = meteorLandingAt = cooldownStartedAt = 0;
            blockStartedAt = blockEndsAt = 0;
        }
    }

    bool Empowered(std::uint64_t now) const { return now < empoweredUntil; }
    bool MeteorLandingPending() const { return meteorLandingAt != 0; }

    void PunchCooldownStarted(std::uint64_t at) {
        empoweredUntil = 0;
        cooldownStartedAt = at;
        blockStartedAt = blockEndsAt = cooldownSampleAt = 0;
        wasCooling = false;
    }

    void PowerBlockStarted(std::uint64_t at) {
        if (cooldownStartedAt && at >= cooldownStartedAt && at < cooldownStartedAt + kPunchCooldownMs &&
            !meteorLandingAt && !Empowered(at)) {
            blockStartedAt = at;
            blockEndsAt = at + kBlockDurationMs;
        }
    }

    void PowerBlockEnded(std::uint64_t at) {
        if (blockStartedAt && at >= blockStartedAt)
            blockEndsAt = (std::min)(blockEndsAt, at);
    }

    // Feed successful right-button cooldown observations. A clock estimate or an
    // input press cannot establish a Ready edge.
    void ObservePunchCooldown(std::uint64_t at, bool cooling) {
        if (!cooldownStartedAt || at < cooldownStartedAt || at <= cooldownSampleAt) return;
        if (!cooling && wasCooling && at - cooldownSampleAt <= 250 &&
            blockStartedAt && at >= blockStartedAt && at <= blockEndsAt &&
            at + kCooldownEdgeMarginMs < cooldownStartedAt + kPunchCooldownMs) {
            empoweredUntil = at + kEmpoweredDurationMs;
            blockStartedAt = blockEndsAt = 0;
        }
        wasCooling = cooling;
        cooldownSampleAt = at;
    }

private:
    std::uint64_t meteorActivatedAt = 0;
    std::uint64_t meteorLandingAt = 0;
    std::uint64_t empoweredUntil = 0;
    std::uint64_t cooldownStartedAt = 0;
    std::uint64_t blockStartedAt = 0;
    std::uint64_t blockEndsAt = 0;
    std::uint64_t cooldownSampleAt = 0;
    bool wasCooling = false;
};

enum class BlockReason { None, Deadline, CancelInput, OutputFailure, Released };

inline const char* BlockReasonName(BlockReason reason) {
    switch (reason) {
    case BlockReason::Deadline: return "auto_release_deadline";
    case BlockReason::CancelInput: return "cancel_input";
    case BlockReason::OutputFailure: return "output_failure";
    case BlockReason::Released: return "released";
    default: return "none";
    }
}

// Input timing is an estimate from the observed physical press, not an ability-state read.
struct ChargeSession {
    bool armed = false;
    bool charging = false;
    bool requested = false;
    bool blocked = false;
    bool released = false;
    BlockReason blockReason = BlockReason::None;
    std::uint64_t startedMs = 0;

    bool Update(std::uint64_t nowMs, bool rightDown, bool assistDown,
                float maxHoldMs, float releaseLeadMs) {
        if (!rightDown) {
            // Our generated RMB-up is not a new manual activation while Mouse4 is held.
            if (released && assistDown)
                return false;
            *this = {};
            armed = true;
            return false;
        }
        if (!armed || blocked)
            return false;
        if (!charging) {
            charging = true;
            startedMs = nowMs;
        }
        requested |= assistDown;
        if (static_cast<float>(nowMs - startedMs) >= maxHoldMs - releaseLeadMs) {
            Block(BlockReason::Deadline);
            return false;
        }
        return requested;
    }

    void Block(BlockReason reason = BlockReason::CancelInput) { blocked = true; blockReason = reason; }
    void MarkReleased() { Block(BlockReason::Released); released = true; }
};

inline bool RestoreMouseAfterRelease(std::uint64_t elapsedMs, bool physicalFresh, bool physicalRightDown) {
    return elapsedMs >= 80 && (!physicalFresh || !physicalRightDown || elapsedMs >= 2000);
}

inline float HorizontalDistance(float dx, float dz) {
    return std::hypot(dx, dz);
}

inline bool InReach(float distance, float heightDelta, float maxDistance,
                    float maxAbove, float maxBelow) {
    return std::isfinite(distance) && std::isfinite(heightDelta) &&
        distance > 0.05f && distance <= maxDistance &&
        heightDelta <= maxAbove && heightDelta >= -maxBelow;
}

inline float ChargeFraction(float elapsedMs, float fullChargeMs) {
    return std::clamp(elapsedMs / fullChargeMs, 0.0f, 1.0f);
}

} // namespace OW::DoomfistPunch
