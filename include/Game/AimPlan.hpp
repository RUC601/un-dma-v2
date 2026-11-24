#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include "Game/ShuwuCore.hpp"
#include "Game/AimResponseCurve.hpp"

namespace OW::AimPlan {
inline constexpr int kMotionMethodCount = 10;
inline constexpr int kMaxGroups = 12;
inline constexpr int kMaxHeroes = 64;
inline constexpr int kMaxSelectionRules = 8;
inline constexpr int kRuleMeasureCount = 6;
inline float Finite(float v, float fallback, float lo, float hi) {
    return (std::clamp)(std::isfinite(v) ? v : fallback, lo, hi);
}
struct Motion {
        int method = 0;
        float angularSpeedScale = 100.0f;
        float pidP = 0.5f;
        float pidI = 0.01f;
        float pidD = 0.1f;
        float pidMaxIntegral = 10.0f;
        float pidDeadzone = 1.0f;
        int bezierControlPoints = 2;
        float bezierCurvature = 0.5f;
        float bezierSpeed = 50.0f;
        float piecewiseNearDegrees = 2.0f;
        float piecewiseMidDegrees = 6.0f;
        float piecewiseFarDegrees = 12.0f;
        float piecewiseNearScale = 0.20f;
        float piecewiseMidScale = 0.45f;
        float piecewiseFarScale = 0.75f;
        float accelLimitedAcceleration = 0.1f;
        float constantAngularSpeedDeg = 30.0f;
        float hybridConstantSpeedDeg = 45.0f;
        float hybridMaxSpeedDeg = 720.0f;
        float hybridAccelerationDeg = 1800.0f;
        float hybridDecelerationDeg = 2600.0f;
        float hybridNearRadiusDeg = 3.0f;
        float hybridTargetMotionGain = 0.35f;
        float hybridSuddenMotionBoost = 2.5f;
        float hybridDeadzoneDeg = 0.10f;
    AimResponseCurve::Points responseCurve{};
    koi::ShuwuConfig shuwu{};
    bool operator==(const Motion&) const = default;
};
enum class RuleMeasure : int {
    Hero = 0,
    FullHealth = 1,
    Visible = 2,
    Health = 3,
    Distance = 4,
    Angle = 5,
};
struct SelectionRule {
    // measure: 0=Hero, 1=FullHealth, 2=Visible, 3=Health, 4=Distance, 5=Angle.
    int measure = 0;
    std::vector<uint64_t> heroes;
    // Inclusive range for Health (HP), Distance (m), or Angle (deg).
    // maxValue <= 0 means unlimited upper bound.
    float minValue = 0.0f, maxValue = 0.0f;
    bool operator==(const SelectionRule&) const = default;
};
struct RuleObservation {
    uint64_t hero = 0;
    float hp = 0.0f;
    float maxHp = 0.0f;
    bool visible = false;
    float distance = std::numeric_limits<float>::quiet_NaN();
    float angleDeg = std::numeric_limits<float>::quiet_NaN();
};
struct Selection {
    // Legacy single-rule fields retained for migration/compatibility.
    // mode: Any / Prefer / Only. Predicate: hero set / full HP / visible.
    int mode = 0, predicate = 0;
    std::vector<uint64_t> heroes;
    // Multi-condition rules:
    // - onlyRules: hard eligibility (ALL rules must match).
    // - preferRules: ranking priority (ANY matching rule ranks ahead).
    std::vector<SelectionRule> onlyRules;
    std::vector<SelectionRule> preferRules;
    float angleWeight = 1, distanceWeight = 0, healthWeight = 0;
    // HP is absolute effective HP. An upper bound of zero is unlimited.
    float minHealth = 0, maxHealth = 0;
    bool operator==(const Selection&) const = default;
};
struct Limits {
    float minDistance = 0, maxDistance = 100, fov = 15;
    float minHealth = 0, maxHealth = 0;
    bool operator==(const Limits&) const = default;
};
struct AimPoint {
    uint16_t boneMask = 1;
    int predictionMode = 0;
    bool operator==(const AimPoint&) const = default;
};
struct Group {
    uint64_t id = 0;
    std::string name = "Hero exception";
    std::vector<uint64_t> heroes;
    bool overrideLimits = false, overrideAimPoint = false, overrideMotion = false;
    Limits limits{};
    AimPoint point{};
    Motion motion{};
    bool operator==(const Group&) const = default;
};
struct Plan {
    Selection selection{};
    bool overrideMotion = false;
    Motion motion{};
    std::vector<Group> groups;
    bool operator==(const Plan&) const = default;
};
struct Effective {
    uint64_t ruleId = 0;
    bool overrideLimits = false, overrideAimPoint = false, overrideMotion = false;
    Limits limits{};
    AimPoint point{};
    Motion motion{};
    bool operator==(const Effective&) const = default;
};
inline bool Contains(const std::vector<uint64_t>& ids, uint64_t id) {
    return id && std::find(ids.begin(),ids.end(),id)!=ids.end();
}
inline Effective Resolve(const Plan& p, uint64_t hero) {
    Effective e; e.overrideMotion=p.overrideMotion; e.motion=p.motion;
    for (const auto& g:p.groups) if (Contains(g.heroes,hero)) {
        e.ruleId=g.id; e.overrideLimits=g.overrideLimits; e.limits=g.limits;
        e.overrideAimPoint=g.overrideAimPoint; e.point=g.point;
        if(g.overrideMotion) { e.overrideMotion=true; e.motion=g.motion; }
        break;
    }
    return e;
}
inline bool PredicateMatches(const Selection& p, uint64_t hero, float hp, float maxHp, bool visible) {
    switch(p.predicate) {
    case 1: return std::isfinite(hp) && std::isfinite(maxHp) && maxHp>0 && hp>=maxHp-0.01f;
    case 2: return visible;
    default: return Contains(p.heroes,hero);
    }
}
inline bool IsSpatialRule(const SelectionRule& r) {
    return r.measure == static_cast<int>(RuleMeasure::Distance) ||
        r.measure == static_cast<int>(RuleMeasure::Angle);
}
inline bool RangePasses(float value, float lo, float hi) {
    return std::isfinite(value) && value >= lo && (hi <= 0.0f || value <= hi);
}
inline bool RuleMatches(const SelectionRule& r, const RuleObservation& obs) {
    switch (r.measure) {
    case static_cast<int>(RuleMeasure::Hero):
        return Contains(r.heroes, obs.hero);
    case static_cast<int>(RuleMeasure::FullHealth):
        return std::isfinite(obs.hp) && std::isfinite(obs.maxHp) &&
            obs.maxHp > 0.0f && obs.hp >= obs.maxHp - 0.01f;
    case static_cast<int>(RuleMeasure::Visible):
        return obs.visible;
    case static_cast<int>(RuleMeasure::Health):
        return RangePasses(obs.hp, r.minValue, r.maxValue);
    case static_cast<int>(RuleMeasure::Distance):
        return RangePasses(obs.distance, r.minValue, r.maxValue);
    case static_cast<int>(RuleMeasure::Angle):
        return RangePasses(obs.angleDeg, r.minValue, r.maxValue);
    default:
        return false;
    }
}
inline bool HasOnlyFilter(const Selection& p) {
    return !p.onlyRules.empty() || p.mode == 2;
}
inline bool HasPreferFilter(const Selection& p) {
    return !p.preferRules.empty() || p.mode == 1;
}
inline void SyncLegacySelectionSummary(Selection& s) {
    if (!s.onlyRules.empty()) {
        s.mode = 2;
        if (s.onlyRules[0].measure <= 2) {
            s.predicate = s.onlyRules[0].measure;
            s.heroes = s.predicate == 0 ? s.onlyRules[0].heroes : std::vector<uint64_t>{};
        } else {
            s.predicate = 0;
            s.heroes.clear();
        }
    } else if (!s.preferRules.empty()) {
        s.mode = 1;
        if (s.preferRules[0].measure <= 2) {
            s.predicate = s.preferRules[0].measure;
            s.heroes = s.predicate == 0 ? s.preferRules[0].heroes : std::vector<uint64_t>{};
        } else {
            s.predicate = 0;
            s.heroes.clear();
        }
    } else {
        s.mode = 0;
        s.predicate = 0;
        s.heroes.clear();
    }
}
inline bool HealthPasses(float hp,float lo,float hi) {
    return std::isfinite(hp) && hp>=lo && (hi<=0 || hp<=hi);
}
inline bool EligiblePreGeometry(const Selection& p,const Effective& e,uint64_t hero,float hp,float maxHp,bool visible) {
    if (!HealthPasses(hp,e.overrideLimits?e.limits.minHealth:p.minHealth,e.overrideLimits?e.limits.maxHealth:p.maxHealth))
        return false;
    if (!p.onlyRules.empty()) {
        const RuleObservation obs{hero, hp, maxHp, visible};
        for (const auto& r : p.onlyRules) {
            if (!IsSpatialRule(r) && !RuleMatches(r, obs))
                return false;
        }
        return true;
    }
    return p.mode != 2 || PredicateMatches(p, hero, hp, maxHp, visible);
}
inline bool Eligible(const Selection& p,const Effective& e,const RuleObservation& obs) {
    if (!HealthPasses(obs.hp,e.overrideLimits?e.limits.minHealth:p.minHealth,e.overrideLimits?e.limits.maxHealth:p.maxHealth))
        return false;
    if (!p.onlyRules.empty()) {
        for (const auto& r : p.onlyRules) {
            if (!RuleMatches(r, obs))
                return false;
        }
        return true;
    }
    return p.mode != 2 || PredicateMatches(p, obs.hero, obs.hp, obs.maxHp, obs.visible);
}
inline bool Eligible(const Selection& p,const Effective& e,uint64_t hero,float hp,float maxHp,bool visible) {
    return EligiblePreGeometry(p, e, hero, hp, maxHp, visible);
}
inline bool Preferred(const Selection& p, const RuleObservation& obs) {
    if (!p.preferRules.empty()) {
        for (const auto& r : p.preferRules) {
            if (RuleMatches(r, obs))
                return true;
        }
        return false;
    }
    return p.mode == 1 && PredicateMatches(p, obs.hero, obs.hp, obs.maxHp, obs.visible);
}
inline float Score(int priority,const Selection& p,float angle,float distance,float hp,
                   float maxHp,float commonFov,float commonDistance) {
    if(!std::isfinite(angle)||!std::isfinite(distance)||!std::isfinite(hp))
        return (std::numeric_limits<float>::infinity)();
    if(priority==0) return angle;
    if(priority==1) return hp;
    if(priority==2) return distance;
    const float a=Finite(p.angleWeight,1,0,100), d=Finite(p.distanceWeight,0,0,100),
                h=Finite(p.healthWeight,0,0,100), sum=a+d+h;
    if(sum<=0) return angle/(std::max)(commonFov,0.001f);
    const float hpFraction=std::isfinite(maxHp)&&maxHp>0 ? (std::clamp)(hp/maxHp,0.0f,1.0f) : 1.0f;
    return (a*(std::clamp)(angle/(std::max)(commonFov,0.001f),0.0f,1.0f)+
            d*(std::clamp)(distance/(std::max)(commonDistance,0.001f),0.0f,1.0f)+h*hpFraction)/sum;
}
// Stable IDs settle equal scores independently of entity traversal order.
inline bool Better(bool preferred,float score,uint64_t key,bool bestPreferred,float bestScore,uint64_t bestKey) {
    if(!std::isfinite(score)) return false;
    return preferred!=bestPreferred ? preferred : score<bestScore || (score==bestScore && key<bestKey);
}
inline void NormalizeMotion(Motion& m) {
    const Motion d;
    AimResponseCurve::Normalize(m.responseCurve);
    m.method=(std::clamp)(m.method,0,kMotionMethodCount-1);
    m.angularSpeedScale=Finite(m.angularSpeedScale,100,0,200);
    m.pidP=Finite(m.pidP,d.pidP,0,10); m.pidI=Finite(m.pidI,d.pidI,0,10);
    m.pidD=Finite(m.pidD,d.pidD,0,10); m.pidMaxIntegral=Finite(m.pidMaxIntegral,10,0,1000);
    m.pidDeadzone=Finite(m.pidDeadzone,1,0,100);
    m.bezierControlPoints=(std::clamp)(m.bezierControlPoints,2,6);
    m.bezierCurvature=Finite(m.bezierCurvature,0.5f,0,1); m.bezierSpeed=Finite(m.bezierSpeed,50,0,200);
    m.piecewiseNearDegrees=Finite(m.piecewiseNearDegrees,2,0,180);
    m.piecewiseMidDegrees=Finite(m.piecewiseMidDegrees,6,m.piecewiseNearDegrees,180);
    m.piecewiseFarDegrees=Finite(m.piecewiseFarDegrees,12,m.piecewiseMidDegrees,180);
    m.piecewiseNearScale=Finite(m.piecewiseNearScale,.2f,0,1);
    m.piecewiseMidScale=Finite(m.piecewiseMidScale,.45f,0,1);
    m.piecewiseFarScale=Finite(m.piecewiseFarScale,.75f,0,1);
    m.accelLimitedAcceleration=Finite(m.accelLimitedAcceleration,.1f,0,20);
    m.constantAngularSpeedDeg=Finite(m.constantAngularSpeedDeg,30,0,7200);
    m.hybridConstantSpeedDeg=Finite(m.hybridConstantSpeedDeg,45,0,7200);
    m.hybridMaxSpeedDeg=Finite(m.hybridMaxSpeedDeg,720,0,7200);
    m.hybridAccelerationDeg=Finite(m.hybridAccelerationDeg,1800,0,100000);
    m.hybridDecelerationDeg=Finite(m.hybridDecelerationDeg,2600,0,100000);
    m.hybridNearRadiusDeg=Finite(m.hybridNearRadiusDeg,3,.001f,180);
    m.hybridTargetMotionGain=Finite(m.hybridTargetMotionGain,.35f,0,10);
    m.hybridSuddenMotionBoost=Finite(m.hybridSuddenMotionBoost,2.5f,0,10);
    m.hybridDeadzoneDeg=Finite(m.hybridDeadzoneDeg,.1f,0,180);
    // Core normalizes every parameter before use. Persist the same finite ranges.
    auto sane=[](double& v,double def,double lo,double hi) { v=(std::clamp)(std::isfinite(v)?v:def,lo,hi); };
    auto& s=m.shuwu; const koi::ShuwuConfig q;
    sane(s.kp,q.kp,0,2); sane(s.kd,q.kd,0,1);
    sane(s.predict_x,q.predict_x,-2,3); sane(s.predict_y,q.predict_y,-2,3);
    sane(s.rate,q.rate,.01,1); sane(s.smooth,q.smooth,0,1);
    sane(s.deadzone,q.deadzone,0,10); sane(s.reset_jump,q.reset_jump,5,160);
    sane(s.integral_threshold,q.integral_threshold,1,320); sane(s.integral_rate,q.integral_rate,.001,1);
    sane(s.kp_threshold,q.kp_threshold,1,10000); sane(s.gain_decay,q.gain_decay,.001,1);
    sane(s.velocity_q,q.velocity_q,.0001,10); sane(s.velocity_r,q.velocity_r,.0001,10);
    sane(s.integral_q,q.integral_q,.0001,10); sane(s.integral_r,q.integral_r,.0001,10);
    sane(s.micro_error,q.micro_error,0,10); sane(s.micro_diff,q.micro_diff,0,5);
    sane(s.ki_deadband,q.ki_deadband,0,10); sane(s.soft_a,q.soft_a,0,2); sane(s.soft_b,q.soft_b,0,2);
    sane(s.ki_scale,q.ki_scale,10,100000); sane(s.max_move_per_frame,q.max_move_per_frame,1,100);
    sane(s.suppression_x,q.suppression_x,0,3); sane(s.suppression_y,q.suppression_y,0,3);
    sane(s.soft_lock_x,q.soft_lock_x,0,3); sane(s.soft_lock_y,q.soft_lock_y,0,3);
    sane(s.soft_lock_delay_ms,q.soft_lock_delay_ms,0,5000);
    // Soft lock has no separate activation binding in UN. Do not invent one.
    s.soft_lock_enabled=false;
}
inline void NormalizeIds(std::vector<uint64_t>& ids) {
    if(ids.size()>kMaxHeroes) ids.resize(kMaxHeroes);
    ids.erase(std::remove(ids.begin(),ids.end(),0),ids.end());
    std::sort(ids.begin(),ids.end()); ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
}
inline void NormalizeRule(SelectionRule& r) {
    r.measure = (std::clamp)(r.measure, 0, kRuleMeasureCount - 1);
    if (r.measure == static_cast<int>(RuleMeasure::Hero)) {
        NormalizeIds(r.heroes);
        r.minValue = 0.0f;
        r.maxValue = 0.0f;
    } else {
        r.heroes.clear();
        if (r.measure == static_cast<int>(RuleMeasure::Health)) {
            r.minValue = Finite(r.minValue, 0.0f, 0.0f, 100000.0f);
            r.maxValue = Finite(r.maxValue, 0.0f, 0.0f, 100000.0f);
            if (r.maxValue > 0.0f) r.maxValue = (std::max)(r.minValue, r.maxValue);
        } else if (r.measure == static_cast<int>(RuleMeasure::Distance)) {
            r.minValue = Finite(r.minValue, 0.0f, 0.0f, 500.0f);
            r.maxValue = Finite(r.maxValue, 0.0f, 0.0f, 500.0f);
            if (r.maxValue > 0.0f) r.maxValue = (std::max)(r.minValue, r.maxValue);
        } else if (r.measure == static_cast<int>(RuleMeasure::Angle)) {
            r.minValue = Finite(r.minValue, 0.0f, 0.0f, 180.0f);
            r.maxValue = Finite(r.maxValue, 0.0f, 0.0f, 180.0f);
            if (r.maxValue > 0.0f) r.maxValue = (std::max)(r.minValue, r.maxValue);
        } else {
            r.minValue = 0.0f;
            r.maxValue = 0.0f;
        }
    }
}
inline void MaterializeLegacyRules(Selection& s) {
    if (s.onlyRules.empty() && s.mode == 2) {
        SelectionRule legacy{};
        legacy.measure = (std::clamp)(s.predicate, 0, 2);
        if (legacy.measure == 0) legacy.heroes = s.heroes;
        NormalizeRule(legacy);
        s.onlyRules.push_back(std::move(legacy));
    }
    if (s.preferRules.empty() && s.mode == 1) {
        SelectionRule legacy{};
        legacy.measure = (std::clamp)(s.predicate, 0, 2);
        if (legacy.measure == 0) legacy.heroes = s.heroes;
        NormalizeRule(legacy);
        s.preferRules.push_back(std::move(legacy));
    }
}
inline void Normalize(Plan& p) {
    auto& s=p.selection; s.mode=(std::clamp)(s.mode,0,2); s.predicate=(std::clamp)(s.predicate,0,2);
    NormalizeIds(s.heroes); s.angleWeight=Finite(s.angleWeight,1,0,100);
    s.distanceWeight=Finite(s.distanceWeight,0,0,100); s.healthWeight=Finite(s.healthWeight,0,0,100);
    s.minHealth=Finite(s.minHealth,0,0,100000); s.maxHealth=Finite(s.maxHealth,0,0,100000);
    if(s.maxHealth>0) s.maxHealth=(std::max)(s.minHealth,s.maxHealth);
    if (s.onlyRules.size() > kMaxSelectionRules) s.onlyRules.resize(kMaxSelectionRules);
    if (s.preferRules.size() > kMaxSelectionRules) s.preferRules.resize(kMaxSelectionRules);
    for (auto& r : s.onlyRules) NormalizeRule(r);
    for (auto& r : s.preferRules) NormalizeRule(r);
    NormalizeMotion(p.motion);
    if(p.groups.size()>kMaxGroups) p.groups.resize(kMaxGroups);
    std::vector<uint64_t> seen, ids;
    for(auto& g:p.groups) {
        if(!g.id || Contains(ids,g.id)) { g.id=1; while(Contains(ids,g.id)) ++g.id; }
        ids.push_back(g.id);
        if(g.name.empty()) g.name="Hero exception";
        if(g.name.size()>96) g.name.resize(96);
        NormalizeIds(g.heroes);
        // First persisted group wins; later duplicates fall back to inheritance.
        std::erase_if(g.heroes,[&](uint64_t id) { return Contains(seen,id); });
        seen.insert(seen.end(),g.heroes.begin(),g.heroes.end());
        auto& l=g.limits;
        l.minDistance=Finite(l.minDistance,0,0,500); l.maxDistance=Finite(l.maxDistance,100,0,500);
        if(l.maxDistance>0) l.maxDistance=(std::max)(l.minDistance,l.maxDistance);
        l.fov=Finite(l.fov,15,0,180); l.minHealth=Finite(l.minHealth,0,0,100000);
        l.maxHealth=Finite(l.maxHealth,0,0,100000);
        if(l.maxHealth>0) l.maxHealth=(std::max)(l.minHealth,l.maxHealth);
        g.point.boneMask &= 0x3ff; if(!g.point.boneMask) g.point.boneMask=1;
        g.point.predictionMode=(std::clamp)(g.point.predictionMode,0,2);
        NormalizeMotion(g.motion);
    }
}
} // namespace OW::AimPlan
