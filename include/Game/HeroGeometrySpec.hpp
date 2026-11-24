#pragma once

#include <cstdint>
#include <optional>

#include "Game/AimArchitecture.hpp"

namespace OW {

struct HitboxSegment {
    Vector3 a{};
    Vector3 b{};
    float radius = 0.0f;
};

struct ProjectileTrajectory {
    Vector3 origin{};
    Vector3 initialVelocity{};
    Vector3 targetVelocity{};
    Vector3 acceleration{};
    float maxTimeSeconds = 0.0f;
};

bool ContinuousTrajectoryIntersectsHitbox(const ProjectileTrajectory& trajectory,
                                          const HitboxSegment& hitbox,
                                          float projectileRadius,
                                          float* hitTimeSeconds = nullptr);

const HeroGeometrySpec* ResolveHeroGeometrySpec(uint64_t heroId);
const BoneHitboxSpec* ResolveBoneHitboxSpec(uint64_t heroId, int boneId);
float ResolveBoneHitboxRadius(uint64_t heroId, int boneId, float fallbackRadius);
float EffectiveExpandedHitboxRadius(float shapeRadius,
                                    float projectileRadius,
                                    float hitboxScalePercent);
// Bounded reverse-lateral safety margin offset applied only to the desired
// steering aim point (never to actual Hitbox contact geometry).
// Returns a world-space offset opposite to the target's lateral motion,
// bounded within half of the scaled effective radius and within the lateral
// lead displacement. Returns zero for stationary or purely radial motion.
Vector3 ComputeReverseLateralAimOffset(const Vector3& camera,
                                       const Vector3& rawAimPoint,
                                       const Vector3& predictedAimPoint,
                                       const Vector3& targetVelocity,
                                       float effectiveHitRadius,
                                       float projectileRadius,
                                       float hitboxScalePercent);
float ResolveEffectiveHitWindow(uint64_t heroId,
                                int boneId,
                                const WeaponSpec* weapon,
                                float hitboxScalePercent,
                                float fallbackBoneRadius,
                                float projectileRadiusOverride = -1.0f);
float ResolveEffectiveHitWindow(const c_entity& entity,
                                int boneId,
                                const WeaponSpec* weapon,
                                float hitboxScalePercent,
                                float fallbackBoneRadius,
                                float projectileRadiusOverride = -1.0f);
int AimBoneToSkeletonBoneId(int aimBone);
bool IsCoreHitboxBoneId(int boneId);
Vector3 ResolveBoneHitboxOffsetLocal(uint64_t heroId, int boneId);
Vector3 ResolveBoneHitboxCenter(const c_entity& entity, int boneId, const Vector3& bonePoint);
bool TryResolveNativeHitboxCenter(const c_entity& entity, int boneId, Vector3& outPoint);
bool NativeHitboxSampleUsable(const c_entity& entity, uint32_t now);
// Distance to the closest associated shape's axis divided by its expanded radius.
// A score of 1 is contact. An absent score means the caller should use its bone fallback.
std::optional<float> NativeHitWindowScore(const c_entity& entity, int boneId, uint32_t now,
    const Vector3& origin, const Vector3& direction, const Vector3& predictedDisplacement,
    float projectileRadius, float hitboxScalePercent);
std::optional<float> NativeHitWindowScore(const c_entity& entity, int boneId, uint32_t now,
    const Vector3& origin, const Vector3& direction, const NativeHitWindowInput& input,
    const Vector3& extraDisplacement = {});

} // namespace OW
