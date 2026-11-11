#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace OW::SkillConditions {
struct NormalThreat {
    uint16_t hero;
    unsigned slot;
    const char* label;
    float interruptWindowMs = 0;
    bool incoming = true;
};
inline constexpr std::array<NormalThreat, 6> kNormalThreats{{
    {0x13b, 0, "Ana Sleep Dart",320}, {0x040, 0, "Roadhog Chain Hook",100},
    {0x23b, 1, "Sigma Accretion",650}, {0x195, 0, "Brigitte Whip Shot",0},
    {0x004, 1, "Mercy Resurrect",1750,false}, {0x206,1,"Echo Focusing Beam",2000,false}
}};
inline bool InterruptCanArrive(const NormalThreat& threat,uint32_t elapsed,float arrivalMs) {
    return std::isfinite(arrivalMs) && arrivalMs>=0 &&
        elapsed+arrivalMs+50 < threat.interruptWindowMs;
}
inline int NormalThreatIndex(uint64_t hero) {
    for (size_t i = 0; i < kNormalThreats.size(); ++i)
        if ((hero & 0xffff) == kNormalThreats[i].hero) return static_cast<int>(i);
    return -1;
}
// A held request belongs to one acquisition attempt, including failed/cancelled
// attempts. A cooldown tick must not silently turn it into a second press.
struct AimSession {
    bool active = false;
    bool blockedUntilRelease = true;
    uint64_t startedMs = 0;
    std::array<uint64_t, 6> target{};
    int boneId = -1;
    bool Advance(bool held, uint64_t now, float delayMs, float maxMs, bool available = true) {
        if (!available) { Block(); return false; }
        if (!held) { active = false; blockedUntilRelease = false; startedMs = 0; target = {}; boneId = -1; return false; }
        if (blockedUntilRelease) return false;
        if (!active) { active = true; startedMs = now; }
        const auto elapsed = now - startedMs;
        const float limit = (std::clamp)(std::isfinite(maxMs) ? maxMs : 650.f, 0.f, 5000.f);
        if (limit > 0 && elapsed > limit) { Block(); return false; }
        return elapsed >= (std::clamp)(std::isfinite(delayMs) ? delayMs : 0.f, 0.f, 1000.f);
    }
    void Block() { active = false; blockedUntilRelease = true; target = {}; boneId = -1; }
};

struct CastConfirmation {
    uint32_t submittedTick = 0;
    bool sawInactive = true;
    bool Confirmed(bool active, uint32_t sampleTick, uint32_t now) {
        if (!sampleTick || uint32_t(now - sampleTick) > 250 ||
            uint32_t(sampleTick - submittedTick) >= 700) return false;
        if (!active) sawInactive = true;
        return active && sawInactive;
    }
    bool Expired(uint32_t now) const { return uint32_t(now - submittedTick) >= 700; }
};

inline bool HealthAtOrBelow(float hp, float maximum, float threshold, bool absolute) {
    if (!std::isfinite(hp) || hp <= 0 || !std::isfinite(threshold) || threshold < 0) return false;
    if (absolute) return hp <= threshold;
    return std::isfinite(maximum) && maximum > 0 && hp / maximum * 100.f <= threshold;
}
inline bool FreshActive(bool active, uint32_t sample, uint32_t now) {
    return active && sample != 0 && uint32_t(now - sample) <= 250;
}
// Pure 1106 0xE5710 / 0xE59C0, conservative mode: 5% recent loss,
// expiring 450 ms after the last loss. Feed only successful health samples.
struct RecentDamage {
    float health = 0, maximum = 0, damage = 0;
    uint32_t sampled = 0, lastLoss = 0;
    void Reset() { *this = {}; }
    void ClearDamage() { damage = 0; lastLoss = 0; }
    void Observe(float hp, float max, uint32_t sample, uint32_t now) {
        if (!sample || uint32_t(now - sample) > 250 || !std::isfinite(hp) || hp <= 0 ||
            !std::isfinite(max) || max <= 0 || hp > max) { Reset(); return; }
        if (sample == sampled) return;
        if (sampled && int32_t(sample - sampled) < 0) { Reset(); return; }
        if (!sampled || uint32_t(sample - sampled) > 250 || max != maximum) {
            Reset(); health = hp; maximum = max; sampled = sample; return;
        }
        if (lastLoss && uint32_t(sample - lastLoss) > 450) ClearDamage();
        if (hp < health) { damage += health - hp; lastLoss = sample; }
        health = hp; maximum = max; sampled = sample;
    }
    bool Ready(uint32_t now) const {
        return sampled && uint32_t(now - sampled) <= 250 && lastLoss &&
            uint32_t(now - lastLoss) <= 450 && damage >= maximum * .05f;
    }
};
enum PauseMask : unsigned { NoPause = 0, Aim = 1, Trigger = 2, Skills = 4, All = 7 };
inline unsigned StatePause(uint64_t hero, bool firstActive, bool secondActive, bool secondaryHeld) {
    switch (hero & 0xffff) {
    case 0x002: // Wraith Form
    case 0x0dd: // Cryo-Freeze
    case 0x32b: // Burrow
        return firstActive ? All : NoPause;
    case 0x003: // Recall
        return secondActive ? All : NoPause;
    case 0x12f: // Rocket Punch: manual hold is independent of the assist key.
        return secondaryHeld ? All : NoPause;
    default:
        return NoPause; // An ultimate's charge is not an activation signal.
    }
}
using BodyIdentity = std::array<uint64_t, 6>;
struct AutomaticAimWindow {
    BodyIdentity target{};
    uint64_t started = 0;
    bool active = false, expired = false;
    bool Ready(const BodyIdentity& next, uint64_t now, float delay, float limit) {
        if (!active || target != next) { target=next;started=now;active=true;expired=false; }
        const auto elapsed=now-started;
        if (limit>0 && elapsed>limit) expired=true;
        return !expired && elapsed>=delay;
    }
    void Reset() { *this={}; }
};
struct ObservationNeeds {
    uint64_t localHero = 0;
    bool localSkills = false;
    bool enemyThreats = false;
    bool Fast(uint64_t hero) const {
        return (localSkills && hero == localHero) || (enemyThreats && NormalThreatIndex(hero) >= 0);
    }
};
struct ActiveEvent {
    bool observed = false;
    bool active = false;
    unsigned responses = 0;
    void Observe(bool next) {
        if (!observed) {
            observed = true;
            active = next;
            // An already-active first sample has no known start time.
            responses = next ? ~0u : 0u;
            return;
        }
        if (!next || !active) responses = 0;
        active = next;
    }
    bool CanRespond(unsigned responder) const { return active && !(responses & responder); }
    void Respond(unsigned responder) { responses |= responder; }
};
inline unsigned Role(uint64_t hero) {
    switch (hero & 0xffff) {
    case 0x007: case 0x009: case 0x040: case 0x068: case 0x07a: case 0x12f:
    case 0x13e: case 0x1ca: case 0x236: case 0x23b: case 0x28d: case 0x30a: case 0x362:
    case 0x4c4: case 0x542:
        return 2;
    case 0x004: case 0x020: case 0x079: case 0x13b: case 0x195: case 0x1a2:
    case 0x221: case 0x231: case 0x291: case 0x31c: case 0x365: case 0x3c3:
    case 0x4e3: case 0x516: case 0x570: case 0x12e:
        return 4;
    case 0x002: case 0x003: case 0x005: case 0x006: case 0x008: case 0x00a:
    case 0x015: case 0x016: case 0x029: case 0x042: case 0x065: case 0x06e: case 0x0dd:
    case 0x1ec: case 0x200: case 0x206: case 0x32a: case 0x32b:
    case 0x4d8: case 0x4dd: case 0x4d2: case 0x472: case 0x52c:
        return 1;
    default: return 0;
    }
}
inline bool RoleAllowed(uint64_t hero, unsigned mask) {
    return mask == 7 || (Role(hero) & mask) != 0;
}
struct RecastWindow {
    bool known = false, active = false, spent = false;
    uint64_t started = 0, lastObserved = 0;
    void Observe(bool readable, bool next, uint64_t now) {
        if (!readable) return;
        if (lastObserved && now-lastObserved>250) { known=false;started=0; }
        lastObserved=now;
        if (!known) { known = true; active = next; spent = next; return; }
        if (!next) { active = false; spent = false; started = 0; return; }
        if (!active) { active = true; spent = false; started = now; }
    }
    bool Ready(uint64_t now) const { return active && !spent && started && now-started >= 170 && now-started <= 5500; }
};
}
