#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include "Game/AimArchitecture.hpp"
#include "Game/BoneSlots.hpp"
#include "Game/HeroGeometrySpec.hpp"
#include "Game/TriggerBoneSelection.hpp"

namespace OW {

inline bool HitWindowAngleInside(const Vector3& viewAngle,
                                 const Vector3& targetAngle,
                                 const Vector3& camera,
                                 const Vector3& aimPoint,
                                 float radius)
{
    const float distance = camera.DistTo(aimPoint);
    if (!std::isfinite(distance) || distance <= 0.0001f || !std::isfinite(radius) || radius <= 0.0f)
        return false;
    return viewAngle.DistTo(targetAngle) <= radius / distance;
}

inline Vector3 HitWindowDirectionFromEuler(const Vector3& viewAngle)
{
    const float horizontal = std::cos(viewAngle.X);
    return Vector3(
        horizontal * std::sin(viewAngle.Y),
        -std::sin(viewAngle.X),
        horizontal * std::cos(viewAngle.Y));
}

inline Vector3 HitWindowEulerFromDirection(const Vector3& unitDirection)
{
    return Vector3(
        -std::asin(std::clamp(unitDirection.Y, -1.0f, 1.0f)),
        std::atan2(unitDirection.X, unitDirection.Z),
        0.0f);
}

inline std::optional<float> SelectedAimNativeHitScore(const TargetCandidate& candidate,
    const Vector3& viewAngle, const Vector3& camera, const Vector3& aimPoint,
    const Vector3& sampleDisplacement = {})
{
    if (!candidate.valid || candidate.nativeHitWindow.projectileRadius < 0.0f ||
        (candidate.aimPoint + sampleDisplacement).DistTo(aimPoint) > 0.0001f)
        return std::nullopt;

    const float distance = camera.DistTo(aimPoint);
    if (!std::isfinite(distance) || distance <= 0.0001f)
        return std::nullopt;
    const Vector3 direction = HitWindowDirectionFromEuler(viewAngle) * distance;
    return NativeHitWindowScore(candidate.entitySnapshot, candidate.boneId,
        GetTickCount(), camera, direction, candidate.nativeHitWindow, sampleDisplacement);
}

inline bool InSelectedAimHitWindow(const TargetCandidate& candidate,
    const Vector3& viewAngle, const Vector3& targetAngle, const Vector3& camera,
    const Vector3& aimPoint)
{
    if (NativeHitboxSampleUsable(candidate.entitySnapshot, GetTickCount())) {
        const auto score = SelectedAimNativeHitScore(candidate, viewAngle, camera, aimPoint);
        if (!score)
            return false;
        return *score <= 1.0f;
    }
    // Keep bone fallback contact centered on the true predicted hitbox rather
    // than any reverse-lateral steering bias applied to candidate.aimPoint.
    if (candidate.valid &&
        std::isfinite(candidate.predictedAimPoint.X) &&
        std::isfinite(candidate.predictedAimPoint.Y) &&
        std::isfinite(candidate.predictedAimPoint.Z) &&
        (candidate.predictedAimPoint.X != 0.0f ||
         candidate.predictedAimPoint.Y != 0.0f ||
         candidate.predictedAimPoint.Z != 0.0f) &&
        candidate.aimPoint.DistTo(aimPoint) <= 0.0001f) {
        const Vector3 delta = candidate.predictedAimPoint - camera;
        const float dist = delta.Size();
        if (std::isfinite(dist) && dist > 0.0001f) {
            const Vector3 centerAngle = HitWindowEulerFromDirection(delta / dist);
            return HitWindowAngleInside(viewAngle, centerAngle, camera,
                candidate.predictedAimPoint, candidate.effectiveHitWindow);
        }
    }
    return HitWindowAngleInside(viewAngle, targetAngle, camera, aimPoint,
        candidate.effectiveHitWindow);
}

inline bool InAllowedAimHitWindow(const TargetCandidate& candidate,
    const Vector3& viewAngle, const Vector3& targetAngle, const Vector3& camera,
    const Vector3& aimPoint, SkeletonBoneMask allowedBoneMask,
    const Vector3& sampleDisplacement = {})
{
    if (!candidate.valid)
        return false;
    if (allowedBoneMask == 0)
        return InSelectedAimHitWindow(candidate, viewAngle, targetAngle, camera, aimPoint);

    const uint32_t now = GetTickCount();
    if (NativeHitboxSampleUsable(candidate.entitySnapshot, now) &&
        candidate.nativeHitWindow.projectileRadius >= 0.0f &&
        (candidate.aimPoint + sampleDisplacement).DistTo(aimPoint) <= 0.0001f) {
        const float distance = camera.DistTo(aimPoint);
        if (!std::isfinite(distance) || distance <= 0.0001f)
            return false;
        const Vector3 direction = HitWindowDirectionFromEuler(viewAngle) * distance;
        bool checkedSelectedBone = false;
        for (std::size_t slot = 0; slot < Plexies20260609::kSkeletonSlotCount; ++slot) {
            if (!SkeletonBoneMaskIncludesRenderSlot(allowedBoneMask, slot))
                continue;
            const int boneId = Plexies20260609::HitboxBoneIdForRenderSlot(slot);
            if (boneId == Plexies20260609::kUnusedRenderSkeletonBone)
                continue;
            if (boneId == candidate.boneId)
                checkedSelectedBone = true;
            const auto score = NativeHitWindowScore(candidate.entitySnapshot, boneId,
                now, camera, direction, candidate.nativeHitWindow, sampleDisplacement);
            if (score && *score <= 1.0f)
                return true;
        }
        if (!checkedSelectedBone) {
            const auto score = NativeHitWindowScore(candidate.entitySnapshot, candidate.boneId,
                now, camera, direction, candidate.nativeHitWindow, sampleDisplacement);
            if (score && *score <= 1.0f)
                return true;
        }
        return false;
    }

    return InSelectedAimHitWindow(candidate, viewAngle, targetAngle, camera, aimPoint);
}

} // namespace OW
