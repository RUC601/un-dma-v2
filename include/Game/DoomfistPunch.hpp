#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace OW::DoomfistPunch {

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
