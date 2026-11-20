#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Game/PredictionAlgorithm.hpp"

namespace OW {

inline constexpr float kDefaultInstantHitRadius = 0.07f;
inline constexpr float kMaxPredictionProjectileRadius = 2.0f;

struct ProjectileSpec {
    bool enabledByDefault = false;
    float projectileSpeed = 0.0f;
    bool gravity = false;
    float projectileRadius = 0.0f;
    float chargeMinSpeed = 0.0f;
    float chargeMaxSpeed = 0.0f;
    bool directHitOnly = true;
    float launchDirectionUpBias = 0.0f;
    float gravityAcceleration = 9.8f;
    float castDelayMs = 0.0f;
};

struct PredictionProfile {
    std::string profileId = "default";
    int configVersion = 1;
    ProjectileSpec projectile{};
    std::optional<bool> inheritInstantHitRadius;
    uint64_t heroId = 0;
    int actionId = 0;
    std::string algorithmProfileId = "ballistic_intercept";
    int algorithm = static_cast<int>(PredictionAlgorithm::BallisticIntercept);
};

} // namespace OW
