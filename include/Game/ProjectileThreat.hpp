#pragma once
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace OW::ProjectileThreat {
// Pool IDs from the existing Pure 1106 Item ESP table. Only these two
// in-flight projectiles have confirmed Matrix/Grasp/Spin/Deflect interactions:
// https://overwatch.weirdgloop.org/w/Ana#Biotic_Grenade
// https://overwatch.weirdgloop.org/w/Bastion#A-36_Tactical_Grenade
// Accretion, deployables, explosions and unidentified objects are excluded.
inline constexpr uint64_t kBioticGrenade = 0x400000000001426ull;
inline constexpr uint64_t kTacticalGrenade = 0x400000000003F69ull;
inline constexpr uint32_t kFreshMs = 150, kMinimumDeltaMs = 10, kMaximumDeltaMs = 150;
constexpr bool Supported(uint64_t id) { return id == kBioticGrenade || id == kTacticalGrenade; }
constexpr bool ValidTeam(uint32_t key) {
    // FFA's shared bit cannot distinguish our own projectile from an enemy's.
    return key && key != (1u << 27) && !(key & ~0x0F800000u) && !(key & (key - 1));
}
inline bool Fresh(uint32_t tick, uint32_t now) { return tick && uint32_t(now - tick) <= kFreshMs; }
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec3 operator*(float value) const { return {x*value,y*value,z*value}; }
    float Dot(Vec3 b) const { return x*b.x+y*b.y+z*b.z; }
    bool Finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};
struct Sample {
    uint64_t parent = 0, type = 0, scene = 0, component = 0;
    uint32_t positionTick = 0, statusTick = 0, teamKey = 0;
    bool teamKnown = false;
    Vec3 position{};
};
struct Track {
    Sample sample{}, anchor{};
    Vec3 velocity{};
    uint32_t velocityTick = 0;
    bool velocityKnown = false, responded = false;
};
inline bool SameIdentity(const Sample& a, const Sample& b) {
    return a.parent == b.parent && a.type == b.type && a.scene == b.scene && a.component == b.component;
}
class Tracker {
    std::unordered_map<uint64_t, Track> tracks_;
public:
    void Reset() { tracks_.clear(); }
    const Track* Find(uint64_t parent) const {
        const auto it = tracks_.find(parent);
        return it == tracks_.end() ? nullptr : &it->second;
    }
    void MarkResponded(uint64_t parent) {
        if (auto it = tracks_.find(parent); it != tracks_.end()) it->second.responded = true;
    }
    void Prune(uint32_t now) {
        // Preserve response tombstones across a brief disappearance; lack of a
        // position sample must not re-arm the same projectile on its return.
        for (auto it = tracks_.begin(); it != tracks_.end();) {
            if (uint32_t(now - it->second.sample.positionTick) > 10000) it = tracks_.erase(it);
            else ++it;
        }
    }
    const Track* Observe(const Sample& next, uint32_t now) {
        if (!next.parent || !next.scene || !next.component || !Supported(next.type)) {
            if (next.parent) tracks_.erase(next.parent);
            return nullptr;
        }
        auto& track = tracks_[next.parent];
        if (!SameIdentity(track.sample,next)) track = {};
        const bool readable = next.position.Finite() && Fresh(next.positionTick,now) &&
            next.teamKnown && ValidTeam(next.teamKey) && Fresh(next.statusTick,now);
        if (!readable) {
            track.velocityKnown = false; track.velocityTick = 0; track.anchor = {};
            track.sample = next;
            return &track;
        }
        if (track.sample.positionTick && int32_t(next.positionTick-track.sample.positionTick) < 0) {
            // Do not use a late-arriving old frame as the first point of a new
            // trajectory; wait for a current frame and a later observation.
            track.velocityKnown = false; track.velocityTick = 0; track.anchor = {};
            return &track;
        }
        if (!track.anchor.positionTick || !track.sample.positionTick ||
            track.sample.teamKey != next.teamKey ||
            uint32_t(next.positionTick - track.sample.positionTick) > kMaximumDeltaMs) {
            // A gap or team transition requires two new
            // observations. A team transition is not a new object identity.
            track.sample = track.anchor = next;
            track.velocityKnown = false; track.velocityTick = 0;
            return &track;
        }
        if (next.positionTick == track.sample.positionTick) {
            // Status can be read on a different cadence; never refresh motion
            // or invent a new position sample from a repeatedly published one.
            track.sample.statusTick = next.statusTick;
            track.sample.teamKnown = next.teamKnown;
            return &track;
        }
        track.sample = next;
        const auto delta = uint32_t(next.positionTick - track.anchor.positionTick);
        if (delta < kMinimumDeltaMs) return &track; // Accumulate 300 Hz samples.
        if (delta > kMaximumDeltaMs) {
            track.anchor = next; track.velocityKnown = false; track.velocityTick = 0;
            return &track;
        }
        const auto velocity = (next.position-track.anchor.position)*(1000.f/float(delta));
        const auto speed2 = velocity.Dot(velocity);
        track.anchor = next;
        // A stationary/stuck grenade is not an airborne threat. A teleport or
        // implausible speed cannot be used to infer a flight direction.
        track.velocityKnown = velocity.Finite() && std::isfinite(speed2) && speed2 >= 1.f && speed2 <= 22500.f;
        track.velocity = track.velocityKnown ? velocity : Vec3{};
        track.velocityTick = track.velocityKnown ? next.positionTick : 0;
        return &track;
    }
};
struct Approach {
    bool valid = false;
    Vec3 position{};
    float seconds = 0, missDistance = 0, facingCos = 0;
};
// Short linear estimate from measured positions, not a read of the game's
// trajectory/owner or a guarantee of impact. Caller supplies a fresh local
// center, velocity and observed forward direction and rechecks before output.
inline Approach Evaluate(const Track& track, Vec3 localCenter, Vec3 localVelocity,
                         Vec3 forward, uint32_t localTeam, uint32_t now,
                         float maxRange = 10, float horizonMs = 250,
                         float impactRadius = 1.5f, float minFacingCos = 0) {
    Approach result{};
    const auto& sample = track.sample;
    if (track.responded || !track.velocityKnown || !Supported(sample.type) ||
        !Fresh(sample.positionTick,now) || !Fresh(sample.statusTick,now) || !Fresh(track.velocityTick,now) ||
        !sample.teamKnown || !ValidTeam(sample.teamKey) || !ValidTeam(localTeam) || sample.teamKey == localTeam ||
        !localCenter.Finite() || !localVelocity.Finite() || !forward.Finite() ||
        !std::isfinite(maxRange) || maxRange <= 0 || !std::isfinite(horizonMs) || horizonMs <= 0 || horizonMs > 500 ||
        !std::isfinite(impactRadius) || impactRadius <= 0 || !std::isfinite(minFacingCos) ||
        minFacingCos < -1 || minFacingCos > 1) return result;
    result.position = sample.position + track.velocity*(float(uint32_t(now-sample.positionTick))*.001f);
    const auto relative = result.position-localCenter;
    const auto relativeVelocity = track.velocity-localVelocity;
    const float distance2 = relative.Dot(relative), speed2 = relativeVelocity.Dot(relativeVelocity);
    const float forward2 = forward.Dot(forward), approach = relative.Dot(relativeVelocity);
    if (!std::isfinite(distance2) || !std::isfinite(speed2) || !std::isfinite(forward2) ||
        distance2 <= .0001f || distance2 > maxRange*maxRange || speed2 < 1.f || forward2 < .25f || approach >= 0) return result;
    result.seconds = -approach/speed2;
    if (result.seconds > horizonMs*.001f) return result;
    const auto closest = relative+relativeVelocity*result.seconds;
    const float miss2 = closest.Dot(closest);
    result.facingCos = forward.Dot(relative)/std::sqrt(forward2*distance2);
    if (!std::isfinite(miss2) || miss2 > impactRadius*impactRadius || result.facingCos < minFacingCos) return result;
    result.missDistance = std::sqrt(miss2);
    result.valid = true;
    return result;
}
}
