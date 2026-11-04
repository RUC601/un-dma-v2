#pragma once
#include "Game/Decrypt.hpp"
#include "Game/Entity.hpp"

namespace OW {
inline float WeaponChargePercentFromSample(bool available,float fraction) {
    return available && std::isfinite(fraction) && fraction>=0 && fraction<=1 ? fraction*100.f : -1.f;
}
// Weapon telemetry only. The behavior configuration decides press versus release.
inline float ReadWeaponChargePercent(const c_entity& local,int action) {
    if(local.HeroID!=eHero::HERO_HANJO || action!=0 || local.skill2act || !local.SkillBase) return -1.f;
    float fraction=0;
    const bool available=TryReadUlt(local.SkillBase+0x40,0xB,0x2A5,fraction);
    return WeaponChargePercentFromSample(available,fraction);
}
}
