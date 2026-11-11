#pragma once

#include "Game/FovGeometry.hpp"
#include "Game/SkillConditions.hpp"
#include "Utils/Config.hpp"

namespace OW::AutoMelee {

// All three 155284 dumps use this input word; Pure 1106 confirms the melee bit.
// Evidence: 03_DOWNP/auto-melee-input-155284-20261111/static-evidence.json.
inline constexpr uint64_t kActionOffset = 0x121C;
inline constexpr uint32_t kActionMask = 0x800;
inline constexpr uint64_t kHoldMs = 50;

struct MemoryPulse {
    uint64_t address = 0, startedMs = 0;
    bool owned = false, releasing = false;

    template<class Read, class Write, class Current>
    bool Start(uint64_t destination, uint64_t now, Read read, Write write, Current current) {
        if (owned || !destination || !current()) return false;
        uint32_t value = 0;
        if (!read(destination, &value, sizeof(value)) || !current() || (value & kActionMask)) return false;
        address = destination; startedMs = now; owned = true;
        value |= kActionMask;
        // A failed transport can be partial; retain release-only cleanup until checked.
        releasing = !write(address, &value, sizeof(value));
        return !releasing;
    }

    template<class Read, class Write, class Current>
    bool Update(uint64_t now, bool keepHolding, Read read, Write write, Current current) {
        if (!owned) return true;
        if (!current()) { *this = {}; return false; } // Never touch a retired body/address.
        releasing = releasing || !keepHolding || now - startedMs >= kHoldMs;
        uint32_t value = 0;
        if (!read(address, &value, sizeof(value))) { releasing = true; return false; }
        if (!current()) { *this = {}; return false; }
        const uint32_t next = releasing ? value & ~kActionMask : value | kActionMask;
        if (next != value && !write(address, &next, sizeof(next))) { releasing = true; return false; }
        if (releasing) *this = {};
        return true;
    }
};

inline bool HealthEligible(float current, float maximum, const Config::HeroSkillSettings& settings)
{
    if (!std::isfinite(current) || current <= 0.0f)
        return false;
    switch (settings.meleeHealthMode) {
    case Config::kMeleeAnyHealth:
        return true;
    case Config::kMeleeHealthPercent:
        return SkillConditions::HealthAtOrBelow(current, maximum, settings.meleeHealthPercent, false);
    case Config::kMeleeHealthAbsolute:
        return SkillConditions::HealthAtOrBelow(current, maximum, settings.enemyHealthThreshold, true);
    default:
        return false;
    }
}

// Test the view ray directly so crossing the yaw seam cannot reject a hit.
inline bool InWindow(const FovGeometry::RuntimeContext& view,
                     const Vector3& point, float radius, float maxDistance)
{
    if (!view.valid || !FovGeometry::IsFiniteVector(view.camera) ||
        !FovGeometry::IsFiniteVector(point) || !std::isfinite(radius) ||
        !std::isfinite(maxDistance) || radius <= 0.0f || maxDistance <= 0.0f)
        return false;
    const Vector3 forward = FovGeometry::NormalizeVector(view.forward);
    const Vector3 offset = point - view.camera;
    const float distance = offset.Size();
    const float along = offset | forward;
    if (forward == Vector3{} || !std::isfinite(distance) ||
        distance <= 0.0001f || distance > maxDistance || along <= 0.0f)
        return false;
    const Vector3 perpendicular = offset - forward * along;
    return perpendicular.Size() <= radius;
}

} // namespace OW::AutoMelee
