#include "Game/HeroGeometrySpec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <DirectXMath.h>

#include "Game/Structs.hpp"
#include "Game/PresentationTiming.hpp"
#include "Utils/Config.hpp"

namespace OW {
namespace {

using DirectX::XMFLOAT3;
using DirectX::XMMATRIX;
using DirectX::XMMatrixRotationY;
using DirectX::XMLoadFloat3;
using DirectX::XMStoreFloat3;
using DirectX::XMVector3Transform;

constexpr std::string_view kGeometryDataNote =
    "Compiled fallback geometry; evidence: research/weapon-data-sources-20261104/legacy-v2-data; conservative non-public measurements.";

constexpr std::array<BoneHitboxSpec, 18> kFallbackBones = {
    BoneHitboxSpec{ BONE_HEAD, "head", 0.16f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_NECK, "neck", 0.14f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_CHEST, "chest", 0.22f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_BODY, "body", 0.22f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_BODY_BOT, "lower_body", 0.20f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_PELVIS, "pelvis", 0.20f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_R_SHOULDER, "right_shoulder", 0.13f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_L_SHOULDER, "left_shoulder", 0.13f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_R_ELBOW, "right_elbow", 0.11f, Vector3{}, "sphere", "", kGeometryDataNote, 0.15f },
    BoneHitboxSpec{ BONE_L_ELBOW, "left_elbow", 0.11f, Vector3{}, "sphere", "", kGeometryDataNote, 0.15f },
    BoneHitboxSpec{ BONE_R_HAND, "right_hand", 0.10f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_L_HAND, "left_hand", 0.10f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_R_KNEE, "right_knee", 0.12f, Vector3{}, "sphere", "", kGeometryDataNote, 0.15f },
    BoneHitboxSpec{ BONE_L_KNEE, "left_knee", 0.12f, Vector3{}, "sphere", "", kGeometryDataNote, 0.15f },
    BoneHitboxSpec{ BONE_R_SHANK, "right_shank", 0.11f, Vector3{}, "sphere", "", kGeometryDataNote, 0.15f },
    BoneHitboxSpec{ BONE_L_SHANK, "left_shank", 0.11f, Vector3{}, "sphere", "", kGeometryDataNote, 0.15f },
    BoneHitboxSpec{ BONE_R_ANKLE, "right_ankle", 0.10f, Vector3{}, "sphere", "", kGeometryDataNote, 0.15f },
    BoneHitboxSpec{ BONE_L_ANKLE, "left_ankle", 0.10f, Vector3{}, "sphere", "", kGeometryDataNote, 0.15f },
};

constexpr HeroGeometrySpec kFallbackGeometry{
    0,
    "fallback",
    kFallbackBones.data(),
    static_cast<int>(kFallbackBones.size())
};

bool IsZeroOffset(const Vector3& value)
{
    return value.X == 0.0f && value.Y == 0.0f && value.Z == 0.0f;
}

bool IsFiniteVector(const Vector3& value)
{
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}

bool NativeHitboxSampleUsableInternal(const c_entity& entity, uint32_t now)
{
    const auto& sample = entity.native_hitboxes;
    return sample.Matches(entity.VelocityBase, entity.skeleton_revision) &&
        PresentationTiming::Fresh(sample.tick, now);
}

float Dot(const Vector3& a, const Vector3& b)
{
    return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}

float RayAxisDistanceSquared(const Vector3& a, const Vector3& b, const Vector3& unitRay)
{
    const auto distanceToRay = [&](const Vector3& point) {
        const auto perpendicular = point - unitRay * (std::max)(0.0f, Dot(point, unitRay));
        return Dot(perpendicular, perpendicular);
    };
    const Vector3 axis = b - a;
    const float axisLengthSquared = Dot(axis, axis);
    if (axisLengthSquared <= 0.00000001f)
        return distanceToRay(a);

    const float closestToOrigin = std::clamp(-Dot(a, axis) / axisLengthSquared, 0.0f, 1.0f);
    float distance = (std::min)({distanceToRay(a), distanceToRay(b),
        distanceToRay(a + axis * closestToOrigin)});
    const Vector3 projectedAxis = axis - unitRay * Dot(axis, unitRay);
    const float projectedLengthSquared = Dot(projectedAxis, projectedAxis);
    if (projectedLengthSquared > 0.00000001f) {
        const float closestOnAxis = std::clamp(-Dot(a, projectedAxis) / projectedLengthSquared, 0.0f, 1.0f);
        distance = (std::min)(distance, distanceToRay(a + axis * closestOnAxis));
    }
    return distance;
}

float DistanceSquaredPointSegment(const Vector3& point, const Vector3& a, const Vector3& b)
{
    const Vector3 axis = b - a;
    const float lengthSquared = Dot(axis, axis);
    if (lengthSquared <= 0.00000001f)
        return point.DistTo(a) * point.DistTo(a);
    const float t = std::clamp(Dot(point - a, axis) / lengthSquared, 0.0f, 1.0f);
    const Vector3 closest = a + axis * t;
    const Vector3 delta = point - closest;
    return Dot(delta, delta);
}

float DistanceSquaredSegmentSegment(const Vector3& p1, const Vector3& q1,
                                    const Vector3& p2, const Vector3& q2)
{
    const Vector3 d1 = q1 - p1;
    const Vector3 d2 = q2 - p2;
    const Vector3 r = p1 - p2;
    const float a = Dot(d1, d1);
    const float e = Dot(d2, d2);
    const float f = Dot(d2, r);
    float s = 0.0f;
    float t = 0.0f;
    constexpr float epsilon = 0.00000001f;
    if (a <= epsilon && e <= epsilon)
        return Dot(r, r);
    if (a <= epsilon) {
        s = 0.0f;
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = Dot(d1, r);
        if (e <= epsilon) {
            t = 0.0f;
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = Dot(d1, d2);
            const float denominator = a * e - b * b;
            if (denominator != 0.0f)
                s = std::clamp((b * f - c * e) / denominator, 0.0f, 1.0f);
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    const Vector3 delta = r + d1 * s - d2 * t;
    return Dot(delta, delta);
}

Vector3 TrajectoryPosition(const ProjectileTrajectory& trajectory, float time)
{
    return trajectory.origin + (trajectory.initialVelocity - trajectory.targetVelocity) * time +
        trajectory.acceleration * (0.5f * time * time);
}

bool ContinuousTrajectoryIntersectsHitboxInternal(const ProjectileTrajectory& trajectory,
                                          const HitboxSegment& hitbox,
                                          float projectileRadius,
                                          float* hitTimeSeconds)
{
    if (hitTimeSeconds)
        *hitTimeSeconds = 0.0f;
    if (!IsFiniteVector(trajectory.origin) || !IsFiniteVector(trajectory.initialVelocity) ||
        !IsFiniteVector(trajectory.acceleration) || !IsFiniteVector(hitbox.a) ||
        !IsFiniteVector(hitbox.b) || !std::isfinite(hitbox.radius) || hitbox.radius <= 0.0f ||
        !std::isfinite(projectileRadius) || projectileRadius < 0.0f ||
        !std::isfinite(trajectory.maxTimeSeconds) || trajectory.maxTimeSeconds <= 0.0f)
        return false;

    const float radius = hitbox.radius + projectileRadius;
    const float radiusSquared = radius * radius;
    const float accelerationMagnitude = trajectory.acceleration.Size();
    constexpr int maxDepth = 18;
    constexpr float minimumInterval = 1.0f / 4096.0f;
    const auto distanceToAxisSquared = [&](const Vector3& point) {
        return DistanceSquaredPointSegment(point, hitbox.a, hitbox.b);
    };
    const auto search = [&](const auto& self, float start, float end, int depth) -> bool {
        const Vector3 startPoint = TrajectoryPosition(trajectory, start);
        const Vector3 endPoint = TrajectoryPosition(trajectory, end);
        if (distanceToAxisSquared(startPoint) <= radiusSquared) {
            if (hitTimeSeconds) *hitTimeSeconds = start;
            return true;
        }
        if (distanceToAxisSquared(endPoint) <= radiusSquared) {
            if (hitTimeSeconds) *hitTimeSeconds = end;
            return true;
        }

        const float chordDistanceSquared = DistanceSquaredSegmentSegment(
            startPoint, endPoint, hitbox.a, hitbox.b);
        const float interval = end - start;
        const float curveDeviation = accelerationMagnitude * interval * interval * 0.125f;
        const float lowerDistance = std::sqrt((std::max)(0.0f, chordDistanceSquared)) - curveDeviation;
        if (lowerDistance > radius)
            return false;

        const float middle = (start + end) * 0.5f;
        const Vector3 middlePoint = TrajectoryPosition(trajectory, middle);
        if (distanceToAxisSquared(middlePoint) <= radiusSquared) {
            if (hitTimeSeconds) *hitTimeSeconds = middle;
            return true;
        }
        if (depth >= maxDepth || interval <= minimumInterval)
            return false;
        return self(self, start, middle, depth + 1) || self(self, middle, end, depth + 1);
    };
    return search(search, 0.0f, trajectory.maxTimeSeconds, 0);
}

Vector3 NativeShapeCenter(const c_entity& entity,
                          const ModelShapes::Shape& shape,
                          const ModelShapes::Pose& pose)
{
    const Vector3 local = shape.a + (shape.b - shape.a) * 0.5f;
    return entity.native_hitboxes.world.TransformPoint(ModelShapes::Transform(local, pose)) +
        (entity.pos - entity.native_hitboxes.world.sampleAnchor);
}

Vector3 NativeBoneReference(const c_entity& entity, int boneId)
{
    switch (boneId) {
    case BONE_HEAD: return entity.head_pos;
    case BONE_NECK: return entity.neck_pos;
    case BONE_CHEST: return entity.chest_pos;
    default: return entity.pos;
    }
}

} // namespace

bool NativeHitboxSampleUsable(const c_entity& entity, uint32_t now)
{
    return NativeHitboxSampleUsableInternal(entity, now);
}

bool ContinuousTrajectoryIntersectsHitbox(const ProjectileTrajectory& trajectory,
                                          const HitboxSegment& hitbox,
                                          float projectileRadius,
                                          float* hitTimeSeconds)
{
    return ContinuousTrajectoryIntersectsHitboxInternal(
        trajectory, hitbox, projectileRadius, hitTimeSeconds);
}

const HeroGeometrySpec* ResolveHeroGeometrySpec(uint64_t heroId)
{
    (void)heroId;
    return &kFallbackGeometry;
}

const BoneHitboxSpec* ResolveBoneHitboxSpec(uint64_t heroId, int boneId)
{
    const HeroGeometrySpec* geometry = ResolveHeroGeometrySpec(heroId);
    if (!geometry || !geometry->bones || geometry->boneCount <= 0)
        return nullptr;

    for (int index = 0; index < geometry->boneCount; ++index) {
        const BoneHitboxSpec& bone = geometry->bones[index];
        if (bone.boneId == boneId)
            return &bone;
    }

    return nullptr;
}

float ResolveBoneHitboxRadius(uint64_t heroId, int boneId, float fallbackRadius)
{
    const BoneHitboxSpec* spec = ResolveBoneHitboxSpec(heroId, boneId);
    if (spec && spec->radiusWorld > 0.0f)
        return spec->radiusWorld;
    return (std::max)(0.0f, fallbackRadius);
}

float ResolveEffectiveHitWindow(uint64_t heroId,
                                int boneId,
                                const WeaponSpec* weapon,
                                float hitboxScalePercent,
                                float fallbackBoneRadius,
                                float projectileRadiusOverride)
{
    const float boneRadius = ResolveBoneHitboxRadius(heroId, boneId, fallbackBoneRadius);
    const float projectileRadius = projectileRadiusOverride >= 0.0f
        ? (std::max)(0.0f, projectileRadiusOverride)
        : (weapon ? (std::max)(0.0f, weapon->projectile.projectileRadius) : 0.0f);
    const float resolvedWindow = (std::max)(0.0f, boneRadius + projectileRadius);
    return resolvedWindow * Config::HitboxScaleMultiplier(hitboxScalePercent);
}

float ResolveEffectiveHitWindow(const c_entity& entity,
                                int boneId,
                                const WeaponSpec* weapon,
                                float hitboxScalePercent,
                                float fallbackBoneRadius,
                                float projectileRadiusOverride)
{
    float boneRadius = ResolveBoneHitboxRadius(entity.HeroID, boneId, fallbackBoneRadius);
    if (NativeHitboxSampleUsableInternal(entity, GetTickCount())) {
        const Vector3 reference = NativeBoneReference(entity, boneId);
        float nearestDistance = (std::numeric_limits<float>::max)();
        float nativeRadius = 0.0f;
        for (size_t index = 0; index < entity.native_hitboxes.shapes.size(); ++index) {
            const auto& shape = entity.native_hitboxes.shapes[index];
            if (shape.boneId != boneId || index >= entity.native_hitboxes.poses.size())
                continue;
            const Vector3 center = NativeShapeCenter(entity, shape, entity.native_hitboxes.poses[index]);
            const float distance = IsFiniteVector(reference) && IsFiniteVector(center)
                ? reference.DistTo(center) : (std::numeric_limits<float>::max)();
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nativeRadius = shape.radius;
            }
        }
        if (nativeRadius > 0.0f && std::isfinite(nativeRadius))
            boneRadius = nativeRadius;
    }
    const float projectileRadius = projectileRadiusOverride >= 0.0f
        ? (std::max)(0.0f, projectileRadiusOverride)
        : (weapon ? (std::max)(0.0f, weapon->projectile.projectileRadius) : 0.0f);
    return (std::max)(0.0f, boneRadius + projectileRadius) *
        Config::HitboxScaleMultiplier(hitboxScalePercent);
}

int AimBoneToSkeletonBoneId(int aimBone)
{
    switch (aimBone) {
    case Config::kAimBoneHead:
        return BONE_HEAD;
    case Config::kAimBoneNeck:
        return BONE_NECK;
    case Config::kAimBoneChest:
    case Config::kAimBoneClosest:
    default:
        return BONE_CHEST;
    }
}

bool IsCoreHitboxBoneId(int boneId)
{
    return boneId == BONE_HEAD || boneId == BONE_NECK || boneId == BONE_CHEST;
}

Vector3 ResolveBoneHitboxOffsetLocal(uint64_t heroId, int boneId)
{
    const BoneHitboxSpec* spec = ResolveBoneHitboxSpec(heroId, boneId);
    return spec ? spec->centerOffsetLocal : Vector3{};
}

Vector3 ResolveBoneHitboxCenter(const c_entity& entity, int boneId, const Vector3& bonePoint)
{
    const Vector3 offset = ResolveBoneHitboxOffsetLocal(entity.HeroID, boneId);
    if (IsZeroOffset(offset) || !IsFiniteVector(bonePoint))
        return bonePoint;

    XMFLOAT3 localOffset(offset.X, offset.Y, offset.Z);
    XMFLOAT3 rotatedOffset{};
    const XMMATRIX rotMatrix = XMMatrixRotationY(entity.Rot.X);
    XMStoreFloat3(
        &rotatedOffset,
        XMVector3Transform(XMLoadFloat3(&localOffset), rotMatrix));
    return bonePoint + Vector3(rotatedOffset.x, rotatedOffset.y, rotatedOffset.z);
}

bool TryResolveNativeHitboxCenter(const c_entity& entity, int boneId, Vector3& outPoint)
{
    outPoint = {};
    if (!NativeHitboxSampleUsableInternal(entity, GetTickCount()))
        return false;
    const Vector3 reference = NativeBoneReference(entity, boneId);
    float nearestDistance = (std::numeric_limits<float>::max)();
    for (size_t index = 0; index < entity.native_hitboxes.shapes.size(); ++index) {
        const auto& shape = entity.native_hitboxes.shapes[index];
        if (shape.boneId != boneId || index >= entity.native_hitboxes.poses.size())
            continue;
        const auto& pose = entity.native_hitboxes.poses[index];
        const Vector3 center = NativeShapeCenter(entity, shape, pose);
        const float distance = IsFiniteVector(reference) && IsFiniteVector(center)
            ? reference.DistTo(center) : (std::numeric_limits<float>::max)();
        if (distance < nearestDistance) {
            nearestDistance = distance;
            outPoint = center;
        }
    }
    return nearestDistance != (std::numeric_limits<float>::max)() &&
        IsFiniteVector(outPoint) && !IsZeroOffset(outPoint);
}

std::optional<float> NativeHitWindowScore(const c_entity& entity, int boneId, uint32_t now,
    const Vector3& origin, const Vector3& direction, const Vector3& predictedDisplacement,
    float projectileRadius, float hitboxScalePercent)
{
    if (!NativeHitboxSampleUsableInternal(entity, now) || !IsFiniteVector(origin) ||
        !IsFiniteVector(direction) || !IsFiniteVector(predictedDisplacement) ||
        !std::isfinite(projectileRadius) || !std::isfinite(hitboxScalePercent))
        return std::nullopt;
    const float rayLength = direction.Size();
    if (!std::isfinite(rayLength))
        return std::nullopt;

    const auto& sample = entity.native_hitboxes;
    const Vector3 unitRay = rayLength > 0.0001f ? direction / rayLength : Vector3{};
    const Vector3 relativeAnchor = entity.pos - sample.world.sampleAnchor + predictedDisplacement - origin;
    const float worldScale = (std::min)({sample.world.record.scale[0], sample.world.record.scale[1],
        sample.world.record.scale[2]});
    const float hitboxScale = Config::HitboxScaleMultiplier(hitboxScalePercent);
    std::optional<float> score;
    for (size_t i = 0; i < sample.shapes.size(); ++i) {
        const auto& shape = sample.shapes[i];
        const auto& pose = sample.poses[i];
        if (shape.boneId != boneId || !std::isfinite(shape.radius) || shape.radius <= 0.0f || !pose.Valid())
            continue;
        const Vector3 a = sample.world.TransformPoint(ModelShapes::Transform(shape.a, pose)) + relativeAnchor;
        const Vector3 b = sample.world.TransformPoint(ModelShapes::Transform(shape.b, pose)) + relativeAnchor;
        // Minimum axis scale gives an inscribed radius for non-uniform transforms.
        const float poseScale = (std::min)({pose.scale[0], pose.scale[1], pose.scale[2]});
        const float radius = (shape.radius * poseScale * worldScale + (std::max)(0.0f, projectileRadius)) * hitboxScale;
        if (!IsFiniteVector(a) || !IsFiniteVector(b) || !std::isfinite(radius) || radius <= 0.0f)
            continue;
        const float normalized = std::sqrt(RayAxisDistanceSquared(a, b, unitRay)) / radius;
        if (std::isfinite(normalized) && (!score || normalized < *score))
            score = normalized;
    }
    return score;
}

std::optional<float> NativeHitWindowScore(const c_entity& entity, int boneId, uint32_t now,
    const Vector3& origin, const Vector3& direction, const NativeHitWindowInput& input,
    const Vector3& extraDisplacement)
{
    if (input.projectileRadius < 0.0f || !IsFiniteVector(origin) || !IsFiniteVector(direction))
        return std::nullopt;
    const float directionLength = direction.Size();
    if (!std::isfinite(directionLength) || directionLength <= 0.0001f)
        return std::nullopt;

    if (input.projectileSpeed <= 0.0f) {
        Vector3 launchDirection = direction + Vector3(0.0f, input.launchUpBias, 0.0f);
        const float launchLength = launchDirection.Size();
        if (!std::isfinite(launchLength) || launchLength <= 0.0001f)
            return std::nullopt;
        launchDirection = launchDirection / launchLength;
        return NativeHitWindowScore(entity, boneId, now, origin, launchDirection,
            input.displacement + extraDisplacement, input.projectileRadius, input.hitboxScalePercent);
    }

    if (!std::isfinite(input.projectileSpeed) || input.projectileSpeed <= 0.0f ||
        !std::isfinite(input.gravityAcceleration) || !IsFiniteVector(input.targetVelocity))
        return std::nullopt;
    Vector3 launchDirection = direction + Vector3(0.0f, input.launchUpBias, 0.0f);
    const float launchLength = launchDirection.Size();
    if (!std::isfinite(launchLength) || launchLength <= 0.0001f)
        return std::nullopt;
    launchDirection = launchDirection / launchLength;
    const Vector3 initialVelocity = launchDirection * input.projectileSpeed;
    const float maxTime = input.maxFlightSeconds > 0.0f
        ? input.maxFlightSeconds
        : std::clamp(directionLength / input.projectileSpeed * 1.5f + 0.05f, 0.05f, 4.0f);
    const ProjectileTrajectory trajectory{
        origin,
        initialVelocity,
        input.targetVelocity,
        input.gravity ? Vector3(0.0f, -std::fabs(input.gravityAcceleration), 0.0f) : Vector3{},
        maxTime
    };

    if (!NativeHitboxSampleUsableInternal(entity, now))
        return std::nullopt;
    const auto& sample = entity.native_hitboxes;
    const Vector3 relativeAnchor = entity.pos - sample.world.sampleAnchor +
        input.displacement + extraDisplacement;
    const float worldScale = (std::min)({sample.world.record.scale[0], sample.world.record.scale[1],
        sample.world.record.scale[2]});
    const float hitboxScale = Config::HitboxScaleMultiplier(input.hitboxScalePercent);
    std::optional<float> bestHitTime;
    std::optional<float> bestHitScore;
    for (size_t i = 0; i < sample.shapes.size(); ++i) {
        const auto& shape = sample.shapes[i];
        const auto& pose = sample.poses[i];
        if (shape.boneId != boneId || !std::isfinite(shape.radius) || shape.radius <= 0.0f || !pose.Valid())
            continue;
        const Vector3 a = sample.world.TransformPoint(ModelShapes::Transform(shape.a, pose)) + relativeAnchor;
        const Vector3 b = sample.world.TransformPoint(ModelShapes::Transform(shape.b, pose)) + relativeAnchor;
        const float poseScale = (std::min)({pose.scale[0], pose.scale[1], pose.scale[2]});
        const float radius = (shape.radius * poseScale * worldScale + input.projectileRadius) * hitboxScale;
        if (!IsFiniteVector(a) || !IsFiniteVector(b) || !std::isfinite(radius) || radius <= 0.0f)
            continue;
        float hitTime = 0.0f;
        if (ContinuousTrajectoryIntersectsHitboxInternal(trajectory, {a, b, radius}, 0.0f, &hitTime)) {
            const Vector3 impact = TrajectoryPosition(trajectory, hitTime);
            const float score = std::sqrt(DistanceSquaredPointSegment(impact, a, b)) / radius;
            if (!bestHitTime || hitTime < *bestHitTime) {
                bestHitTime = hitTime;
                bestHitScore = score;
            }
        }
    }
    if (!bestHitTime || !bestHitScore)
        return std::nullopt;
    return std::isfinite(*bestHitScore) ? bestHitScore : std::nullopt;
}

} // namespace OW
