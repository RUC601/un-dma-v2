#include "SmoothingInternal.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"
#include "Game/AimSmoothingTiming.hpp"
#include <algorithm>
#include <cmath>
namespace OW {
    Vector3 SmoothLinear(Vector3 LocalAngle, Vector3 TargetAngle, float speed) {
        const float factor = std::isfinite(speed) ? std::clamp(speed, 0.0f, 1.0f) : 0.0f;
        thread_local float lastx = 0;
        thread_local float lasty = 0;
        thread_local float lastz = 0;

        Vector3 Result;
        float deltax = (TargetAngle.X - LocalAngle.X) * factor;
        float deltay = (TargetAngle.Y - LocalAngle.Y) * factor;
        float deltaz = (TargetAngle.Z - LocalAngle.Z) * factor;

        Result.X = LocalAngle.X + deltax;
        Result.Y = LocalAngle.Y + deltay;
        Result.Z = LocalAngle.Z + deltaz;

        if (Config::trackcompensate) {
            Vector3 realresult{
                LocalAngle.X + TargetAngle.X - lastx,
                LocalAngle.Y + TargetAngle.Y - lasty,
                LocalAngle.Z + TargetAngle.Z - lastz
            };
            if (Result.DistTo(realresult) <= Config::comarea) {
                if (fabsf(deltax) < fabsf(TargetAngle.X - lastx))
                    deltax = (TargetAngle.X - lastx) * Config::comspeed;
                if (fabsf(deltaz) < fabsf(TargetAngle.Z - lastz))
                    deltaz = (TargetAngle.Z - lastz) * Config::comspeed;
            }
            lastx = TargetAngle.X;
            lasty = TargetAngle.Y;
            lastz = TargetAngle.Z;

            Result.X = LocalAngle.X + deltax;
            Result.Y = LocalAngle.Y + deltay;
            Result.Z = LocalAngle.Z + deltaz;
        }
        return Result;
    }

    Vector3 SmoothAccelerate(Vector3 LocalAngle, Vector3 TargetAngle, float Speed, float Acc) {
        Vector3 Result = LocalAngle;
        __try {
            Vector3 delta = TargetAngle - LocalAngle;
            float tmp = Acc / delta.get_length();
            tmp = tmp * tmp * 0.005f;
            float c = Speed + tmp * Speed;
            if (c >= 1.f) c = 1.f;
            Result.X += delta.X * c;
            Result.Y += delta.Y * c;
            Result.Z += delta.Z * c;
            return Result;
        } __except (1) {
            return Result;
        }
    }

    namespace AimSmoothingDetail {
        AimHybrid::State& GetHybridState() {
            thread_local AimHybrid::State state;
            return state;
        }

        PIDState& GetPIDState() {
            thread_local PIDState state;
            return state;
        }

        BezierState& GetBezierState() {
            thread_local BezierState state;
            return state;
        }

        OvershootState& GetOvershootState() {
            thread_local OvershootState state;
            return state;
        }

        void ResetPIDState() {
            GetPIDState() = PIDState{};
        }

        void ResetBezierState() {
            GetBezierState() = BezierState{};
        }

        void ResetOvershootState() {
            GetOvershootState() = OvershootState{};
        }

        void ResetHybridState() {
            GetHybridState() = AimHybrid::State{};
        }

        float ClampDeltaTime(float deltaTime) {
            if (!std::isfinite(deltaTime) || deltaTime <= 0.0f)
                return kDefaultDeltaTime;
            return std::clamp(deltaTime, kMinDeltaTime, kMaxDeltaTime);
        }

        std::chrono::steady_clock::time_point& LastSmoothingTick() {
            static thread_local std::chrono::steady_clock::time_point lastTick{};
            return lastTick;
        }

        void ResetDeltaTime() {
            LastSmoothingTick() = {};
        }

        float ComputeDeltaTime() {
            const auto currentTick = std::chrono::steady_clock::now();
            auto& lastTick = LastSmoothingTick();
            if (lastTick == std::chrono::steady_clock::time_point{}) {
                lastTick = currentTick;
                return kDefaultDeltaTime;
            }

            const float deltaTime =
                std::chrono::duration<float>(currentTick - lastTick).count();
            lastTick = currentTick;
            return ClampDeltaTime(deltaTime);
        }

        bool IsRetarget(const Vector3& target, const Vector3& lastTarget) {
            return target.DistTo(lastTarget) > kRetargetAngleThreshold;
        }

        float EstimateMovePixels(const Vector3& angleDelta) {
            const float sensitivity = Config::OutputYawCountsPerRadian();
            const float pitchSensitivity = Config::OutputPitchCountsPerRadian();
            const float pitchScale = std::clamp(Config::View_aimbotPitchScale(), 0.1f, 3.0f);
            const float pixelX = -angleDelta.Y * sensitivity;
            const float pixelY = angleDelta.X * pitchSensitivity * pitchScale;
            return sqrtf(pixelX * pixelX + pixelY * pixelY);
        }

        Vector3 ApplyOvershootCurveTarget(const Vector3& local, const Vector3& target) {
            if (!Config::aimOvershootCurve)
                return target;

            OvershootState& state = GetOvershootState();
            if (!state.initialized || IsRetarget(target, state.lastTarget)) {
                state = OvershootState{};
                state.initialized = true;
            }

            state.lastTarget = target;
            const float resetPixels = std::clamp(Config::aimOvershootResetPixels, 1.0f, 250.0f);
            if (EstimateMovePixels(state.offset) > resetPixels)
                state.offset = Vector3{};

            const Vector3 adjusted = target + state.offset;
            Diagnostics::Aim("smooth.overshoot apply offset=(%.9f,%.9f,%.9f) offset_pixels=%.3f local=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f) adjusted=(%.9f,%.9f,%.9f)",
                state.offset.X,
                state.offset.Y,
                state.offset.Z,
                EstimateMovePixels(state.offset),
                local.X,
                local.Y,
                local.Z,
                target.X,
                target.Y,
                target.Z,
                adjusted.X,
                adjusted.Y,
                adjusted.Z);
            return adjusted;
        }

        void CommitOvershootStep(const Vector3& outputDelta) {
            if (!Config::aimOvershootCurve) {
                ResetOvershootState();
                return;
            }

            OvershootState& state = GetOvershootState();
            const float gain = std::clamp(Config::aimOvershootGain, 0.0f, 1.0f);
            const float resetPixels = std::clamp(Config::aimOvershootResetPixels, 1.0f, 250.0f);
            state.offset = (state.offset + outputDelta * gain) * 0.82f;
            if (EstimateMovePixels(state.offset) > resetPixels)
                state.offset = Vector3{};

            Diagnostics::Aim("smooth.overshoot commit output_delta=(%.9f,%.9f,%.9f) gain=%.3f offset=(%.9f,%.9f,%.9f) offset_pixels=%.3f resetPixels=%.3f",
                outputDelta.X,
                outputDelta.Y,
                outputDelta.Z,
                gain,
                state.offset.X,
                state.offset.Y,
                state.offset.Z,
                EstimateMovePixels(state.offset),
                resetPixels);
        }

        float ClampIntegralComponent(float value, float maxIntegral) {
            return std::clamp(value, -maxIntegral, maxIntegral);
        }

        Vector3 ClampIntegral(const Vector3& value, float maxIntegral) {
            return Vector3(
                ClampIntegralComponent(value.X, maxIntegral),
                ClampIntegralComponent(value.Y, maxIntegral),
                ClampIntegralComponent(value.Z, maxIntegral)
            );
        }

        float ClampStepComponent(float output, float error) {
            const float maxStep = std::fabs(error);
            if (maxStep <= 0.0f)
                return 0.0f;
            return std::clamp(output, -maxStep, maxStep);
        }

        Vector3 ClampStepToError(const Vector3& output, const Vector3& error) {
            return Vector3(
                ClampStepComponent(output.X, error.X),
                ClampStepComponent(output.Y, error.Y),
                ClampStepComponent(output.Z, error.Z)
            );
        }

        Vector3 Lerp(const Vector3& a, const Vector3& b, float t) {
            return a * (1.0f - t) + b * t;
        }

        Vector3 PerpendicularTo(const Vector3& value) {
            Vector3 perpendicular(-value.Y, value.X, 0.0f);
            if (perpendicular.Size() <= 0.0001f)
                perpendicular = Vector3(0.0f, -value.Z, value.Y);

            const float length = perpendicular.get_length();
            return perpendicular / length;
        }

        Vector3 EvaluateBezier(const std::vector<Vector3>& controlPoints, float t) {
            if (controlPoints.empty())
                return Vector3{};

            std::vector<Vector3> points = controlPoints;
            t = std::clamp(t, 0.0f, 1.0f);

            for (size_t count = points.size(); count > 1; --count) {
                for (size_t index = 0; index + 1 < count; ++index)
                    points[index] = Lerp(points[index], points[index + 1], t);
            }

            return points.front();
        }

        void GenerateBezierControlPoints(BezierState& state,
                                                const Vector3& current,
                                                const Vector3& target,
                                                const Config::AimMethodPreset* methodPreset) {
            const int intermediateCount = std::clamp(
                methodPreset ? methodPreset->bezierControlPoints : Config::View_aimBezierControlPoints(),
                2,
                6);
            const float rawCurvature = methodPreset ? methodPreset->bezierCurvature : Config::View_aimBezierCurvature();
            const float curvature = std::isfinite(rawCurvature) ? std::clamp(rawCurvature, 0.0f, 1.0f) : 0.5f;
            const Vector3 delta = target - current;
            const float distance = delta.Size();

            state.controlPoints.clear();
            state.controlPoints.reserve(static_cast<size_t>(intermediateCount) + 2);
            state.controlPoints.push_back(current);

            if (distance > 0.0001f) {
                const Vector3 perpendicular = PerpendicularTo(delta);
                const float offsetScale = distance * curvature * 0.35f;

                for (int index = 1; index <= intermediateCount; ++index) {
                    const float alpha = static_cast<float>(index) / static_cast<float>(intermediateCount + 1);
                    const float wave = sinf(alpha * M_PI_F);
                    const Vector3 base = current + delta * alpha;
                    state.controlPoints.push_back(base + perpendicular * (offsetScale * wave));
                }
            }

            state.controlPoints.push_back(target);
            state.lastTarget = target;
            state.t = 0.0f;
            state.initialized = true;
        }

    } // namespace AimSmoothingDetail

    void ResetAimSmoothingState() {
        AimSmoothingDetail::ResetPIDState();
        AimSmoothingDetail::ResetBezierState();
        AimSmoothingDetail::ResetOvershootState();
        AimSmoothingDetail::ResetHybridState();
        AimSmoothingDetail::ResetDeltaTime();
    }

    void HoldAimSmoothingState() {
        AimSmoothingDetail::ResetPIDState();
        AimSmoothingDetail::ResetBezierState();
        AimSmoothingDetail::ResetOvershootState();
        AimSmoothingDetail::ResetHybridState();
        AimSmoothingDetail::LastSmoothingTick() =
            std::chrono::steady_clock::now();
    }

    void PrimeAimSmoothingClock() {
        AimSmoothingDetail::LastSmoothingTick() =
            std::chrono::steady_clock::now();
    }

    Vector3 SmoothPID(Vector3 current,
                             Vector3 target,
                             float deltaTime,
                             const Config::AimMethodPreset* methodPreset) {
        AimSmoothingDetail::PIDState& state = AimSmoothingDetail::GetPIDState();
        deltaTime = AimSmoothingDetail::ClampDeltaTime(deltaTime);

        const Vector3 error = target - current;
        const float rawDeadzone = methodPreset ? methodPreset->pidDeadzone : Config::View_aimPidDeadzone();
        const float deadzone = std::clamp(
            (std::isfinite(rawDeadzone) ? rawDeadzone : 1.0f) * (M_PI_F / 180.0f),
            0.0f,
            M_PI_F / 2.0f);
        const float errorLength = error.Size();

        if (errorLength < deadzone) {
            Diagnostics::Aim("smooth.pid early_return reason=deadzone current=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f) error=(%.9f,%.9f,%.9f) error_len=%.9f deadzone=%.9f",
                current.X,
                current.Y,
                current.Z,
                target.X,
                target.Y,
                target.Z,
                error.X,
                error.Y,
                error.Z,
                errorLength,
                deadzone);
            state.integral = Vector3{};
            state.previousError = error;
            state.filteredDerivative = Vector3{};
            state.lastTarget = target;
            state.initialized = true;
            return current;
        }

        const bool resetState = !state.initialized ||
            AimSmoothingDetail::IsRetarget(target, state.lastTarget);

        if (resetState) {
            state.integral = Vector3{};
            state.previousError = error;
            state.filteredDerivative = Vector3{};
            state.initialized = true;
        }

        state.lastTarget = target;
        state.integral += error * deltaTime;
        const float rawMaxIntegral = methodPreset ? methodPreset->pidMaxIntegral : Config::View_aimPidMaxIntegral();
        state.integral = AimSmoothingDetail::ClampIntegral(
            state.integral,
            std::clamp(std::isfinite(rawMaxIntegral) ? rawMaxIntegral : 10.0f, 1.0f, 50.0f)
        );

        const Vector3 rawDerivative = resetState
            ? Vector3{}
            : (error - state.previousError) / deltaTime;
        constexpr float kDerivativeFilterTimeConstantSeconds = 0.012f;
        const float derivativeAlpha = resetState
            ? 1.0f
            : std::clamp(
                deltaTime /
                    (kDerivativeFilterTimeConstantSeconds + deltaTime),
                0.0f,
                1.0f);
        state.filteredDerivative +=
            (rawDerivative - state.filteredDerivative) * derivativeAlpha;
        state.previousError = error;

        const float rawP = methodPreset ? methodPreset->pidP : Config::View_aimPidP();
        const float rawI = methodPreset ? methodPreset->pidI : Config::View_aimPidI();
        const float rawD = methodPreset ? methodPreset->pidD : Config::View_aimPidD();
        const Vector3 output =
            error * std::clamp(std::isfinite(rawP) ? rawP : 0.5f, 0.0f, 2.0f) +
            state.integral * std::clamp(std::isfinite(rawI) ? rawI : 0.01f, 0.0f, 0.5f) +
            state.filteredDerivative *
                std::clamp(std::isfinite(rawD) ? rawD : 0.1f, 0.0f, 1.0f);

        return current + AimSmoothingDetail::ClampStepToError(output, error);
    }

    Vector3 SmoothBezier(Vector3 current,
                                Vector3 target,
                                float deltaTime,
                                float speed,
                                const Config::AimMethodPreset* methodPreset) {
        AimSmoothingDetail::BezierState& state = AimSmoothingDetail::GetBezierState();
        deltaTime = AimSmoothingDetail::ClampDeltaTime(deltaTime);

        const bool resetState = !state.initialized ||
            state.controlPoints.empty() ||
            AimSmoothingDetail::IsRetarget(target, state.lastTarget);

        if (resetState)
            AimSmoothingDetail::GenerateBezierControlPoints(state, current, target, methodPreset);
        else {
            state.lastTarget = target;
            state.controlPoints.back() = target;
        }

        state.t = std::clamp(
            state.t + std::clamp(speed, 0.0f, 200.0f) * deltaTime,
            0.0f,
            1.0f
        );

        if (state.t >= 1.0f)
            return target;

        return AimSmoothingDetail::EvaluateBezier(state.controlPoints, state.t);
    }

    Vector3 SmoothPiecewise(Vector3 current,
                                   Vector3 target,
                                   float speed,
                                   const Config::AimMethodPreset* methodPreset) {
        const Vector3 error = target - current;
        const float errorDegrees = RAD2DEG(error.Size());
        float scale = Config::AimPiecewiseNearScale(methodPreset);
        if (errorDegrees > Config::AimPiecewiseFarDegrees(methodPreset))
            scale = 1.00f;
        else if (errorDegrees > Config::AimPiecewiseMidDegrees(methodPreset))
            scale = Config::AimPiecewiseFarScale(methodPreset);
        else if (errorDegrees > Config::AimPiecewiseNearDegrees(methodPreset))
            scale = Config::AimPiecewiseMidScale(methodPreset);

        return SmoothLinear(current, target, std::clamp(speed * scale, 0.0f, 1.0f));
    }

    Vector3 SmoothConstantAngularVelocity(Vector3 current,
                                                 Vector3 target,
                                                 float deltaTime,
                                                 float degreesPerSecond) {
        deltaTime = AimSmoothingDetail::ClampDeltaTime(deltaTime);
        const Vector3 error = target - current;
        const float errorLength = error.Size();
        const float angularSpeedRad = DEG2RAD(std::clamp(
            std::isfinite(degreesPerSecond) ? degreesPerSecond : 30.0f,
            0.0f,
            Config::kAimConstantAngularSpeedMaxDeg));
        const float maxStep = angularSpeedRad * deltaTime;

        if (errorLength <= 0.000001f || maxStep <= 0.0f)
            return current;
        if (errorLength <= maxStep)
            return target;

        return current + error * (maxStep / errorLength);
    }

    Vector3 SmoothDispatchWithMethod(Vector3 local,
                                            Vector3 target,
                                            float speed,
                                            float accel,
                                            int methodOverride,
                                            float bezierSpeedOverride,
                                            bool commitOvershootStep) {
        const float deltaTime = AimSmoothingDetail::ComputeDeltaTime();
        const int method = std::clamp(methodOverride, 0, Config::kAimMethodCount - 1);
        thread_local int previousMethod = -1;

        if (method != previousMethod) {
            AimSmoothingDetail::ResetPIDState();
            AimSmoothingDetail::ResetBezierState();
            AimSmoothingDetail::ResetOvershootState();
            AimSmoothingDetail::ResetHybridState();
            previousMethod = method;
        }

        // Hybrid PID owns its braking curve and must converge on the real target.
        const Vector3 adjustedTarget = method == 6
            ? target
            : AimSmoothingDetail::ApplyOvershootCurveTarget(local, target);
        const Vector3 error = adjustedTarget - local;
        const Config::AimMethodPreset* methodPreset = Config::ActiveAimMethodPreset(method);
        const float methodSpeedScale = Config::RuntimeAimMethodAngularSpeedScale(method);
        const float slotSpeedScale = std::isfinite(speed) ? std::clamp(speed, 0.0f, 2.0f) : 0.0f;
        const float effectiveSpeed = std::clamp(slotSpeedScale * methodSpeedScale, 0.0f, 1.0f);
        const float effectiveStepFactor = OW::AimSmoothingTiming::NormalizePerTickFactor(
            effectiveSpeed,
            deltaTime);
        const float effectiveAccel = method == 4 ? Config::RuntimeAimMethodAcceleration(method) : accel;
        const float presetBezierSpeed = methodPreset ? methodPreset->bezierSpeed : Config::View_aimBezierSpeed();
        const float bezierSpeed = bezierSpeedOverride > 0.0f
            ? bezierSpeedOverride
            : presetBezierSpeed;
        const float effectiveBezierSpeed = std::clamp(bezierSpeed * slotSpeedScale * methodSpeedScale, 0.0f, 200.0f);
        const float constantAngularSpeedDeg = Config::RuntimeAimConstantAngularSpeedDeg();
        const float effectiveConstantAngularSpeedDeg = std::clamp(
            constantAngularSpeedDeg * slotSpeedScale * methodSpeedScale,
            0.0f,
            Config::kAimConstantAngularSpeedMaxDeg);
        Diagnostics::Aim("smooth.dispatch method=%d speed=%.9f slotSpeedScale=%.9f speedScale=%.9f effectiveSpeed=%.9f stepFactor=%.9f accel=%.9f effectiveAccel=%.9f dt=%.9f bezierSpeed=%.9f constantDegPerSec=%.9f local=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f) adjusted=(%.9f,%.9f,%.9f) error=(%.9f,%.9f,%.9f) error_len=%.9f",
            method,
            speed,
            slotSpeedScale,
            methodSpeedScale,
            effectiveSpeed,
            effectiveStepFactor,
            accel,
            effectiveAccel,
            deltaTime,
            effectiveBezierSpeed,
            effectiveConstantAngularSpeedDeg,
            local.X,
            local.Y,
            local.Z,
            target.X,
            target.Y,
            target.Z,
            adjustedTarget.X,
            adjustedTarget.Y,
            adjustedTarget.Z,
            error.X,
            error.Y,
            error.Z,
            error.Size());

        auto scaleCandidateStep = [&](const Vector3& candidate) {
            const Vector3 candidateDelta = candidate - local;
            return local + AimSmoothingDetail::ClampStepToError(candidateDelta * effectiveStepFactor, error);
        };

        auto normalizeReferenceCandidateStep = [&](const Vector3& candidate) {
            const float errorLength = error.Size();
            if (!std::isfinite(errorLength) || errorLength <= 0.000001f)
                return local;

            const Vector3 referenceDelta =
                AimSmoothingDetail::ClampStepToError(candidate - local, error);
            const float referenceFactor = std::clamp(
                referenceDelta.Size() / errorLength,
                0.0f,
                1.0f);
            if (!std::isfinite(referenceFactor) || referenceFactor <= 0.0f)
                return local;

            const float normalizedFactor =
                OW::AimSmoothingTiming::NormalizePerTickFactor(
                    referenceFactor,
                    deltaTime);
            return local + referenceDelta * (normalizedFactor / referenceFactor);
        };

        Vector3 result{};
        switch (method) {
        case 0:
            result = SmoothLinear(local, adjustedTarget, effectiveStepFactor);
            break;
        case 1:
            result = scaleCandidateStep(SmoothPID(local, adjustedTarget, deltaTime, methodPreset));
            break;
        case 2:
            result = SmoothBezier(local, adjustedTarget, deltaTime, effectiveBezierSpeed, methodPreset);
            break;
        case 3:
            result = normalizeReferenceCandidateStep(
                SmoothPiecewise(local, adjustedTarget, effectiveSpeed, methodPreset));
            break;
        case 4:
            result = normalizeReferenceCandidateStep(
                SmoothAccelerate(local, adjustedTarget, effectiveSpeed, effectiveAccel));
            break;
        case 5:
            result = SmoothConstantAngularVelocity(local, adjustedTarget, deltaTime, effectiveConstantAngularSpeedDeg);
            break;
        case 6: {
            AimHybrid::Tuning tuning{};
            tuning.p = methodPreset ? methodPreset->pidP : Config::View_aimPidP();
            tuning.i = methodPreset ? methodPreset->pidI : Config::View_aimPidI();
            tuning.d = methodPreset ? methodPreset->pidD : Config::View_aimPidD();
            tuning.maxIntegral = methodPreset ? methodPreset->pidMaxIntegral : Config::View_aimPidMaxIntegral();
            tuning.deadzoneDeg = methodPreset
                ? methodPreset->hybridDeadzoneDeg
                : Config::aimHybridDeadzoneDeg;
            const float hybridScale = std::clamp(slotSpeedScale * methodSpeedScale, 0.0f, 2.0f);
            tuning.constantSpeedDegPerSec = (methodPreset
                ? methodPreset->hybridConstantSpeedDeg
                : Config::aimHybridConstantSpeedDeg) * hybridScale;
            tuning.maxSpeedDegPerSec = (methodPreset
                ? methodPreset->hybridMaxSpeedDeg
                : Config::aimHybridMaxSpeedDeg) * hybridScale;
            tuning.accelerationDegPerSec2 = (methodPreset
                ? methodPreset->hybridAccelerationDeg
                : Config::aimHybridAccelerationDeg) * (std::max)(0.05f, hybridScale);
            tuning.decelerationDegPerSec2 = (methodPreset
                ? methodPreset->hybridDecelerationDeg
                : Config::aimHybridDecelerationDeg) * (std::max)(0.05f, hybridScale);
            tuning.nearRadiusDeg = methodPreset
                ? methodPreset->hybridNearRadiusDeg
                : Config::aimHybridNearRadiusDeg;
            tuning.targetMotionGain = methodPreset
                ? methodPreset->hybridTargetMotionGain
                : Config::aimHybridTargetMotionGain;
            tuning.suddenMotionBoost = methodPreset
                ? methodPreset->hybridSuddenMotionBoost
                : Config::aimHybridSuddenMotionBoost;
            const AimHybrid::StepResult hybrid = AimHybrid::Advance(
                local,
                adjustedTarget,
                deltaTime,
                tuning,
                AimSmoothingDetail::GetHybridState());
            Diagnostics::Aim(
                "smooth.hybrid speedDegS=%.3f targetSpeedDegS=%.3f targetAccelDegS2=%.3f accelLimitDegS2=%.3f reset=%d",
                hybrid.outputSpeedDegPerSec,
                hybrid.targetSpeedDegPerSec,
                hybrid.targetAccelerationDegPerSec2,
                hybrid.accelerationLimitDegPerSec2,
                hybrid.reset ? 1 : 0);
            result = hybrid.value;
            break;
        }
        default:
            result = SmoothLinear(local, adjustedTarget, effectiveStepFactor);
            break;
        }

        const Vector3 outputDelta = result - local;
        if (commitOvershootStep && method != 6)
            AimSmoothingDetail::CommitOvershootStep(outputDelta);
        Diagnostics::Aim("smooth.result method=%d result=(%.9f,%.9f,%.9f) output_delta=(%.9f,%.9f,%.9f) output_len=%.9f",
            method,
            result.X,
            result.Y,
            result.Z,
            outputDelta.X,
            outputDelta.Y,
            outputDelta.Z,
            outputDelta.Size());
        return result;
    }

    void CommitAimSmoothingOutput(const Vector3& outputDelta) {
        AimSmoothingDetail::CommitOvershootStep(outputDelta);
    }

    Vector3 SmoothDispatch(Vector3 local, Vector3 target, float speed, float accel) {
        return SmoothDispatchWithMethod(
            local,
            target,
            speed,
            accel,
            Config::AimBehaviorMethod(Config::View_aimBehavior()),
            Config::View_aimBezierSpeed());
    }


}
