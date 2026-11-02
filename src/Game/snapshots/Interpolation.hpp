#pragma once
#include "Game/Entity.hpp"
#include "Game/PresentationTiming.hpp"
#include <algorithm>
#include <cmath>
namespace OverlayRenderDetail {
    using OW::Vector3;
    namespace PresentationTiming = OW::PresentationTiming;
    inline float Clamp01(float value) {
        return std::clamp(value, 0.0f, 1.0f);
    }

    inline float Lerp(float from, float to, float t) {
        return from + (to - from) * Clamp01(t);
    }

    inline bool IsFiniteVector(const Vector3& value) {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

    inline bool IsNonZeroVector(const Vector3& value) {
        return value != Vector3(0.0f, 0.0f, 0.0f);
    }

    inline Vector3 LerpVector(const Vector3& from, const Vector3& to, float t) {
        const float alpha = Clamp01(t);
        return Vector3(
            Lerp(from.X, to.X, alpha),
            Lerp(from.Y, to.Y, alpha),
            Lerp(from.Z, to.Z, alpha));
    }

    inline Vector3 LerpWorldPosition(const Vector3& from, const Vector3& to, float t) {
        if (!IsFiniteVector(from) || !IsFiniteVector(to) ||
            !IsNonZeroVector(from) || !IsNonZeroVector(to)) {
            return to;
        }
        return LerpVector(from, to, t);
    }

    inline Vector3 LerpFiniteVector(const Vector3& from, const Vector3& to, float t) {
        if (!IsFiniteVector(from) || !IsFiniteVector(to))
            return to;
        return LerpVector(from, to, t);
    }

    inline Vector3 SelectInterpolationAnchor(const OW::c_entity& entity, bool previous) {
        const Vector3 position = previous ? entity.previous_pos : entity.pos;
        if (IsFiniteVector(position) && IsNonZeroVector(position))
            return position;

        const Vector3 head = previous ? entity.previous_head_pos : entity.head_pos;
        return head;
    }

    inline bool CanInterpolateEntity(const OW::c_entity& entity) {
        if (!entity.has_previous_render_sample)
            return false;
        if (!IsFiniteVector(entity.head_pos) || !IsFiniteVector(entity.previous_head_pos))
            return false;

        const Vector3 currentAnchor = SelectInterpolationAnchor(entity, false);
        const Vector3 previousAnchor = SelectInterpolationAnchor(entity, true);
        if (!IsFiniteVector(currentAnchor) || !IsFiniteVector(previousAnchor) ||
            !IsNonZeroVector(currentAnchor) || !IsNonZeroVector(previousAnchor)) {
            return false;
        }

        const float movement = previousAnchor.DistTo(currentAnchor);
        return std::isfinite(movement) && movement <= 8.0f;
    }

    inline float EntityInterpolationAlpha(const OW::c_entity& entity, DWORD now) {
        if (!CanInterpolateEntity(entity))
            return 1.0f;

        return PresentationTiming::Evaluate(entity.previous_render_sample_tick_ms,
            entity.render_sample_tick_ms, now, entity.has_previous_render_sample).alpha;
    }

    inline bool IsReasonableRenderVelocity(const Vector3& velocity, float* speedOut = nullptr) {
        if (!IsFiniteVector(velocity)) {
            if (speedOut) *speedOut = 0.0f;
            return false;
        }

        const float speed = velocity.Size();
        if (speedOut) *speedOut = speed;
        return std::isfinite(speed) && speed >= 0.75f && speed <= 80.0f;
    }

    inline void OffsetRenderPoint(Vector3& point, const Vector3& offset) {
        if (IsFiniteVector(point) && IsNonZeroVector(point))
            point += offset;
    }

    inline OW::c_entity InterpolateEntitySamples(const OW::c_entity& source, float alpha) {
        OW::c_entity entity = source;
        entity.head_pos = LerpWorldPosition(source.previous_head_pos, source.head_pos, alpha);
        entity.velocity = LerpFiniteVector(source.previous_velocity, source.velocity, alpha);
        entity.pos = LerpWorldPosition(source.previous_pos, source.pos, alpha);
        entity.neck_pos = LerpWorldPosition(source.previous_neck_pos, source.neck_pos, alpha);
        entity.chest_pos = LerpWorldPosition(source.previous_chest_pos, source.chest_pos, alpha);

        for (size_t i = 0; i < entity.skeleton_bones.size(); ++i) {
            if (source.previous_skeleton_bone_valid[i] && source.skeleton_bone_valid[i]) {
                entity.skeleton_bones[i] =
                    LerpWorldPosition(source.previous_skeleton_bones[i], source.skeleton_bones[i], alpha);
                entity.skeleton_bone_valid[i] = true;
            }
        }

        if (source.previous_cached_bot_chest_bone_valid && source.cached_bot_chest_bone_valid) {
            entity.cached_bot_chest_bone =
                LerpWorldPosition(source.previous_cached_bot_chest_bone, source.cached_bot_chest_bone, alpha);
            entity.cached_bot_chest_bone_valid = true;
        }

        return entity;
    }


}
