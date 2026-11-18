#pragma once

#include <cmath>
#include <optional>

#include "Game/AimArchitecture.hpp"
#include "Game/HeroGeometrySpec.hpp"

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

inline std::optional<float> SelectedAimNativeHitScore(const TargetCandidate& candidate,
    const Vector3& viewAngle, const Vector3& camera, const Vector3& aimPoint,
    const Vector3& sampleDisplacement = {})
{
    if (!candidate.valid || candidate.nativeHitWindow.projectileRadius < 0.0f ||
        (candidate.aimPoint + sampleDisplacement).DistTo(aimPoint) > 0.0001f)
        return std::nullopt;

    const float horizontal = std::cos(viewAngle.X);
    Vector3 direction(horizontal * std::sin(viewAngle.Y), -std::sin(viewAngle.X),
                      horizontal * std::cos(viewAngle.Y));
    const float distance = camera.DistTo(aimPoint);
    if (!std::isfinite(distance) || distance <= 0.0001f)
        return std::nullopt;
    direction = direction * distance;
    return NativeHitWindowScore(candidate.entitySnapshot, candidate.boneId,
        GetTickCount(), camera, direction, candidate.nativeHitWindow, sampleDisplacement);
}

inline bool InSelectedAimHitWindow(const TargetCandidate& candidate,
    const Vector3& viewAngle, const Vector3& targetAngle, const Vector3& camera,
    const Vector3& aimPoint)
{
    if (const auto score = SelectedAimNativeHitScore(candidate, viewAngle, camera, aimPoint))
        return *score <= 1.0f;
    return HitWindowAngleInside(viewAngle, targetAngle, camera, aimPoint,
        candidate.effectiveHitWindow);
}

} // namespace OW
