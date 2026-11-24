#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Utils/Types.hpp"

namespace OW::HitboxMotionAssist {

enum class MotionRegime : int {
    Baseline = 0,
    Repeated = 1,
    Stationary = 2,
    Steady = 3,
    Transient = 4,
};

struct Params {
    float motionGain = 0.35f;
    float suddenBoost = 2.5f;
    float maxSampleDtSeconds = 0.048f;
    float maxPlausibleSpeedMps = 100.0f;
    float stationarySpeedMps = 0.10f;
    float suddenEnterAccelMps2 = 8.0f;
    float suddenExitAccelMps2 = 4.0f;
};

struct Sample {
    uint64_t actorKey = 0;
    uint64_t actorGeneration = 0;
    uint64_t sampleTimeUs = 0;
    Vector3 bodyPosition{};
    Vector3 bodyVelocity{};
};

struct State {
    uint64_t actorKey = 0;
    uint64_t actorGeneration = 0;
    uint64_t sampleTimeUs = 0;
    Vector3 lastPosition{};
    Vector3 lastVelocity{};
    bool initialized = false;
    bool suddenActive = false;

    void Reset()
    {
        actorKey = 0;
        actorGeneration = 0;
        sampleTimeUs = 0;
        lastPosition = {};
        lastVelocity = {};
        initialized = false;
        suddenActive = false;
    }
};

struct Response {
    MotionRegime regime = MotionRegime::Baseline;
    Vector3 worldFollowDisplacement{};
    float assistScale = 0.0f;
    float accelerationMps2 = 0.0f;
    float speedMps = 0.0f;

    bool HasFollowDisplacement() const
    {
        return (regime == MotionRegime::Steady || regime == MotionRegime::Transient) &&
            std::isfinite(worldFollowDisplacement.X) &&
            std::isfinite(worldFollowDisplacement.Y) &&
            std::isfinite(worldFollowDisplacement.Z) &&
            worldFollowDisplacement.Size() > 1e-6f;
    }
};

inline bool IsFiniteVector(const Vector3& value)
{
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}

inline const char* RegimeName(MotionRegime regime)
{
    switch (regime) {
    case MotionRegime::Repeated:
        return "repeated";
    case MotionRegime::Stationary:
        return "stationary";
    case MotionRegime::Steady:
        return "steady";
    case MotionRegime::Transient:
        return "transient";
    case MotionRegime::Baseline:
    default:
        return "baseline";
    }
}

inline Response Advance(State& state, const Sample& sample, const Params& rawParams = {})
{
    Response out{};
    if (sample.actorKey == 0 || sample.sampleTimeUs == 0 ||
        !IsFiniteVector(sample.bodyPosition) || !IsFiniteVector(sample.bodyVelocity)) {
        state.Reset();
        return out;
    }

    const float motionGain = std::isfinite(rawParams.motionGain)
        ? std::clamp(rawParams.motionGain, 0.0f, 10.0f)
        : 0.35f;
    const float suddenBoost = std::isfinite(rawParams.suddenBoost)
        ? std::clamp(rawParams.suddenBoost, 0.0f, 10.0f)
        : 2.5f;
    const float maxDt = std::isfinite(rawParams.maxSampleDtSeconds) && rawParams.maxSampleDtSeconds > 0.0f
        ? rawParams.maxSampleDtSeconds
        : 0.048f;
    const float maxSpeed = std::isfinite(rawParams.maxPlausibleSpeedMps) && rawParams.maxPlausibleSpeedMps > 0.0f
        ? rawParams.maxPlausibleSpeedMps
        : 100.0f;
    const float stationarySpeed = std::isfinite(rawParams.stationarySpeedMps) && rawParams.stationarySpeedMps >= 0.0f
        ? rawParams.stationarySpeedMps
        : 0.10f;
    const float suddenEnter = std::isfinite(rawParams.suddenEnterAccelMps2) && rawParams.suddenEnterAccelMps2 > 0.0f
        ? rawParams.suddenEnterAccelMps2
        : 8.0f;
    const float suddenExit = std::isfinite(rawParams.suddenExitAccelMps2) && rawParams.suddenExitAccelMps2 > 0.0f
        ? (std::min)(rawParams.suddenExitAccelMps2, suddenEnter)
        : (std::min)(4.0f, suddenEnter);

    const bool sameBody = state.initialized &&
        state.actorKey == sample.actorKey &&
        state.actorGeneration == sample.actorGeneration;
    if (sameBody && sample.sampleTimeUs <= state.sampleTimeUs) {
        out.regime = MotionRegime::Repeated;
        out.speedMps = sample.bodyVelocity.Size();
        return out;
    }

    const float dtSeconds = sameBody
        ? static_cast<float>(sample.sampleTimeUs - state.sampleTimeUs) / 1000000.0f
        : 0.0f;
    const bool coherent = sameBody && dtSeconds > 0.0f && dtSeconds <= maxDt &&
        IsFiniteVector(state.lastPosition) && IsFiniteVector(state.lastVelocity);
    const Vector3 delta = coherent ? (sample.bodyPosition - state.lastPosition) : Vector3{};
    const float deltaLength = coherent ? delta.Size() : 0.0f;
    const float velocitySpeed = sample.bodyVelocity.Size();
    const float acceleration = coherent ? ((sample.bodyVelocity - state.lastVelocity).Size() / dtSeconds) : 0.0f;
    const bool plausible = coherent &&
        std::isfinite(deltaLength) &&
        std::isfinite(acceleration) &&
        (deltaLength / dtSeconds) <= maxSpeed;
    const float suddenThreshold = state.suddenActive ? suddenExit : suddenEnter;
    const bool sudden = plausible && acceleration >= suddenThreshold;

    state.actorKey = sample.actorKey;
    state.actorGeneration = sample.actorGeneration;
    state.sampleTimeUs = sample.sampleTimeUs;
    state.lastPosition = sample.bodyPosition;
    state.lastVelocity = sample.bodyVelocity;
    state.initialized = true;
    state.suddenActive = sudden;

    out.speedMps = std::isfinite(velocitySpeed) ? velocitySpeed : 0.0f;
    out.accelerationMps2 = plausible ? acceleration : 0.0f;

    if (!plausible) {
        out.regime = MotionRegime::Baseline;
        return out;
    }

    if (sudden) {
        const float combinedGain = std::clamp(motionGain * suddenBoost, 0.0f, 10.0f);
        out.regime = MotionRegime::Transient;
        out.worldFollowDisplacement = delta * combinedGain;
        out.assistScale = std::clamp(combinedGain, 0.0f, 1.0f);
        return out;
    }

    if (velocitySpeed <= stationarySpeed && deltaLength <= stationarySpeed * dtSeconds) {
        out.regime = MotionRegime::Stationary;
        out.assistScale = 0.0f;
        return out;
    }

    out.regime = MotionRegime::Steady;
    out.worldFollowDisplacement = delta * motionGain;
    out.assistScale = std::clamp(motionGain, 0.0f, 1.0f);
    return out;
}

inline Vector3 ComputeInHitboxTargetAngle(const Vector3& localAngle,
                                          float contactDistance,
                                          const Response& response)
{
    if (!IsFiniteVector(localAngle) || !std::isfinite(contactDistance) ||
        contactDistance <= 0.0001f || !response.HasFollowDisplacement()) {
        return localAngle;
    }

    const float horizontal = std::cos(localAngle.X);
    const Vector3 forward(
        horizontal * std::sin(localAngle.Y),
        -std::sin(localAngle.X),
        horizontal * std::cos(localAngle.Y));
    const Vector3 shifted = forward * contactDistance + response.worldFollowDisplacement;
    const float shiftedLength = shifted.Size();
    if (!std::isfinite(shiftedLength) || shiftedLength <= 0.0001f)
        return localAngle;

    const Vector3 unitShifted = shifted / shiftedLength;
    const float clampedY = std::clamp(unitShifted.Y, -1.0f, 1.0f);
    const Vector3 result(
        -std::asin(clampedY),
        std::atan2(unitShifted.X, unitShifted.Z),
        0.0f);
    return IsFiniteVector(result) ? result : localAngle;
}

} // namespace OW::HitboxMotionAssist
