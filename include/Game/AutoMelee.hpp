#pragma once

#include "Game/FovGeometry.hpp"

namespace OW::AutoMelee {

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
