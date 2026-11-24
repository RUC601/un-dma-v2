#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Utils/Types.hpp"

namespace OW::Motion {

enum class VelocitySource : int {
    None,
    Reported,
    WorldDeltaFallback,
    PreviousStableFallback,
};

struct EntityMotionEstimate {
    Vector3 reportedVelocity{};
    Vector3 worldDeltaVelocity{};
    Vector3 previousStableVelocity{};
    Vector3 effectiveVelocity{};
    Vector3 reportedAcceleration{};
    float sampleSeconds = 0.0f;
    float fallbackGapSeconds = 0.0f;
    float reportedSpeed = 0.0f;
    float worldDeltaSpeed = 0.0f;
    float previousStableSpeed = 0.0f;
    float reportedJumpDelta = 0.0f;
    float reportedJumpAllowed = 0.0f;
    float effectiveSpeed = 0.0f;
    float confidence = 0.0f;
    VelocitySource velocitySource = VelocitySource::None;
    bool hasReportedVelocity = false;
    bool hasWorldDeltaVelocity = false;
    bool hasPreviousStableVelocity = false;
    bool usedWorldDeltaFallback = false;
    bool usedReportedFallback = false;
    bool usedPreviousStableFallback = false;
    bool rejectedReportedJump = false;
    bool acceptedForStableHistory = false;
    bool valid = false;
};

inline constexpr float kMinSampleSeconds = 0.004f;
inline constexpr float kMaxSampleSeconds = 0.010f;
inline constexpr float kMaxReasonableSpeed = 250.0f;
inline constexpr float kMinReportedSpeed = 1.0f;
inline constexpr float kReportedJumpNoiseMargin = 2.5f;
inline constexpr float kReportedJumpMaxAcceleration = 60.0f;
inline constexpr float kReportedJumpMaxWindowSeconds = 0.050f;
inline constexpr float kReportedJumpHardCap = 6.0f;
inline constexpr float kMaxPreviousStableAgeSeconds = 0.250f;

inline bool IsFiniteVector(const Vector3& value)
{
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}

inline bool IsNonZeroVector(const Vector3& value)
{
    return value != Vector3(0, 0, 0);
}

inline float VectorLength(const Vector3& value)
{
    return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
}

inline bool IsReasonableVelocity(const Vector3& velocity, float* speedOut = nullptr)
{
    if (!IsFiniteVector(velocity)) {
        if (speedOut) *speedOut = 0.0f;
        return false;
    }

    const float speed = VectorLength(velocity);
    if (speedOut) *speedOut = speed;
    return std::isfinite(speed) && speed <= kMaxReasonableSpeed;
}

template <typename EntityLike>
inline float RawSampleGapSeconds(const EntityLike& entity, bool* hasClockOut = nullptr)
{
    if constexpr (requires { entity.sample_time_us; entity.previous_sample_time_us; }) {
        if (entity.previous_sample_time_us != 0 && entity.sample_time_us != 0) {
            if (entity.sample_time_us < entity.previous_sample_time_us) {
                if (hasClockOut) *hasClockOut = false;
                return -1.0f;
            }
            if (hasClockOut) *hasClockOut = true;
            return static_cast<float>(entity.sample_time_us - entity.previous_sample_time_us) / 1000000.0f;
        }
    }

    if (entity.has_previous_render_sample &&
        entity.previous_render_sample_tick_ms != 0 &&
        entity.render_sample_tick_ms != 0) {
        if (entity.render_sample_tick_ms < entity.previous_render_sample_tick_ms) {
            if (hasClockOut) *hasClockOut = false;
            return -1.0f;
        }
        if (hasClockOut) *hasClockOut = true;
        return static_cast<float>(
            entity.render_sample_tick_ms - entity.previous_render_sample_tick_ms) / 1000.0f;
    }

    if (hasClockOut) *hasClockOut = false;
    return 0.0f;
}

template <typename EntityLike>
inline float SampleSeconds(const EntityLike& entity)
{
    if constexpr (requires { entity.sample_time_us; entity.previous_sample_time_us; }) {
        if (entity.previous_sample_time_us && entity.sample_time_us > entity.previous_sample_time_us) {
            const float dt = static_cast<float>(entity.sample_time_us - entity.previous_sample_time_us) / 1000000.0f;
            return dt >= kMinSampleSeconds && dt <= kMaxSampleSeconds ? dt : 0.0f;
        }
    }
    if (!entity.has_previous_render_sample ||
        entity.previous_render_sample_tick_ms == 0 ||
        entity.render_sample_tick_ms == 0 ||
        entity.render_sample_tick_ms <= entity.previous_render_sample_tick_ms) {
        return 0.0f;
    }

    const float dt = static_cast<float>(
        entity.render_sample_tick_ms - entity.previous_render_sample_tick_ms) / 1000.0f;
    if (dt < kMinSampleSeconds || dt > kMaxSampleSeconds)
        return 0.0f;

    return dt;
}

inline float AllowedReportedJumpDelta(float gapSeconds)
{
    const float clampedGap = std::isfinite(gapSeconds)
        ? std::clamp(gapSeconds, 0.0f, kReportedJumpMaxWindowSeconds)
        : 0.0f;
    const float scaled = kReportedJumpNoiseMargin + kReportedJumpMaxAcceleration * clampedGap;
    return (std::min)(kReportedJumpHardCap, scaled);
}

template <typename EntityLike>
inline bool ResolvePreviousStableVelocity(const EntityLike& entity,
                                          Vector3& velocityOut,
                                          float& speedOut,
                                          float& gapSecondsOut)
{
    velocityOut = {};
    speedOut = 0.0f;
    gapSecondsOut = 0.0f;

    if constexpr (requires { entity.has_previous_stable_velocity; entity.previous_stable_velocity; }) {
        if (!entity.has_previous_stable_velocity)
            return false;
        if (!IsReasonableVelocity(entity.previous_stable_velocity, &speedOut))
            return false;

        bool hasClock = false;
        float ageSeconds = 0.0f;
        if constexpr (requires { entity.sample_time_us; entity.previous_stable_velocity_time_us; }) {
            if (entity.previous_stable_velocity_time_us != 0 && entity.sample_time_us != 0) {
                if (entity.sample_time_us < entity.previous_stable_velocity_time_us)
                    return false;
                ageSeconds = static_cast<float>(
                    entity.sample_time_us - entity.previous_stable_velocity_time_us) / 1000000.0f;
                hasClock = true;
            }
        }
        if (!hasClock) {
            if constexpr (requires { entity.render_sample_tick_ms; entity.previous_stable_velocity_tick_ms; }) {
                if (entity.previous_stable_velocity_tick_ms != 0 && entity.render_sample_tick_ms != 0) {
                    if (entity.render_sample_tick_ms < entity.previous_stable_velocity_tick_ms)
                        return false;
                    ageSeconds = static_cast<float>(
                        entity.render_sample_tick_ms - entity.previous_stable_velocity_tick_ms) / 1000.0f;
                    hasClock = true;
                }
            }
        }
        if (!hasClock) {
            ageSeconds = RawSampleGapSeconds(entity, &hasClock);
            if (ageSeconds < 0.0f)
                return false;
        }
        if (hasClock && (!std::isfinite(ageSeconds) || ageSeconds > kMaxPreviousStableAgeSeconds))
            return false;

        velocityOut = entity.previous_stable_velocity;
        gapSecondsOut = hasClock ? (std::max)(0.0f, ageSeconds) : 0.0f;
        return true;
    } else {
        bool hasPreviousHistory = entity.has_previous_render_sample;
        if constexpr (requires { entity.sample_time_us; entity.previous_sample_time_us; }) {
            if (entity.previous_sample_time_us != 0 && entity.sample_time_us >= entity.previous_sample_time_us)
                hasPreviousHistory = true;
        }
        if (!hasPreviousHistory)
            return false;
        if (!IsReasonableVelocity(entity.previous_velocity, &speedOut))
            return false;

        bool hasClock = false;
        const float ageSeconds = RawSampleGapSeconds(entity, &hasClock);
        if (ageSeconds < 0.0f)
            return false;
        if (hasClock && (!std::isfinite(ageSeconds) || ageSeconds > kMaxPreviousStableAgeSeconds))
            return false;

        velocityOut = entity.previous_velocity;
        gapSecondsOut = hasClock ? (std::max)(0.0f, ageSeconds) : 0.0f;
        return true;
    }
}

template <typename EntityLike>
inline EntityMotionEstimate EstimateEntityMotionCore(const EntityLike& entity)
{
    EntityMotionEstimate estimate{};
    estimate.reportedVelocity = entity.velocity;
    estimate.sampleSeconds = SampleSeconds(entity);
    estimate.hasReportedVelocity =
        IsReasonableVelocity(estimate.reportedVelocity, &estimate.reportedSpeed);
    estimate.hasPreviousStableVelocity = ResolvePreviousStableVelocity(
        entity,
        estimate.previousStableVelocity,
        estimate.previousStableSpeed,
        estimate.fallbackGapSeconds);

    if (estimate.sampleSeconds > 0.0f &&
        IsFiniteVector(entity.pos) &&
        IsFiniteVector(entity.previous_pos) &&
        IsNonZeroVector(entity.pos) &&
        IsNonZeroVector(entity.previous_pos)) {
        estimate.worldDeltaVelocity = (entity.pos - entity.previous_pos) / estimate.sampleSeconds;
        estimate.hasWorldDeltaVelocity =
            IsReasonableVelocity(estimate.worldDeltaVelocity, &estimate.worldDeltaSpeed);

        float previousReportedSpeed = 0.0f;
        if (estimate.hasReportedVelocity &&
            IsReasonableVelocity(entity.previous_velocity, &previousReportedSpeed)) {
            estimate.reportedAcceleration =
                (estimate.reportedVelocity - entity.previous_velocity) / estimate.sampleSeconds;
        }
    }

    // Tier 1: World position delta within the 4-10 ms gate is the primary velocity source.
    if (estimate.hasWorldDeltaVelocity) {
        estimate.effectiveVelocity = estimate.worldDeltaVelocity;
        estimate.effectiveSpeed = estimate.worldDeltaSpeed;
        estimate.velocitySource = VelocitySource::WorldDeltaFallback;
        estimate.usedWorldDeltaFallback = true;
        estimate.acceptedForStableHistory = true;
        estimate.confidence = 0.75f;
        estimate.valid = true;
        return estimate;
    }

    // Tier 2: When delta is unavailable or gated out, screen reported velocity
    // against the previous stable velocity using 3D vector difference.
    if (estimate.hasReportedVelocity) {
        if (estimate.hasPreviousStableVelocity) {
            estimate.reportedJumpDelta =
                VectorLength(estimate.reportedVelocity - estimate.previousStableVelocity);
            estimate.reportedJumpAllowed = AllowedReportedJumpDelta(estimate.fallbackGapSeconds);

            if (!std::isfinite(estimate.reportedJumpDelta) ||
                estimate.reportedJumpDelta > estimate.reportedJumpAllowed) {
                estimate.rejectedReportedJump = true;
                estimate.effectiveVelocity = estimate.previousStableVelocity;
                estimate.effectiveSpeed = estimate.previousStableSpeed;
                estimate.velocitySource = VelocitySource::PreviousStableFallback;
                estimate.usedPreviousStableFallback = true;
                estimate.confidence = 0.45f;
                estimate.valid = true;
                return estimate;
            }
        }

        estimate.effectiveVelocity = estimate.reportedVelocity;
        estimate.effectiveSpeed = estimate.reportedSpeed;
        estimate.velocitySource = VelocitySource::Reported;
        estimate.usedReportedFallback = true;
        estimate.acceptedForStableHistory = true;
        estimate.confidence = estimate.hasPreviousStableVelocity ? 0.60f : 0.55f;
        estimate.valid = true;
        return estimate;
    }

    // Tier 3: Reported velocity itself is invalid; reuse previous stable velocity if fresh.
    if (estimate.hasPreviousStableVelocity) {
        estimate.effectiveVelocity = estimate.previousStableVelocity;
        estimate.effectiveSpeed = estimate.previousStableSpeed;
        estimate.velocitySource = VelocitySource::PreviousStableFallback;
        estimate.usedPreviousStableFallback = true;
        estimate.confidence = 0.45f;
        estimate.valid = true;
        return estimate;
    }

    estimate.confidence = 0.10f;
    return estimate;
}

template <typename EntityLike>
inline EntityMotionEstimate EstimateEntityMotion(const EntityLike& entity)
{
    return EstimateEntityMotionCore(entity);
}

template <typename EntityLike>
inline EntityMotionEstimate UpdateEntityStableVelocity(EntityLike& entity)
{
    const EntityMotionEstimate estimate = EstimateEntityMotionCore(entity);
    if constexpr (requires {
            entity.has_stable_velocity;
            entity.stable_velocity;
            entity.stable_velocity_time_us;
            entity.stable_velocity_tick_ms;
        }) {
        if (estimate.valid && estimate.acceptedForStableHistory) {
            entity.stable_velocity = estimate.effectiveVelocity;
            if constexpr (requires { entity.sample_time_us; })
                entity.stable_velocity_time_us = entity.sample_time_us;
            else
                entity.stable_velocity_time_us = 0;
            if constexpr (requires { entity.render_sample_tick_ms; })
                entity.stable_velocity_tick_ms = entity.render_sample_tick_ms;
            else
                entity.stable_velocity_tick_ms = 0;
            entity.has_stable_velocity = true;
        } else if (estimate.valid && estimate.usedPreviousStableFallback) {
            if constexpr (requires {
                    entity.has_previous_stable_velocity;
                    entity.previous_stable_velocity;
                    entity.previous_stable_velocity_time_us;
                    entity.previous_stable_velocity_tick_ms;
                }) {
                entity.stable_velocity = entity.previous_stable_velocity;
                entity.stable_velocity_time_us = entity.previous_stable_velocity_time_us;
                entity.stable_velocity_tick_ms = entity.previous_stable_velocity_tick_ms;
                entity.has_stable_velocity = entity.has_previous_stable_velocity;
            }
        } else {
            entity.stable_velocity = {};
            entity.stable_velocity_time_us = 0;
            entity.stable_velocity_tick_ms = 0;
            entity.has_stable_velocity = false;
        }
    }
    return estimate;
}

} // namespace OW::Motion
