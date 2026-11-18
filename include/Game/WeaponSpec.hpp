#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "Game/AimArchitecture.hpp"

namespace OW {

const WeaponSpec* ResolveWeaponSpec(uint64_t heroId, int action = 0);
const WeaponSpec* ResolveDefaultWeaponSpec(uint64_t heroId);
PredictionProfile ResolvePredictionProfile(const WeaponSpec* weapon);
PredictionProfile ResolvePredictionProfile(std::string_view profileId,
                                           uint64_t heroId,
                                           int actionId,
                                           const ProjectileSpec& fallback);
std::vector<PredictionProfile> SnapshotPredictionProfiles();
bool UpdatePredictionProfile(const PredictionProfile& profile);
const WeaponSpec* WeaponSpecsBegin();
const WeaponSpec* WeaponSpecsEnd();
std::size_t WeaponSpecCount();

bool HeroHasAttackAction(uint64_t heroId, int action);
bool HeroUsesScopedStanceActions(uint64_t heroId);
const char* AttackActionNameForHero(uint64_t heroId, int action);
const char* AttackActionCompactNameForHero(uint64_t heroId, int action);

const char* AimClassName(AimClass value);
const char* AimBehaviorName(AimBehaviorType value);
const char* FirePolicyName(FirePolicyType value);
const char* PredictionOverrideName(PredictionOverrideMode value);
const char* RuntimeVariantRequirementName(RuntimeVariantRequirement value);

int MouseButtonForAttackAction(int action);
uint32_t FireKeyMaskForMouseButton(int button);
uint32_t FireKeyMaskForAttackAction(int action);
int ResolveGeneratedFireMouseButton(const WeaponSpec* weapon, int fallbackAction);
int ResolveTrackingHoldMouseButton(const WeaponSpec* weapon, int fallbackAction);
uint32_t ResolveGeneratedFireKeyMask(const WeaponSpec* weapon, int fallbackAction);

} // namespace OW
