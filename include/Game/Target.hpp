#pragma once
#include <cstdint>
#include "Game/PresentationTiming.hpp"
#include <cmath>
#include <algorithm>
#include <complex>
#include <vector>
#include <string>
#include <ctime>
#include <limits>
#include <mutex>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <atomic>
#include <functional>
#include <map>
#include <unordered_map>
#include <windows.h>
#include <chrono>
#include <optional>
#include <thread>
#include <utility>

#include "Game/Decrypt.hpp"
#include "Game/AimHybridController.hpp"
#include "Game/AimSmoothingTiming.hpp"
#include "Game/CalibrationPolicy.hpp"
#include "Game/Entity.hpp"
#include "Game/snapshots/SnapshotAccess.hpp"
#include "Game/FovGeometry.hpp"
#include "Game/HeroGeometrySpec.hpp"
#include "Game/InputOrchestrator.hpp"
#include "Game/LeadPrediction.hpp"
#include "Game/Motion.hpp"
#include "Game/RuntimeState.hpp"
#include "Game/TrackingContinuityPolicy.hpp"
#include "Game/TriggerBoneSelection.hpp"
#include "Game/WeaponSpec.hpp"
#include "output/OutputRuntime.hpp"
#include "output/Actions.hpp"
#include "output/PhysicalMask.hpp"
#include "features/aim/Smoothing.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"
#include "Utils/ProcessConnection.hpp"

extern std::mutex g_mutex;

namespace OW {

    inline constexpr uint64_t kPlayerControllerViewDirectionOffset = 0x1260;

    inline Vector3 DirectionToAimEuler(const Vector3& direction) {
        return Vector3(
            -asinf(direction.Y),
            atan2f(direction.X, direction.Z),
            0.0f
        );
    }

    inline Vector3 ReadPlayerControllerViewDirection(uint64_t playerControllerBase) {
        if (!playerControllerBase || !RuntimeWorkersRunning() ||
            !ProcessConnection::IsConnected() || !SDK || !SDK->IsInitialized())
            return Vector3{};
        if(consumerFrame)return consumerFrame->local.AngleBase==playerControllerBase ? consumerFrame->local.view_direction : Vector3{};
        return SDK->RPM<Vector3>(playerControllerBase + kPlayerControllerViewDirectionOffset);
    }

    // =========================================================================
    // Global extern declarations (defined in Overwatch.hpp)
    // =========================================================================

    inline int ReadPresentEnvFlagState(const char* name, int defaultValue)
    {
        char buffer[32]{};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0)
            return defaultValue;
        if (length >= sizeof(buffer))
            return defaultValue;

        std::string value(buffer, buffer + (std::min<DWORD>)(length, sizeof(buffer) - 1));
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (value == "1" || value == "true" || value == "yes" || value == "on")
            return 1;
        if (value == "0" || value == "false" || value == "no" || value == "off")
            return 0;
        return defaultValue;
    }

    inline DWORD ReadPresentEnvDword(const char* name, DWORD fallback, DWORD minValue, DWORD maxValue)
    {
        char buffer[32]{};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0)
            return fallback;
        if (length >= sizeof(buffer))
            return fallback;

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(buffer, &end, 10);
        if (end == buffer)
            return fallback;

        return std::clamp<DWORD>(static_cast<DWORD>(parsed), minValue, maxValue);
    }

    inline bool PresentInterpolationEnabled()
    {
        static const bool enabled = ReadPresentEnvFlagState("UN_DMA_PRESENT_INTERP", 0) == 1;
        return enabled;
    }

    inline bool PresentUseForRenderEnabled()
    {
        static const bool enabled =
            PresentInterpolationEnabled() &&
            ReadPresentEnvFlagState("UN_DMA_PRESENT_USE_FOR_RENDER", 1) == 1;
        return enabled;
    }

    inline DWORD PresentEntityHz()
    {
        static const DWORD hz = ReadPresentEnvDword("UN_DMA_ENTITY_PRESENT_HZ", 120, 30, 144);
        return hz;
    }

    inline DWORD PresentEntityDelayMs()
    {
        static const DWORD delayMs = ReadPresentEnvDword("UN_DMA_ENTITY_PRESENT_DELAY_MS", 16, 0, 48);
        return delayMs;
    }

    inline DWORD PresentRenderEntityDelayMs()
    {
        static const DWORD delayMs = ReadPresentEnvDword(
            "UN_DMA_ENTITY_PRESENT_RENDER_DELAY_MS",
            24,
            0,
            64);
        return delayMs;
    }

    inline DWORD PresentMaxExtrapolationMs()
    {
        static const DWORD maxMs = ReadPresentEnvDword("UN_DMA_PRESENT_MAX_EXTRAP_MS", 8, 0, 16);
        return maxMs;
    }

    inline DWORD PresentRenderMicroExtrapolationMs()
    {
        static const DWORD maxMs = ReadPresentEnvDword("UN_DMA_RENDER_PRESENT_MICRO_EXTRAP_MS", 8, 0, 16);
        return maxMs;
    }

    // =========================================================================
    // Color4 = ImVec4 (Config uses ImVec4 directly)

    // =========================================================================
    // Config variables are in Utils/Config.hpp
    // =========================================================================

    // =========================================================================
    // Keyboard / input helpers
    // =========================================================================

    // =========================================================================
    // Sensitivity auto-calibration
    // =========================================================================

    inline float ResolveCalibrationReferenceGameSensitivity(float referenceOverride = 0.0f) {
        if (std::isfinite(referenceOverride) && referenceOverride > 0.0f)
            return referenceOverride;
        if (std::isfinite(Config::gameMouseSensitivity) && Config::gameMouseSensitivity > 0.0f)
            return Config::gameMouseSensitivity;
        return Config::referenceGameSensitivity;
    }

    struct CalibrationOutputCleanup {
        bool enabled;
        ~CalibrationOutputCleanup() {
            if (enabled && V2::Output::ReleaseActiveRuntime() != success)
                Diagnostics::Warn("[CALIBRATE] Output cleanup failed; device output gate remains closed.");
        }
    };

    inline float CalibrateSensitivity(bool calibrateBothAxes = true,
                                      float referenceGameSensitivityOverride = 0.0f,
                                      CalibrationMeasurement* measurement = nullptr) {
        if (measurement)
            *measurement = {};
        CalibrationRunGuard calibrationRun(Config::calibrationInProgress);
        if (!calibrationRun || ShouldSuppressOutput("calibration_move") ||
            !SDK || !ProcessConnection::IsConnected()) {
            Diagnostics::Aim("output.calibration skipped reason=busy_or_output_suppressed");
            return 0.0f;
        }
        CalibrationOutputCleanup cleanup{calibrationRun.IsRoot()};
        if (calibrationRun.IsRoot() && V2::Output::ReleaseActiveRuntime() != success)
            return 0.0f;
        const OutputRuntimeState outputRuntime = CurrentOutputRuntimeState();
        const uint64_t connectionEpoch = ProcessConnection::ConnectionEpoch();
        if (!SDK || connectionEpoch == 0 || !ProcessConnection::IsConnected() ||
            !outputRuntime.outputGateOpen || outputRuntime.backendGeneration == 0 ||
            Config::calibrationMovePixels <= 0 || Config::calibrationStabilityWaitMs < 0)
            return 0.0f;
        const uint64_t controller = SDK->g_player_controller;
        if (!controller)
            return 0.0f;
        const float referenceSensitivity =
            ResolveCalibrationReferenceGameSensitivity(referenceGameSensitivityOverride);
        if (!std::isfinite(referenceSensitivity) || referenceSensitivity <= 0.0f)
            return 0.0f;

        const auto sessionCurrent = [&] {
            const auto current = CurrentOutputRuntimeState();
            return RuntimeWorkersRunning() && ProcessConnection::IsConnected() &&
                ProcessConnection::ConnectionEpoch() == connectionEpoch &&
                SDK && SDK->IsInitialized() && SDK->g_player_controller.load() == controller &&
                current.outputGateOpen &&
                current.backendGeneration == outputRuntime.backendGeneration &&
                !ShouldSuppressOutput("calibration_move");
        };
        const auto readAngle = [&](bool pitch, float& angle) {
            if (!sessionCurrent())
                return false;
            Vector3 direction{};
            if (!SDK->read_range(controller + kPlayerControllerViewDirectionOffset,
                                 &direction, sizeof(direction)))
                return false;
            const double lengthSquared = double(direction.X) * direction.X +
                double(direction.Y) * direction.Y + double(direction.Z) * direction.Z;
            if (!std::isfinite(lengthSquared) || lengthSquared < 0.90 || lengthSquared > 1.10 ||
                !std::isfinite(direction.Y) || std::fabs(direction.Y) > 1.0f)
                return false;
            const Vector3 euler = DirectionToAimEuler(direction);
            angle = pitch ? euler.X : euler.Y;
            return std::isfinite(angle) && sessionCurrent();
        };
        const auto waitStable = [&] {
            return RuntimeWorkerSleep(static_cast<uint32_t>(Config::calibrationStabilityWaitMs)) &&
                sessionCurrent();
        };
        const auto measure = [&](bool pitch) {
            float before = 0.0f, after = 0.0f;
            if (!waitStable() || !readAngle(pitch, before))
                return 0.0f;
            const int counts = Config::calibrationMovePixels;
            if (EnqueueOutputPixelMoveForGeneration(pitch ? 0 : counts, pitch ? counts : 0,
                    0, outputRuntime.backendGeneration, connectionEpoch) != success ||
                !waitStable() || !readAngle(pitch, after))
                return 0.0f;
            return CalibrationCountsPerRadian(counts, before, after);
        };
        const float yaw = measure(false);
        const float pitch = calibrateBothAxes && yaw > 0.0f ? measure(true) : 0.0f;
        if (!CalibrationAxesReady(yaw, pitch, calibrateBothAxes) || !sessionCurrent()) {
            Diagnostics::Warn("[CALIBRATE] Incomplete sample or changed session; previous calibration retained.");
            return 0.0f;
        }
        // Commit only a complete measurement from the same device and game session.
        if (measurement)
            *measurement = {yaw, calibrateBothAxes ? pitch : yaw, referenceSensitivity};
        else {
            Config::calibratedCountsPerRadian = yaw;
            Config::calibratedPitchCountsPerRadian = calibrateBothAxes && std::fabs(pitch - yaw) / yaw > 0.05f
                ? pitch : 0.0f;
            Config::referenceGameSensitivity = referenceSensitivity;
        }
        std::printf("[CALIBRATE] Result: yaw=%.1f pitch=%.1f counts/rad refGameSens=%.3f\n",
            yaw, calibrateBothAxes ? pitch : yaw, referenceSensitivity);
        return yaw;
    }

    inline float RunCalibrationSamples(float referenceGameSensitivityOverride = 0.0f) {
        CalibrationRunGuard calibrationRun(Config::calibrationInProgress);
        if (!calibrationRun || Config::calibrationStabilityWaitMs < 0 ||
            ShouldSuppressOutput("calibration_move") || !SDK || !ProcessConnection::IsConnected())
            return 0.0f;
        CalibrationOutputCleanup cleanup{calibrationRun.IsRoot()};
        if (calibrationRun.IsRoot() && V2::Output::ReleaseActiveRuntime() != success)
            return 0.0f;
        const uint64_t connectionEpoch = ProcessConnection::ConnectionEpoch();
        const auto device = CurrentOutputRuntimeState();
        const uint64_t controller = SDK ? SDK->g_player_controller.load() : 0;
        const float referenceSensitivity =
            ResolveCalibrationReferenceGameSensitivity(referenceGameSensitivityOverride);
        const auto batchCurrent = [&] {
            const auto current = CurrentOutputRuntimeState();
            return RuntimeWorkersRunning() && ProcessConnection::IsConnected() &&
                connectionEpoch != 0 && ProcessConnection::ConnectionEpoch() == connectionEpoch &&
                SDK && SDK->IsInitialized() && controller != 0 &&
                SDK->g_player_controller.load() == controller && device.outputGateOpen &&
                current.outputGateOpen && current.backendGeneration == device.backendGeneration &&
                !ShouldSuppressOutput("calibration_move");
        };
        if (!batchCurrent() || !std::isfinite(referenceSensitivity) || referenceSensitivity <= 0.0f)
            return 0.0f;
        double yawTotal = 0.0, pitchTotal = 0.0;
        int validSamples = 0;
        const int numSamples = (std::max)(Config::calibrationSampleCount, 1);
        for (int i = 0; i < numSamples; ++i) {
            if (!batchCurrent())
                return 0.0f;
            CalibrationMeasurement sample;
            std::printf("[CALIBRATE] Sample %d/%d starting...\n", i + 1, numSamples);
            const float result = CalibrateSensitivity(true, referenceSensitivity, &sample);
            if (!batchCurrent())
                return 0.0f;
            if (result > 0.0f && CalibrationAxesReady(sample.yaw, sample.pitch, true)) {
                yawTotal += sample.yaw;
                pitchTotal += sample.pitch;
                ++validSamples;
            }
            if (i < numSamples - 1) {
                const uint64_t pause = uint64_t(Config::calibrationStabilityWaitMs) * 2;
                if (pause > UINT32_MAX || !RuntimeWorkerSleep(static_cast<uint32_t>(pause)) || !batchCurrent())
                    return 0.0f;
            }
        }
        const auto result = CalibrationMean(yawTotal, pitchTotal, validSamples, referenceSensitivity);
        if (!CalibrationAxesReady(result.yaw, result.pitch, true) || !batchCurrent()) {
            Diagnostics::Warn("[CALIBRATE] Batch failed; previous calibration retained.");
            return 0.0f;
        }
        // No intermediate sample changed persistent settings; publish the paired mean once.
        Config::calibratedCountsPerRadian = result.yaw;
        Config::calibratedPitchCountsPerRadian = std::fabs(result.pitch - result.yaw) / result.yaw > 0.05f
            ? result.pitch : 0.0f;
        Config::referenceGameSensitivity = result.referenceSensitivity;
        std::printf("[CALIBRATE] Averaged %d valid paired samples: yaw=%.1f pitch=%.1f counts/rad refGameSens=%.3f\n",
            validSamples, result.yaw, result.pitch, result.referenceSensitivity);
        return result.yaw;
    }

    inline int get_bind_id(int setting) {
        return Labels::AimActivationKeyVk(setting);
    }

    inline unsigned int convertToHex(const ImVec4& color) {
        unsigned int red   = static_cast<unsigned int>(color.x * 255.f);
        unsigned int green = static_cast<unsigned int>(color.y * 255.f);
        unsigned int blue  = static_cast<unsigned int>(color.z * 255.f);
        unsigned int alpha = static_cast<unsigned int>(color.w * 255.f);
        return (alpha << 24) | (blue << 16) | (green << 8) | red;
    }

    // =========================================================================
    // Simple math helpers
    // =========================================================================

    inline std::pair<double, double> dd(double a, double b, double c, double d, double m, double n) {
        return std::pair<double, double>(
            (m * d - c * n) / (a * d - c * b),
            (a * n - b * m) / (a * d - c * b)
        );
    }

    // =========================================================================
    // Aim smoothing
    // =========================================================================

    // =========================================================================
    // Projectile prediction (quartic solver)
    // =========================================================================

    inline void SolveQuartic(const std::complex<float> coefficients[5], std::complex<float> roots[4]) {
        const std::complex<float> a = coefficients[4];
        const std::complex<float> b = coefficients[3] / a;
        const std::complex<float> c = coefficients[2] / a;
        const std::complex<float> d = coefficients[1] / a;
        const std::complex<float> e = coefficients[0] / a;

        const std::complex<float> Q1 = c * c - 3.f * b * d + 12.f * e;
        const std::complex<float> Q2 = 2.f * c * c * c - 9.f * b * c * d + 27.f * d * d + 27.f * b * b * e - 72.f * c * e;
        const std::complex<float> Q3 = 8.f * b * c - 16.f * d - 2.f * b * b * b;
        const std::complex<float> Q4 = 3.f * b * b - 8.f * c;

        const std::complex<float> Q5 = std::pow(Q2 / 2.f + std::sqrt(Q2 * Q2 / 4.f - Q1 * Q1 * Q1), 1.f / 3.f);
        const std::complex<float> Q6 = (Q1 / Q5 + Q5) / 3.f;
        const std::complex<float> Q7 = 2.f * std::sqrt(Q4 / 12.f + Q6);

        roots[0] = (-b - Q7 - std::sqrt(4.f * Q4 / 6.f - 4.f * Q6 - Q3 / Q7)) / 4.f;
        roots[1] = (-b - Q7 + std::sqrt(4.f * Q4 / 6.f - 4.f * Q6 - Q3 / Q7)) / 4.f;
        roots[2] = (-b + Q7 - std::sqrt(4.f * Q4 / 6.f - 4.f * Q6 + Q3 / Q7)) / 4.f;
        roots[3] = (-b + Q7 + std::sqrt(4.f * Q4 / 6.f - 4.f * Q6 + Q3 / Q7)) / 4.f;
    }

    inline void AimCorrection(Vector3* InVecArg, Vector3 currVelocity, float Distance, float Bulletspeed, bool gravityEnabled) {
        double G = 9.8;
        const Vector3 camera = SnapshotCameraPosition();
        double A = camera.X;
        double B = camera.Y;
        double C = camera.Z;
        double M = InVecArg->X;
        double N = InVecArg->Y;
        double O = InVecArg->Z;
        double P = currVelocity.X;
        double Q = currVelocity.Y;
        double R = currVelocity.Z;
        double S = Bulletspeed;
        double H = M - A;
        double J = O - C;
        double K = N - B;
        double L = -0.5 * G;
        double c4 = L * L;
        double c3 = -2.0 * Q * L;
        double c2 = (Q * Q) - (2.0 * K * L) - (S * S) + (P * P) + (R * R);
        double c1 = (2.0 * K * Q) + (2.0 * H * P) + (2.0 * J * R);
        double c0 = (K * K) + (H * H) + (J * J);

        std::complex<float> pOutRoots[4];
        const std::complex<float> pInCoeffs[5] = { (float)c0, (float)c1, (float)c2, (float)c3, (float)c4 };
        SolveQuartic(pInCoeffs, pOutRoots);
        float fBestRoot = FLT_MAX;
        for (int i = 0; i < 4; i++) {
            if (pOutRoots[i].real() > 0.f && std::abs(pOutRoots[i].imag()) < 0.0001f && pOutRoots[i].real() < fBestRoot) {
                fBestRoot = pOutRoots[i].real();
            }
        }
        InVecArg->X = (float)(camera.X + (H + P * fBestRoot));
        if (gravityEnabled) {
            InVecArg->Y = (float)(camera.Y + (K + Q * fBestRoot - L * fBestRoot * fBestRoot));
        } else {
            InVecArg->Y += (float)(fBestRoot * currVelocity.Y);
        }
        InVecArg->Z = (float)(camera.Z + (J + R * fBestRoot));
    }

    inline void AimCorrection22(Vector3* InVecArg, Vector3 currVelocity, float Distance, float Bulletspeed, bool gravityEnabled) {
        double G = 9.8;
        const Vector3 camera = SnapshotCameraPosition();
        double A = camera.X;
        double B = camera.Y;
        double C = camera.Z;
        double M = InVecArg->X;
        double N = InVecArg->Y;
        double O = InVecArg->Z;
        double P = currVelocity.X;
        double Q = currVelocity.Y;
        double R = currVelocity.Z;
        double S = Bulletspeed;
        double H = M - A;
        double J = O - C;
        double K = N - B;
        double L = -0.5 * G;
        double c4 = L * L;
        double c3 = -2.0 * Q * L;
        double c2 = (Q * Q) - (2.0 * K * L) - (S * S) + (P * P) + (R * R);
        double c1 = (2.0 * K * Q) + (2.0 * H * P) + (2.0 * J * R);
        double c0 = (K * K) + (H * H) + (J * J);

        std::complex<float> pOutRoots[4];
        const std::complex<float> pInCoeffs[5] = { (float)c0, (float)c1, (float)c2, (float)c3, (float)c4 };
        SolveQuartic(pInCoeffs, pOutRoots);
        float fBestRoot = FLT_MAX;
        for (int i = 0; i < 4; i++) {
            if (pOutRoots[i].real() > 0.f && std::abs(pOutRoots[i].imag()) < 0.0001f && pOutRoots[i].real() < fBestRoot) {
                fBestRoot = pOutRoots[i].real();
            }
        }
        InVecArg->X = (float)(camera.X + (H + P * fBestRoot));
        if (gravityEnabled) {
            InVecArg->Y = (float)(camera.Y + (K + Q * fBestRoot - L * fBestRoot * fBestRoot));
        } else {
            InVecArg->Y += (float)(fBestRoot * currVelocity.Y);
        }
        InVecArg->Z = (float)(camera.Z + (J + R * fBestRoot));
    }

    namespace TargetingDetail {

        constexpr float kNoTargetScore = 100000.f;
        inline std::atomic<size_t> published_entity_count{0};

        struct SelectionResult {
            int index = -1;
            Vector3 target{};
            c_entity entity{};
            float score = kNoTargetScore;
        };

        struct VelocitySample {
            Vector3 velocity{}, acceleration{};
            uint64_t tick = 0;
            bool initialized = false;
        };

        inline std::mutex velocity_history_mutex;
        inline std::unordered_map<uint64_t, VelocitySample> velocity_history;

        struct TargetLockRuntime {
            uint64_t entityKey = 0;
            uint64_t actorGeneration = 0;
            int entityIndex = -1;
            DWORD lockStartedTick = 0;
            DWORD lastSeenTick = 0;
            float lastScore = kNoTargetScore;
            bool active = false;
        };

        inline std::mutex target_lock_mutex;
        inline TargetLockRuntime target_lock_runtime;

        inline bool IsZeroVector(const Vector3& value) {
            return value == Vector3(0, 0, 0);
        }

        inline Vector2 CrosshairCenter() {
            float width = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
            float height = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
            if (WX > 0.0f && WY > 0.0f) {
                width = WX;
                height = WY;
            } else if (Config::manualScreenWidth > 0 && Config::manualScreenHeight > 0) {
                width = static_cast<float>(Config::manualScreenWidth);
                height = static_cast<float>(Config::manualScreenHeight);
            }
            return Vector2(
                width * 0.5f,
                height * 0.5f
            );
        }

        inline Vector3 CameraPosition() {
            return OW::SnapshotCameraPosition();
        }

        using FovRuntimeContext = FovGeometry::RuntimeContext;

        inline bool IsFiniteVector(const Vector3& value) {
            return FovGeometry::IsFiniteVector(value);
        }

        inline Vector3 NormalizeVector(const Vector3& value) {
            return FovGeometry::NormalizeVector(value);
        }

        inline FovRuntimeContext SnapshotFovRuntimeContext() {
            Matrix view{}, viewXor{};
            SnapshotViewMatrices(view, viewXor);

            const auto camera = viewXor.get_location();
            const auto forward = viewXor.get_rotation();

            FovRuntimeContext context{};
            context.camera = Vector3(camera.x, camera.y, camera.z);
            context.forward = NormalizeVector(Vector3(forward.x, forward.y, forward.z));
            context.valid = IsFiniteVector(context.camera) &&
                            !IsZeroVector(context.camera) &&
                            !IsZeroVector(context.forward);
            return context;
        }

        inline float FovAngleLimitDeg(float fovDeg) {
            return Config::ClampFovDeg(fovDeg);
        }

        inline float AngularSeparationDegFromCamera(const Vector3& camera,
                                                    const Vector3& a,
                                                    const Vector3& b) {
            return FovGeometry::AngularSeparationDegFromCamera(camera, a, b);
        }

        inline float FovScoreDeg(const FovRuntimeContext& context, const Vector3& position) {
            return FovGeometry::FovScoreDeg(context, position);
        }

        inline bool IsWithinFovDeg(const FovRuntimeContext& context,
                                   const Vector3& position,
                                   float fovDeg,
                                   float* outScoreDeg = nullptr) {
            return FovGeometry::IsWithinFovDeg(
                context,
                position,
                FovAngleLimitDeg(fovDeg),
                outScoreDeg);
        }

        using CandidateFovEvaluation = FovGeometry::CandidateFovEvaluation;

        inline bool EvaluateCandidateFov(const FovRuntimeContext& context,
                                         const Vector3& rawAimPoint,
                                         const Vector3& finalAimPoint,
                                         float fixedFovDeg,
                                         FovGeometry::ResolveFovForDistanceFn resolveFov,
                                         CandidateFovEvaluation& outEvaluation) {
            return FovGeometry::EvaluateCandidateFov(
                context,
                rawAimPoint,
                finalAimPoint,
                fixedFovDeg,
                resolveFov,
                outEvaluation);
        }

        inline bool IsTrainingBot(uint64_t heroId) {
            return GameData::IsTrainingBotHeroId(heroId);
        }

        inline bool IsSpecialAimEntity(uint64_t heroId) {
            return heroId == 0x16dd || heroId == 0x16ee;
        }

        inline void SetPublishedEntityCount(size_t count) {
            published_entity_count.store(count, std::memory_order_release);
            Diagnostics::RecordEntityPublish(count);
        }

        inline size_t PublishedEntityCount() {
            return published_entity_count.load(std::memory_order_acquire);
        }

        inline bool HasPublishedEntities() {
            return PublishedEntityCount() > 0;
        }

        template<class Tick = decltype(&::GetTickCount)>
        inline bool TryPublishEntitySnapshots(
            std::vector<c_entity>&& nextEntities,
            std::vector<hpanddy>&& nextDynamicEntities,
            uint64_t expectedConnectionEpoch,
            size_t& publishedCount,
            const EntityRegistry::Snapshot* registry=nullptr,
            uint64_t expectedScene=0, Tick tick = &::GetTickCount)
        {
            const size_t count = nextEntities.size();
            {
                std::lock_guard<std::mutex> lock(::g_mutex);
                if (expectedScene && expectedScene != EntitySceneGeneration()) {
                    ++rejectedEntitySceneResults; return false;
                }
                std::unique_lock<std::mutex> registryLock(entityRegistryMonitor.mutex);
                if(registry && registry->generation) {
                    // Invalidation must publish even when the source is unreadable.
                    if(!entityRegistryMonitor.state.Accept(*registry) &&
                        (!nextDynamicEntities.empty() || std::any_of(nextEntities.begin(),nextEntities.end(),
                            [](const auto& value){return value.body_valid;}))) return false;
                }
                if (expectedConnectionEpoch != 0 &&
                    (!RuntimeWorkersRunning() || !ProcessConnection::IsConnected() ||
                     ProcessConnection::ConnectionEpoch() != expectedConnectionEpoch)) {
                    return false;
                }
                std::lock_guard<std::mutex> bodyLock(entityBodyValidityMutex);
                if(expectedScene && expectedScene!=EntitySceneGeneration())return false;
                const auto previousFrame=SnapshotFrame();
                const auto& previousEntities=previousFrame->entities;
                for(const auto& value:nextEntities) {
                    const auto current=std::find_if(previousEntities.begin(),previousEntities.end(),[&](const auto& old) {
                        return old.address==value.address && old.connection_epoch==value.connection_epoch && old.scene_generation==value.scene_generation;
                    });
                    if(current!=previousEntities.end() && int32_t(value.anchor_sample_tick_ms-current->anchor_sample_tick_ms)<0)return false;
                    const auto authority=entityBodyAuthorities.find(value.address);
                    if(value.body_valid && (authority==entityBodyAuthorities.end() || !authority->second.alive))return false;
                    // Revoked seats may publish unavailable metadata; they cannot displace an admitted successor.
                    const bool invalidation=!value.body_valid && (authority==entityBodyAuthorities.end() ||
                        (value.body_authority_revision && !authority->second.admitted && authority->second.generation==value.actor_generation &&
                        authority->second.epoch==value.connection_epoch && authority->second.scene==value.scene_generation));
                    if((value.body_authority_revision || value.body_valid || authority!=entityBodyAuthorities.end()) && !invalidation &&
                        !EntityBodyAuthorityMatchesLocked({value.address,value.LinkParent,value.HeroID,value.match_id,value.last_discovered_tick_ms},
                            value.actor_generation,value.body_authority_revision,value.connection_epoch,value.scene_generation,value.body_absence_revision))return false;
                    if(authority!=entityBodyAuthorities.end() && registry && authority->second.registry!=registry->generation)return false;
                }
                {
                    activeEntityBodies.clear();
                    for(auto& value:nextEntities) {
                        if(value.body_valid && value.roster_state==EntityRosterState::Fresh &&
                            value.Alive && value.HasFreshLifeState(tick()) && value.scene_generation==EntitySceneGeneration() &&
                            PresentationTiming::Fresh(value.anchor_sample_tick_ms,tick())) {
                            value.body_valid=TryActivateEntityBodyLocked({value.address,value.LinkParent,value.HeroID,value.match_id,
                                value.last_discovered_tick_ms},value.actor_generation,value.scene_generation,tick(),value.body_absence_revision,
                                value.body_authority_revision,value.connection_epoch);
                        } else value.body_valid=false;
                        if(!value.body_valid && value.roster_state==EntityRosterState::Fresh) {
                            value.roster_state=EntityRosterState::Missing;value.Alive=false;value.Vis=false;
                        }
                    }
                }
                auto frame=std::make_shared<ConsumptionFrame>();
                frame->sequence=previousFrame->sequence+1;
                uint64_t sampleUs=0;
                for(const auto& entity:nextEntities)sampleUs=(std::max)(sampleUs,entity.sample_time_us);
                frame->sampledAt=sampleUs ? std::chrono::steady_clock::time_point(std::chrono::microseconds(sampleUs)) : std::chrono::steady_clock::now();
                frame->local=local_entity;
                const bool localTeamKnown=local_entity.team_known && local_entity.connection_epoch==ProcessConnection::ConnectionEpoch() &&
                    local_entity.scene_generation==EntitySceneGeneration() &&
                    EntityBodyAuthorityMatchesLocked({local_entity.address,local_entity.LinkParent,local_entity.HeroID,
                        local_entity.match_id,local_entity.last_discovered_tick_ms},local_entity.actor_generation,
                        local_entity.body_authority_revision,local_entity.connection_epoch,local_entity.scene_generation,local_entity.body_absence_revision);
                frame->local.team_known=localTeamKnown;
                for(auto& value:nextEntities) {
                    value.team_known=value.team_known && localTeamKnown;
                    value.Team=value.team_known && value.address!=local_entity.address &&
                        (value.team_key==(1u<<0x1B) || value.team_key!=local_entity.team_key);
                    if(GameData::IsFriendlyTrainingBotHeroId(value.HeroID))value.Team=false;
                }
                frame->entities=std::move(nextEntities);frame->dynamic=std::move(nextDynamicEntities);
                { std::lock_guard<std::mutex> cameraLock(g_viewMatrixMutex);
                    frame->view=viewMatrix;frame->camera=viewMatrix_xor;frame->cameraSample=viewMatrixSampleState; }
                PublishFrame(std::move(frame));
#ifdef UNLEASHED_SELF_TESTS
                entities=SnapshotFrame()->entities;hp_dy_entities=SnapshotFrame()->dynamic;
#endif
                SetPublishedEntityCount(count);
                Diagnostics::SetEntityCount(count);
            }

            publishedCount = count;
            return true;
        }

        inline size_t PublishEntitySnapshots(std::vector<c_entity>&& nextEntities,
                                             std::vector<hpanddy>&& nextDynamicEntities) {
            size_t publishedCount = 0;
            TryPublishEntitySnapshots(
                std::move(nextEntities),
                std::move(nextDynamicEntities),
                0,
                publishedCount);
            return publishedCount;
        }

        inline std::vector<c_entity> SnapshotEntities() {
            const auto frame=consumerFrame?consumerFrame:SnapshotFrame();
            return frame->entities;
        }

        inline std::vector<c_entity> SnapshotPresentEntities() {
            std::vector<c_entity> snapshot;
            if (!OW::PresentInterpolationEnabled())
                return snapshot;

            std::lock_guard<std::mutex> lock(OW::g_presentEntityMutex);
            snapshot = OW::present_entities;
            return snapshot;
        }

        inline std::vector<c_entity> SnapshotPresentRenderEntities() {
            std::vector<c_entity> snapshot;
            if (!OW::PresentUseForRenderEnabled())
                return snapshot;

            std::lock_guard<std::mutex> lock(OW::g_presentRenderEntityMutex);
            snapshot = OW::present_render_entities;
            return snapshot;
        }

        inline std::vector<hpanddy> SnapshotDynamicEntities() {
            const auto frame=consumerFrame?consumerFrame:SnapshotFrame();
            return frame->dynamic;
        }

        inline std::vector<hpanddy> SnapshotPresentDynamicEntities() {
            std::vector<hpanddy> snapshot;
            if (!OW::PresentInterpolationEnabled())
                return snapshot;

            std::lock_guard<std::mutex> lock(OW::g_presentEntityMutex);
            snapshot = OW::present_hp_dy_entities;
            return snapshot;
        }

        inline std::vector<hpanddy> SnapshotPresentRenderDynamicEntities() {
            std::vector<hpanddy> snapshot;
            if (!OW::PresentUseForRenderEnabled())
                return snapshot;

            std::lock_guard<std::mutex> lock(OW::g_presentRenderEntityMutex);
            snapshot = OW::present_render_hp_dy_entities;
            return snapshot;
        }

        inline c_entity SnapshotLocalEntity() {
            return OW::SnapshotLocalEntity();
        }

        inline bool IsValidIndex(int index, size_t size) {
            return index >= 0 && static_cast<size_t>(index) < size;
        }

        inline bool IsRuntimeTargetValid(const c_entity& entity, bool requireVisible = true) {
            if (!entity.team_known) return false;
            if (!entity.HasUsableBody(GetTickCount(),EntitySceneGeneration())) return false;
            if (entity.roster_state != EntityRosterState::Fresh) return false;
            if (!PresentationTiming::Fresh(entity.anchor_sample_tick_ms, GetTickCount())) return false;
            if (!entity.address || !entity.Alive) return false;
            if (requireVisible && !entity.Vis) return false;
            return true;
        }

        inline bool TryEntityAt(int index, c_entity& entity, bool requireVisible = false) {
            const auto snapshot = SnapshotEntities();
            if (!IsValidIndex(index, snapshot.size())) return false;
            entity = snapshot[static_cast<size_t>(index)];
            return IsRuntimeTargetValid(entity, requireVisible);
        }

        inline bool TargetTeamMatches(const c_entity& entity, int teamMode, const c_entity& local) {
            if (!entity.team_known || !local.team_known) return false;
            if (entity.address == local.address)
                return false;

            switch (teamMode) {
            case 0:  return entity.Team;  // Enemies
            case 1:  return !entity.Team; // Allies
            case 2:  return true;         // All
            default: return entity.Team;
            }
        }

        inline bool IsSelectableCandidate(const c_entity& entity, int teamMode, const c_entity& local) {
            return IsRuntimeTargetValid(entity, false) && TargetTeamMatches(entity, teamMode, local);
        }

        inline Vector3 CachedSkeletonBoneRawPosition(const c_entity& entity,int boneId) {
            const auto ids=entity.GetRenderSkel();
            for(size_t slot=0;slot<ids.size();++slot)
                if(ids[slot]==boneId && entity.skeleton_bone_valid[slot])return entity.skeleton_bones[slot];
            return {};
        }

        inline Vector3 CoreBoneRawPosition(const c_entity& entity, int boneId) {
            if (boneId == BONE_HEAD) return entity.head_pos;
            if (boneId == BONE_NECK) return entity.neck_pos;
            return entity.chest_pos;
        }

        inline Vector3 CoreBoneHitboxCenter(const c_entity& entity, int boneId) {
            return ResolveBoneHitboxCenter(entity, boneId, CoreBoneRawPosition(entity, boneId));
        }

        inline bool TryResolveSkeletonBonePoint(const c_entity& entity,
                                                std::size_t renderSlot,
                                                int& outBoneId,
                                                Vector3& outPoint) {
            if (renderSlot >= entity.skeleton_bones.size() ||
                renderSlot >= entity.skeleton_bone_valid.size()) {
                return false;
            }

            const int boneId = Plexies20260609::HitboxBoneIdForRenderSlot(renderSlot);
            if (boneId == Plexies20260609::kUnusedRenderSkeletonBone)
                return false;

            Vector3 rawPoint{};
            if (renderSlot == 0) {
                rawPoint = entity.head_pos;
            } else if (renderSlot == 1) {
                rawPoint = entity.neck_pos;
            } else if (renderSlot == 2) {
                rawPoint = entity.chest_pos;
            } else {
                if (!entity.skeleton_bone_valid[renderSlot])
                    return false;
                rawPoint = entity.skeleton_bones[renderSlot];
            }

            if (IsZeroVector(rawPoint) || rawPoint == entity.pos || !IsFiniteVector(rawPoint))
                return false;

            const Vector3 hitboxCenter = ResolveBoneHitboxCenter(entity, boneId, rawPoint);
            if (IsZeroVector(hitboxCenter) || !IsFiniteVector(hitboxCenter))
                return false;

            outBoneId = boneId;
            outPoint = hitboxCenter;
            return true;
        }

        inline SkeletonBoneMask EffectiveAimBoneMaskForDistance(SkeletonBoneMask rawMask,
                                                                 float distance) {
            SkeletonBoneMask mask = NormalizeSkeletonBoneMask(rawMask);
            const float headGate = std::clamp(Config::View_aimbotMaxHead(), 0.0f, 500.0f);
            if (headGate > 0.0f &&
                std::isfinite(distance) &&
                distance > headGate &&
                SkeletonBoneMaskIncludesGroup(mask, kSkeletonBoneHead)) {
                mask &= ~kSkeletonBoneHead;
                // Preserve the legacy Head-only behavior: beyond Max Head
                // Distance it falls back to Neck instead of losing the target.
                if (mask == 0)
                    mask = kSkeletonBoneNeck;
            }
            return NormalizeSkeletonBoneMask(mask);
        }

        inline bool TrySelectClosestSelectedSkeletonPoint(
            const c_entity& entity,
            SkeletonBoneMask rawMask,
            const FovRuntimeContext& fovContext,
            const Matrix& view,
            const Vector2& crosshair,
            int& outBoneId,
            Vector3& outPoint,
            float* outScore = nullptr) {
            const SkeletonBoneMask mask = NormalizeSkeletonBoneMask(rawMask);
            bool found = false;
            float bestScore = (std::numeric_limits<float>::max)();

            for (std::size_t renderSlot = 0;
                 renderSlot < kSkeletonBoneGroupForRenderSlot.size();
                 ++renderSlot) {
                if (!SkeletonBoneMaskIncludesRenderSlot(mask, renderSlot))
                    continue;

                int boneId = Plexies20260609::kUnusedRenderSkeletonBone;
                Vector3 point{};
                if (!TryResolveSkeletonBonePoint(entity, renderSlot, boneId, point))
                    continue;

                const float score = fovContext.valid
                    ? TargetingDetail::FovScoreDeg(fovContext, point)
                    : crosshair.Distance(view.WorldToScreen(point));
                if (!std::isfinite(score))
                    continue;

                if (!found || score < bestScore) {
                    found = true;
                    bestScore = score;
                    outBoneId = boneId;
                    outPoint = point;
                }
            }

            if (found && outScore)
                *outScore = bestScore;
            return found;
        }

        inline Vector3 ConfiguredBonePosition(const c_entity& entity, int boneSetting) {
            const int normalized = Config::NormalizeAimBone(boneSetting);
            const int boneId = AimBoneToSkeletonBoneId(normalized);
            return CoreBoneHitboxCenter(entity, boneId);
        }

        inline int ResolveAimBoneForDistance(int configuredBone, float distance) {
            const int normalized = Config::NormalizeAimBone(configuredBone);
            const bool headRequested = normalized == Config::kAimBoneHead;
            const float headGate = std::clamp(Config::View_aimbotMaxHead(), 0.0f, 500.0f);
            if (headRequested && headGate > 0.0f && distance > headGate)
                return Config::kAimBoneNeck;
            return normalized;
        }

        inline bool TrySelectClosestCoreAimPoint(const c_entity& entity,
                                                 const FovRuntimeContext& fovContext,
                                                 const Matrix& view,
                                                 const Vector2& crosshair,
                                                 int& outBoneId,
                                                 Vector3& outPoint,
                                                 float* outScore = nullptr) {
            static constexpr std::array<int, 3> kCoreBones{
                BONE_HEAD,
                BONE_NECK,
                BONE_CHEST,
            };

            bool found = false;
            float bestScore = (std::numeric_limits<float>::max)();
            for (int boneId : kCoreBones) {
                const Vector3 point = CoreBoneHitboxCenter(entity, boneId);
                if (IsZeroVector(point) || point == entity.pos || !IsFiniteVector(point))
                    continue;

                const float score = fovContext.valid
                    ? TargetingDetail::FovScoreDeg(fovContext, point)
                    : crosshair.Distance(view.WorldToScreen(point));
                if (!std::isfinite(score))
                    continue;

                if (!found || score < bestScore) {
                    found = true;
                    bestScore = score;
                    outBoneId = boneId;
                    outPoint = point;
                }
            }

            if (found && outScore)
                *outScore = bestScore;
            return found;
        }

        inline bool TrySelectClosestSkeletonAimPoint(const c_entity& entity,
                                                     const FovRuntimeContext& fovContext,
                                                     const Matrix& view,
                                                     const Vector2& crosshair,
                                                     size_t maxSkeletonBones,
                                                     Vector3& outPoint,
                                                     float* outScore = nullptr) {
            const size_t limit = (std::min)(
                maxSkeletonBones,
                (std::min)(entity.skeleton_bones.size(), entity.skeleton_bone_valid.size()));
            if (limit == 0)
                return false;

            bool found = false;
            float bestScore = (std::numeric_limits<float>::max)();
            for (size_t i = 0; i < limit; ++i) {
                if (!entity.skeleton_bone_valid[i])
                    continue;

                const Vector3 point = entity.skeleton_bones[i];
                if (IsZeroVector(point) || point == entity.pos || !IsFiniteVector(point))
                    continue;

                const float score = fovContext.valid
                    ? TargetingDetail::FovScoreDeg(fovContext, point)
                    : crosshair.Distance(view.WorldToScreen(point));
                if (!std::isfinite(score))
                    continue;

                if (!found || score < bestScore) {
                    found = true;
                    bestScore = score;
                    outPoint = point;
                }
            }

            if (found && outScore)
                *outScore = bestScore;
            return found;
        }

        inline bool DistancePassesAimFilter(float distance,
                                            bool currentTarget = false) {
            const float minDistance = std::clamp(Config::View_aimbotMinDist(), 0.0f, 500.0f);
            const float maxDistance = std::clamp(Config::View_aimbotMaxDist(), 0.0f, 500.0f);
            if (minDistance > 0.0f && distance < minDistance)
                return false;
            const float effectiveMaxDistance =
                TrackingContinuityPolicy::EffectiveMaxDistance(
                    maxDistance,
                    currentTarget);
            if (effectiveMaxDistance > 0.0f && distance > effectiveMaxDistance)
                return false;
            return true;
        }

        inline uint64_t StableTargetKey(const c_entity& entity) {
            if (entity.roster_key != 0)
                return entity.roster_key;
            return entity.address ? entity.address : entity.LinkBase;
        }

        inline DWORD ResolvePolicySliderMs(float value) {
            return static_cast<DWORD>(std::clamp(value, 0.0f, 5000.0f) * 10.0f);
        }

        inline DWORD ResolveAimSessionTimeoutMs() {
            const float value = std::clamp(Config::View_aimbotMaxAim(), 0.0f, 100.0f);
            if (value <= 0.0f || value >= 100.0f)
                return 0;
            return static_cast<DWORD>(value * 20.0f);
        }

        inline TargetLockPolicy ResolveTargetLockPolicy() {
            TargetLockPolicy policy{};
            policy.traceMode = ClampTraceMode(Config::View_aimbotTrace());
            policy.unlockMode = ClampUnlockMode(Config::View_aimbotUnlock());
            policy.minLockMs = static_cast<float>(ResolvePolicySliderMs(Config::View_aimbotLockTime()));
            policy.maxLockMs = static_cast<float>(ResolveAimSessionTimeoutMs());
            policy.retargetHysteresis = std::clamp(Config::View_aimbotStickiness(), 0.0f, 100.0f);
            return policy;
        }

        inline TargetLockRuntime SnapshotTargetLockRuntime() {
            std::lock_guard<std::mutex> lock(target_lock_mutex);
            return target_lock_runtime;
        }

        inline std::mutex& LastTargetCandidateMutex() {
            static std::mutex mutex;
            return mutex;
        }

        inline TargetCandidate& LastTargetCandidateStorage() {
            static TargetCandidate candidate{};
            return candidate;
        }

        inline void StoreLastTargetCandidate(const TargetCandidate& candidate) {
            std::lock_guard<std::mutex> lock(LastTargetCandidateMutex());
            LastTargetCandidateStorage() = candidate;
        }

        inline TargetCandidate SnapshotLastTargetCandidate() {
            std::lock_guard<std::mutex> lock(LastTargetCandidateMutex());
            return LastTargetCandidateStorage();
        }

        inline void ResetTargetLockRuntime() {
            std::lock_guard<std::mutex> lock(target_lock_mutex);
            target_lock_runtime = TargetLockRuntime{};
        }

        inline void InvalidateTargetBody(uint64_t address,uint64_t rosterKey,uint64_t generation) {
            std::lock_guard<std::mutex> lock(target_lock_mutex);
            if(target_lock_runtime.active && target_lock_runtime.actorGeneration==generation &&
               (target_lock_runtime.entityKey==address || target_lock_runtime.entityKey==rosterKey ||
                target_lock_runtime.entityKey==PresentationTiming::ActorControlKey(address,generation)))
                target_lock_runtime=TargetLockRuntime{};
        }

        inline bool TargetLockExpired(const TargetLockRuntime& state,
                                      const TargetLockPolicy& policy,
                                      DWORD now) {
            if (!state.active || state.entityKey == 0)
                return true;
            if (policy.maxLockMs <= 0.0f)
                return false;
            return now - state.lockStartedTick >= static_cast<DWORD>(policy.maxLockMs);
        }

        inline bool CandidateCanBypassTrace(const TargetLockPolicy& policy,
                                            const TargetLockRuntime& state,
                                            uint64_t candidateKey) {
            if (policy.traceMode == TraceMode::Off)
                return true;
            if (policy.traceMode == TraceMode::Relaxed &&
                state.active &&
                candidateKey != 0 &&
                candidateKey == state.entityKey) {
                return true;
            }
            return false;
        }

        inline bool CandidateBlockedByMinLock(const TargetLockPolicy& policy,
                                              const TargetLockRuntime& state,
                                              uint64_t candidateKey,
                                              DWORD now) {
            if (!state.active || state.entityKey == 0 || candidateKey == 0 ||
                candidateKey == state.entityKey) {
                return false;
            }
            const DWORD minLockMs = static_cast<DWORD>(policy.minLockMs);
            return minLockMs > 0 && now - state.lockStartedTick < minLockMs;
        }

        inline float ApplyRetargetHysteresis(float score,
                                             const TargetLockPolicy& policy,
                                             const TargetLockRuntime& state,
                                             uint64_t candidateKey) {
            if (!state.active || state.entityKey == 0 || candidateKey == state.entityKey)
                return score;
            const float multiplier = 1.0f + std::clamp(policy.retargetHysteresis, 0.0f, 100.0f) * 0.01f;
            return score * multiplier;
        }

        inline void CommitTargetLockRuntime(const TargetCandidate& candidate,
                                            float selectedScore,
                                            const TargetLockPolicy& policy,
                                            DWORD now,
                                            DWORD missingTargetGraceMs = 0) {
            std::lock_guard<std::mutex> lock(target_lock_mutex);
            if (!candidate.valid || candidate.entityKey == 0) {
                const bool expired = target_lock_runtime.active &&
                    TargetLockExpired(target_lock_runtime, policy, now);
                const bool withinMissingGrace = target_lock_runtime.active &&
                    TrackingContinuityPolicy::IsWithinTargetDropoutGrace(
                        now,
                        target_lock_runtime.lastSeenTick,
                        missingTargetGraceMs);
                if (expired ||
                    (policy.unlockMode == UnlockMode::Anytime &&
                     !withinMissingGrace)) {
                    target_lock_runtime = TargetLockRuntime{};
                }
                return;
            }

            if (!target_lock_runtime.active ||
                target_lock_runtime.entityKey != candidate.entityKey ||
                target_lock_runtime.actorGeneration != candidate.entitySnapshot.actor_generation ||
                TargetLockExpired(target_lock_runtime, policy, now)) {
                target_lock_runtime.entityKey = candidate.entityKey;
                target_lock_runtime.actorGeneration = candidate.entitySnapshot.actor_generation;
                target_lock_runtime.entityIndex = candidate.entityIndex;
                target_lock_runtime.lockStartedTick = now;
                target_lock_runtime.active = true;
            }

            target_lock_runtime.lastSeenTick = now;
            target_lock_runtime.lastScore = selectedScore;
            target_lock_runtime.entityIndex = candidate.entityIndex;
        }

        inline EntityMotionState EstimateMotionState(const c_entity& entity, const Vector2& screenPoint) {
            (void)screenPoint;

            const Motion::EntityMotionEstimate motion = Motion::EstimateEntityMotion(entity);
            const Vector3 velocity = motion.valid ? motion.effectiveVelocity : entity.velocity;

            EntityMotionState state{};
            state.worldVelocity = velocity;
            state.verticalVelocity = velocity.Y;

            const float horizontalSpeed = sqrtf(
                velocity.X * velocity.X +
                velocity.Z * velocity.Z);
            const float verticalSpeed = velocity.Y;

            if (!std::isfinite(horizontalSpeed) || !std::isfinite(verticalSpeed) ||
                horizontalSpeed > 250.0f || fabsf(verticalSpeed) > 250.0f) {
                state.kind = EntityMotionState::Kind::TeleportOrInvalid;
                state.confidence = 0.25f;
                return state;
            }

            if (verticalSpeed > 1.0f) {
                state.kind = EntityMotionState::Kind::AirborneRising;
                state.confidence = 0.55f;
            } else if (verticalSpeed < -1.0f) {
                state.kind = EntityMotionState::Kind::AirborneFalling;
                state.confidence = 0.55f;
            } else if (horizontalSpeed > 2.0f) {
                state.kind = EntityMotionState::Kind::Strafing;
                state.confidence = motion.usedWorldDeltaFallback ? 0.50f : 0.45f;
            } else {
                state.kind = EntityMotionState::Kind::Grounded;
                state.confidence = 0.35f;
            }

            return state;
        }

        inline Vector3 ClampMagnitude(const Vector3& value, float maxLength) {
            const float length = value.get_length();
            if (length <= maxLength) return value;
            return value * (maxLength / length);
        }

        inline Vector3 AccelerationAwareVelocity(const c_entity& entity, float distance, float projectileSpeed) {
            const Motion::EntityMotionEstimate motion = Motion::EstimateEntityMotion(entity);
            const Vector3 baseVelocity = motion.valid ? motion.effectiveVelocity : entity.velocity;
            const uint64_t key = PresentationTiming::ActorControlKey(entity.address ? entity.address : entity.LinkBase,entity.actor_generation);
            if (!key) return baseVelocity;

            const uint64_t now = entity.sample_time_us ? entity.sample_time_us : uint64_t(entity.render_sample_tick_ms)*1000;
            const float safeSpeed = (std::max)(projectileSpeed, 1.0f);
            const float leadTime = std::clamp(distance / safeSpeed, 0.0f, 1.0f);
            Vector3 adjusted = baseVelocity;

            std::lock_guard<std::mutex> lock(velocity_history_mutex);
            if (velocity_history.size() > 256) velocity_history.clear();

            VelocitySample& sample = velocity_history[key];
            if (sample.initialized && now > sample.tick) {
                const float dt = static_cast<float>(now - sample.tick) / 1000000.0f;
                if (dt > 0.001f && dt < 0.5f) {
                    Vector3 acceleration = (baseVelocity - sample.velocity) / dt;
                    acceleration = ClampMagnitude(acceleration, 250.0f);
                    sample.acceleration = acceleration;
                }
            }

            if(!sample.initialized || now>sample.tick) {
                sample.velocity=baseVelocity;sample.tick=now;sample.initialized=true;
            }
            return adjusted + sample.acceleration * std::clamp(leadTime*0.5f,0.0f,0.25f);
        }

        inline ProjectileRuntimeSpec ResolveProjectileRuntimeSpec(const WeaponSpec* weapon,
                                                                  const c_entity& local,
                                                                  bool secondary) {
            if (Config::hanzoautospeed &&
                local.HeroID == eHero::HERO_HANJO) {
                Config::predit_level = readult(local.SkillBase + 0x40, 0xB, 0x2A5) * 85.f + 25.f;
                if (local.skill2act) Config::predit_level = 110.f;
            }

            const float fallbackSpeed = Config::predit_level;
            const bool fallbackGravity = Config::projectile_arc ||
                local.HeroID == eHero::HERO_HANJO ||
                Config::Gravitypredit;

            if (!secondary &&
                Config::hanzoautospeed &&
                local.HeroID == eHero::HERO_HANJO &&
                fallbackSpeed > 0.0f) {
                return ProjectileRuntimeSpec{
                    fallbackSpeed,
                    weapon ? weapon->projectile.gravity : true,
                    weapon != nullptr
                };
            }

            if (weapon && weapon->projectile.projectileSpeed > 0.0f) {
                return ProjectileRuntimeSpec{
                    weapon->projectile.projectileSpeed,
                    weapon->projectile.gravity,
                    true
                };
            }

            return ProjectileRuntimeSpec{
                fallbackSpeed,
                fallbackGravity,
                false
            };
        }

        inline Vector3 SnapshotLocalAimAngleForLeadEstimate() {
            const uint64_t playerControllerBase = SDK ? SDK->g_player_controller.load() : 0;
            if (playerControllerBase) {
                const Vector3 viewDir = ReadPlayerControllerViewDirection(playerControllerBase);
                const float dirLen = viewDir.Size();
                if (std::isfinite(viewDir.X) &&
                    std::isfinite(viewDir.Y) &&
                    std::isfinite(viewDir.Z) &&
                    dirLen > 0.5f &&
                    dirLen < 1.5f) {
                    return DirectionToAimEuler(viewDir);
                }
            }

            Matrix view{}, viewXor{};
            SnapshotViewMatrices(view, viewXor);
            const XMFLOAT3 forward = viewXor.get_rotation();
            const float forwardLen = sqrtf(
                forward.x * forward.x +
                forward.y * forward.y +
                forward.z * forward.z);
            if (std::isfinite(forward.x) &&
                std::isfinite(forward.y) &&
                std::isfinite(forward.z) &&
                forwardLen > 0.5f &&
                forwardLen < 1.5f) {
                return DirectionToAimEuler(Vector3(forward.x, forward.y, forward.z));
            }

            return Vector3{};
        }

        inline Vector3 AimAngleForWorldPoint(const Vector3& worldPoint) {
            const Vector3 camera = CameraPosition();
            const Vector3 delta = worldPoint - camera;
            const float distance = delta.Size();
            if (!std::isfinite(distance) || distance <= 0.0001f)
                return Vector3{};
            return DirectionToAimEuler(delta / distance);
        }

        inline int ResolveLeadAimMethod(bool secondary) {
            if (secondary) {
                return Config::SecondaryAimMethod(Config::Flick2 ? 1 : 0);
            }

            const int behavior = Config::ClampAimBehaviorIndex(Config::View_aimBehavior());
            return Config::AimBehaviorMethod(behavior);
        }

        inline float ResolveLeadSmoothInput(bool secondary) {
            if (secondary) {
                if (Config::Flick2)
                    return Config::AimBehaviorSmoothInput(Config::kAimBehaviorFlick, Config::Flick_smooth2);
                if (Config::Tracking2)
                    return Config::AimBehaviorSmoothInput(Config::kAimBehaviorTracking, Config::Tracking_smooth2);
                return 1.0f;
            }

            const int behavior = Config::ClampAimBehaviorIndex(Config::View_aimBehavior());
            const float scalePercent = Config::IsFlickBehavior(behavior)
                ? Config::View_Flick_smooth()
                : Config::View_Tracking_smooth();
            return Config::AimBehaviorSmoothInput(behavior, scalePercent);
        }

        inline LeadTimingEstimate EstimateLeadTimingForAimPoint(const Vector3& rawAimPoint, bool secondary) {
            const int method = ResolveLeadAimMethod(secondary);
            AimSettleEstimateInput input{};
            input.localAngle = SnapshotLocalAimAngleForLeadEstimate();
            input.targetAngle = AimAngleForWorldPoint(rawAimPoint);
            input.method = method;
            input.slotSpeedScale = ResolveLeadSmoothInput(secondary);
            input.methodSpeedScale = Config::RuntimeAimMethodAngularSpeedScale(method);
            input.constantAngularSpeedDeg = Config::RuntimeAimConstantAngularSpeedDeg();
            input.frameSeconds = kLeadDefaultFrameSeconds;

            return BuildLeadTiming(
                EstimateAimSettleTimeMs(input),
                Config::outputInputDelayMs);
        }

        inline const char* MotionVelocitySourceName(Motion::VelocitySource source) {
            switch (source) {
            case Motion::VelocitySource::Reported:
                return "reported";
            case Motion::VelocitySource::WorldDeltaFallback:
                return "world_delta";
            case Motion::VelocitySource::None:
            default:
                return "none";
            }
        }

        inline LeadPredictionResult ResolveLeadPrediction(const c_entity& entity,
                                                          Vector3 rawAimPoint,
                                                          const ProjectileRuntimeSpec& projectile,
                                                          bool predictionEnabled,
                                                          bool secondary,
                                                          float extraPreFireDelayMs = 0.0f) {
            LeadPredictionResult result{};
            result.predictionEnabled = predictionEnabled;
            result.secondary = secondary;
            result.rawAimPoint = rawAimPoint;
            result.preFireAimPoint = rawAimPoint;
            result.finalAimPoint = rawAimPoint;
            result.projectile = projectile;

            if (!predictionEnabled || IsZeroVector(rawAimPoint))
                return result;

            const Vector3 camera = CameraPosition();
            result.distance = camera.DistTo(rawAimPoint);
            const Motion::EntityMotionEstimate motion = Motion::EstimateEntityMotion(entity);
            result.targetVelocity = AccelerationAwareVelocity(entity, result.distance, projectile.projectileSpeed);
            result.timing = EstimateLeadTimingForAimPoint(rawAimPoint, secondary);
            const float preFireMaxMs = extraPreFireDelayMs > 0.0f ? 1000.0f : kLeadMaxPreFireDelayMs;
            result.timing.preFireDelayMs = ClampLeadDelayMs(
                result.timing.preFireDelayMs + extraPreFireDelayMs,
                preFireMaxMs);
            result.preFireAimPoint = ApplyTargetMotionPreFireDelay(
                rawAimPoint,
                result.targetVelocity,
                result.timing.preFireDelayMs,
                preFireMaxMs);
            result.finalAimPoint = result.preFireAimPoint;

            if (projectile.projectileSpeed > 0.0f) {
                const float leadDistance = camera.DistTo(result.preFireAimPoint);
                if (secondary) {
                    AimCorrection22(
                        &result.finalAimPoint,
                        result.targetVelocity,
                        leadDistance,
                        projectile.projectileSpeed,
                        projectile.gravity);
                } else {
                    AimCorrection(
                        &result.finalAimPoint,
                        result.targetVelocity,
                        leadDistance,
                        projectile.projectileSpeed,
                        projectile.gravity);
                }
            }

            Diagnostics::Aim("lead.solve enabled=%d secondary=%d projectile(source=%s speed=%.3f gravity=%d) motion(source=%s confidence=%.3f velocity=(%.3f,%.3f,%.3f)) timing(settleMs=%.3f inputMs=%d extraPreFireMs=%.3f preFireMs=%.3f) raw=(%.3f,%.3f,%.3f) preFire=(%.3f,%.3f,%.3f) final=(%.3f,%.3f,%.3f)",
                predictionEnabled ? 1 : 0,
                secondary ? 1 : 0,
                projectile.fromWeaponSpec ? "weapon" : "legacy",
                projectile.projectileSpeed,
                projectile.gravity ? 1 : 0,
                MotionVelocitySourceName(motion.velocitySource),
                motion.confidence,
                result.targetVelocity.X,
                result.targetVelocity.Y,
                result.targetVelocity.Z,
                result.timing.estimatedSettleMs,
                result.timing.inputDelayMs,
                extraPreFireDelayMs,
                result.timing.preFireDelayMs,
                result.rawAimPoint.X,
                result.rawAimPoint.Y,
                result.rawAimPoint.Z,
                result.preFireAimPoint.X,
                result.preFireAimPoint.Y,
                result.preFireAimPoint.Z,
                result.finalAimPoint.X,
                result.finalAimPoint.Y,
                result.finalAimPoint.Z);

            return result;
        }

        inline Vector3 ApplyPrediction(const c_entity& entity, Vector3 position, bool predit, bool secondary) {
            if (IsZeroVector(position)) return position;

            const c_entity local = SnapshotLocalEntity();
            const WeaponSpec* weapon = ResolveWeaponSpec(local.HeroID, Config::View_aimbotAttack());
            const bool resolvedPrediction = ResolvePredictionEnabled(
                ClampPredictionOverride(Config::View_aimbotPredictionMode()),
                weapon,
                predit);
            if (!resolvedPrediction) return position;

            const ProjectileRuntimeSpec projectile = ResolveProjectileRuntimeSpec(weapon, local, secondary);
            return ResolveLeadPrediction(entity, position, projectile, true, secondary).finalAimPoint;
        }

        inline float CandidateScore(const Vector3& position, const FovRuntimeContext& fovContext) {
            return TargetingDetail::FovScoreDeg(fovContext, position);
        }

        inline SelectionResult SelectTargetFromSnapshot(const std::vector<c_entity>& snapshot,
                                                        const c_entity& local,
                                                        bool predit,
                                                        bool secondary,
                                                        int teamMode,
                                                        int boneSetting,
                                                        float fov) {
            SelectionResult result{};
            Config::ScopedConfigRead configRead;
            const FovRuntimeContext fovContext = SnapshotFovRuntimeContext();

            for (size_t i = 0; i < snapshot.size(); ++i) {
                c_entity entity = snapshot[i];
                if (!IsSelectableCandidate(entity, teamMode, local)) continue;

                Vector3 rootPosition = ConfiguredBonePosition(entity, boneSetting);
                if (IsZeroVector(rootPosition)) continue;

                Vector3 aimPosition = ApplyPrediction(entity, rootPosition, predit, secondary);
                const float score = CandidateScore(aimPosition, fovContext);
                if (score >= result.score) continue;
                const float distance = fovContext.valid
                    ? fovContext.camera.DistTo(rootPosition)
                    : local.pos.DistTo(rootPosition);
                const float effectiveFov = Config::ResolveDynamicAimFovForDistance(fov, distance);
                if (score > FovAngleLimitDeg(effectiveFov)) continue;

                result.index = static_cast<int>(i);
                result.target = aimPosition;
                result.entity = entity;
                result.score = score;
            }

            return result;
        }

        inline Vector3 SelectAutoBone(c_entity entity, const Vector2& crosshair, bool predit, bool secondary, size_t maxSkeletonBones) {
            const Matrix view = OW::SnapshotViewMatrix();
            Config::ScopedConfigRead configRead;
            const FovRuntimeContext fovContext = SnapshotFovRuntimeContext();
            int bestBoneId = BONE_CHEST;
            Vector3 bestRoot{};
            if (!TrySelectClosestCoreAimPoint(entity, fovContext, view, crosshair, bestBoneId, bestRoot)) {
                const Vector3 configuredRoot =
                    ConfiguredBonePosition(entity, secondary ? Config::Bone2 : Config::View_Bone());
                if (!IsZeroVector(configuredRoot) &&
                    configuredRoot != entity.pos &&
                    IsFiniteVector(configuredRoot)) {
                    bestRoot = configuredRoot;
                } else if (!TrySelectClosestSkeletonAimPoint(
                    entity, fovContext, view, crosshair, maxSkeletonBones, bestRoot)) {
                    return Vector3{};
                }
            }
            return ApplyPrediction(entity, bestRoot, predit, secondary);
        }

        inline void ApplyTargetOutline(const c_entity& entity, bool secondary) {
            (void)entity;
            (void)secondary;
        }

        inline void UpdateHanzoAutoSpeed(const c_entity& local) {
            if (!Config::hanzoautospeed || local.HeroID != eHero::HERO_HANJO) return;
            Config::predit_level = readult(local.SkillBase + 0x40, 0xB, 0x2A5) * 85.f + 25.f;
            if (local.skill2act) Config::predit_level = 110.f;
        }
    }

    inline bool IsValidTargetIndex(int index) {
        c_entity target{};
        return TargetingDetail::TryEntityAt(index, target, false);
    }

    inline bool TryGetTargetEntity(int index, c_entity& entity, bool requireVisible = false) {
        return TargetingDetail::TryEntityAt(index, entity, requireVisible);
    }

    // =========================================================================
    // Angle helpers
    // =========================================================================

    inline bool in_range(Vector3 MyAngle, Vector3 EnemyAngle, Vector3 MyPosition, Vector3 EnemyPosition, float radius) {
        float dist = MyPosition.DistTo(EnemyPosition);
        radius /= dist;
        return MyAngle.DistTo(EnemyAngle) <= radius;
    }

    inline XMFLOAT3 CalcAngle(XMFLOAT3 Target, XMFLOAT3 CameraPos) {
        XMFLOAT3 Result;
        float Distance = XMVectorGetX(XMVector3Length(XMLoadFloat3(&Target) - XMLoadFloat3(&CameraPos)));
        if (Distance < 0.0001f) Distance = 0.0001f;
        XMStoreFloat3(&Result, (XMLoadFloat3(&Target) - XMLoadFloat3(&CameraPos)) / Distance);
        return Result;
    }

    // =========================================================================
    // Skill lookup (hanzo projectile speed)
    // =========================================================================

    inline float GetLookUpSkill(uint16_t a1) {
        const uint64_t localSkillBase = SnapshotLocalSkillBase();
        if (!localSkillBase)
            return 0.f;

        __try {
            uint64_t pSkill = SDK->RPM<uint64_t>(localSkillBase + 0x1848);
            uint64_t SkillRawList = SDK->RPM<uint64_t>(pSkill + 0x10);
            uint32_t SkillSize = SDK->RPM<uint32_t>(pSkill + 0x18);
            for (uint32_t i = 0; i < SkillSize; i++) {
                if (SDK->RPM<uint16_t>(SkillRawList + (i * 0x80)) == a1) {
                    float val = SDK->RPM<float>(SkillRawList + (i * 0x80) + 0x30);
                    if (val >= 0.5f) return val;
                }
            }
        } __except (1) {}
        return 0.f;
    }

    // =========================================================================
    // Main target selection (GetVector3)
    // =========================================================================

    inline TargetCandidate AcquireTarget(
        const ConsumptionFrame& frame,
        bool predit = false,
        bool ignoreInvisible = Config::View_aimbotIgnoreInvisible(),
        DWORD trackingContinuityGraceMs = 0) {
        TargetCandidate best{};
        int TarGetIndex = -1;
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        const Matrix& aimViewMatrix = frame.view;
        Config::ScopedConfigRead configRead;
        const TargetingDetail::FovRuntimeContext fovContext = TargetingDetail::SnapshotFovRuntimeContext();
        const auto& entities = frame.entities;
        const auto& hp_dy_entities = frame.dynamic;
        const auto& local_entity = frame.local;
        const WeaponSpec* weaponSpec = ResolveWeaponSpec(local_entity.HeroID, Config::View_aimbotAttack());
        const TargetLockPolicy lockPolicy = TargetingDetail::ResolveTargetLockPolicy();
        const DWORD now = GetTickCount();
        TargetingDetail::TargetLockRuntime activeLock = TargetingDetail::SnapshotTargetLockRuntime();
        if (TargetingDetail::TargetLockExpired(activeLock, lockPolicy, now))
            activeLock.active = false;
        const bool trackingContinuityEnabled = trackingContinuityGraceMs != 0;
        bool activeLockCandidateSelectable = false;
        const bool resolvedPrediction = ResolvePredictionEnabled(
            ClampPredictionOverride(Config::View_aimbotPredictionMode()),
            weaponSpec,
            predit);

        float origin = 100000.f;
        size_t selectableCandidates = 0;
        float selectedFovScoreDeg = 0.0f;
        bool targetFromBob = false;
        Diagnostics::Aim("target.primary start prediction=%d predictionMode=%d weapon=%s entities=%zu dynamic=%zu local_addr=0x%llX local_hero=0x%llX fovDeg=%.6f bone=%d autobone=%d boneMask=0x%03X teamMode=%d distance=(%.2f,%.2f) lock=(active=%d key=0x%llX minMs=%.0f maxMs=%.0f hysteresis=%.1f trace=%d unlock=%d) crosshair=(%.3f,%.3f)",
            resolvedPrediction ? 1 : 0,
            Config::View_aimbotPredictionMode(),
            weaponSpec ? weaponSpec->weaponId.data() : "none",
            entities.size(),
            hp_dy_entities.size(),
            static_cast<unsigned long long>(local_entity.address),
            static_cast<unsigned long long>(local_entity.HeroID),
            Config::View_Fov(),
            Config::View_Bone(),
            Config::View_autobone() ? 1 : 0,
            static_cast<unsigned int>(Config::View_aimbotBoneMask()),
            Config::View_aimbotTeam(),
            Config::View_aimbotMinDist(),
            Config::View_aimbotMaxDist(),
            activeLock.active ? 1 : 0,
            static_cast<unsigned long long>(activeLock.entityKey),
            lockPolicy.minLockMs,
            lockPolicy.maxLockMs,
            lockPolicy.retargetHysteresis,
            static_cast<int>(lockPolicy.traceMode),
            static_cast<int>(lockPolicy.unlockMode),
            CrossHair.X,
            CrossHair.Y);
        const ProjectileRuntimeSpec projectileSpec =
            TargetingDetail::ResolveProjectileRuntimeSpec(weaponSpec, local_entity, false);
        if (entities.size() > 0) {
            for (size_t i = 0; i < entities.size(); i++) {
                const bool teamPass = TargetingDetail::TargetTeamMatches(
                    entities[i], Config::View_aimbotTeam(), local_entity);
                if (TargetingDetail::IsRuntimeTargetValid(entities[i], false) && teamPass) {
                    const uint64_t candidateKey =
                        TargetingDetail::StableTargetKey(entities[i]);
                    const bool currentLockedCandidate =
                        activeLock.active && candidateKey != 0 &&
                        candidateKey == activeLock.entityKey;
                    const bool applyTrackingExitMargin =
                        trackingContinuityEnabled && currentLockedCandidate;
                    if (ignoreInvisible && !entities[i].Vis &&
                        !TargetingDetail::CandidateCanBypassTrace(lockPolicy, activeLock, candidateKey)) {
                        continue;
                    }
                    if (TargetingDetail::CandidateBlockedByMinLock(lockPolicy, activeLock, candidateKey, now))
                        continue;
                    ++selectableCandidates;
                    const float initialDistance = TargetingDetail::CameraPosition().DistTo(entities[i].chest_pos);
                    if (!TargetingDetail::DistancePassesAimFilter(
                            initialDistance,
                            applyTrackingExitMargin))
                        continue;

                    const SkeletonBoneMask candidateMask =
                        TargetingDetail::EffectiveAimBoneMaskForDistance(
                            Config::View_aimbotBoneMask(),
                            initialDistance);
                    int candidateBoneId = BONE_HEAD;
                    Vector3 rootPosition{};
                    if (!TargetingDetail::TrySelectClosestSelectedSkeletonPoint(
                            entities[i],
                            candidateMask,
                            fovContext,
                            aimViewMatrix,
                            CrossHair,
                            candidateBoneId,
                            rootPosition)) {
                        continue;
                    }

                    const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                        entities[i],
                        rootPosition,
                        projectileSpec,
                        resolvedPrediction,
                        false);
                    const float fovEntryHorizonMs =
                        Config::ClampFovEntryPredictionMs(Config::View_aimbotFovEntryPredictionMs());
                    const Vector3 fovEntryAimPoint = lead.finalAimPoint +
                        lead.targetVelocity * (fovEntryHorizonMs / 1000.0f);
                    FovGeometry::PredictedEntryEvaluation fovEvaluation{};
                    const float resolvedCandidateFovDeg =
                        Config::ResolveDynamicAimFovForDistance(
                            Config::View_Fov(),
                            fovContext.camera.DistTo(rootPosition));
                    const float candidateFovDeg =
                        TrackingContinuityPolicy::EffectiveFovDeg(
                            resolvedCandidateFovDeg,
                            applyTrackingExitMargin);
                    if (FovGeometry::EvaluateCandidateFovWithPredictedEntry(
                            fovContext,
                            rootPosition,
                            lead.finalAimPoint,
                            fovEntryAimPoint,
                            candidateFovDeg,
                            nullptr,
                            Config::View_aimbotPredictFovEntry(),
                            Config::ClampFovEntryMaxOutsideDeg(Config::View_aimbotFovEntryMaxOutsideDeg()),
                            fovEvaluation)) {
                        const float distance = fovEvaluation.distance;
                        const float effectiveFovDeg = fovEvaluation.effectiveFovDeg;
                        const float fovScoreDeg = fovEvaluation.acceptedByPrediction
                            ? fovEvaluation.projectedScoreDeg
                            : fovEvaluation.currentScoreDeg;
                        if (!TargetingDetail::DistancePassesAimFilter(
                                distance,
                                applyTrackingExitMargin))
                            continue;

                        if (currentLockedCandidate)
                            activeLockCandidateSelectable = true;

                        Vector2 Vec2 = CrossHair;
                        aimViewMatrix.WorldToScreen(fovEvaluation.acceptedAimPoint, &Vec2, Vector2(OW::WX, OW::WY));
                        float score;
                        if (Config::View_aimbotPriority() == 0) {
                            score = fovScoreDeg;
                        } else if (Config::View_aimbotPriority() == 1) {
                            score = entities[i].PlayerHealth;
                        } else {
                            score = distance;
                        }
                        score = TargetingDetail::ApplyRetargetHysteresis(score, lockPolicy, activeLock, candidateKey);
                        const bool preferCurrentInside = best.valid &&
                            best.predictedFovEntry &&
                            !fovEvaluation.acceptedByPrediction;
                        const bool predictedEntryLosesToCurrentInside = best.valid &&
                            !best.predictedFovEntry &&
                            fovEvaluation.acceptedByPrediction;
                        if (!predictedEntryLosesToCurrentInside &&
                            (preferCurrentInside || score < origin)) {
                            origin = score;
                            selectedFovScoreDeg = fovScoreDeg;
                            TarGetIndex = i;
                            best.valid = true;
                            best.entityIndex = static_cast<int>(i);
                            best.entityKey = candidateKey;
                            best.entitySnapshot = entities[i];
                            best.boneId = candidateBoneId;
                            best.rawAimPoint = rootPosition;
                            best.predictedAimPoint = lead.finalAimPoint;
                            best.aimPoint = fovEvaluation.acceptedAimPoint;
                            best.screenPoint = Vec2;
                            best.distance = distance;
                            best.fovScore = fovScoreDeg;
                            best.effectiveFovDeg = effectiveFovDeg;
                            best.dynamicFov = Config::IsDynamicAimFovActive();
                            best.dynamicFovPresetId = best.dynamicFov ? Config::View_aimbotDynamicFovPresetId() : -1;
                            best.predictedFovEntry = fovEvaluation.acceptedByPrediction;
                            best.fovEntryHorizonMs = fovEvaluation.acceptedByPrediction ? fovEntryHorizonMs : 0.0f;
                            best.currentFovScore = fovEvaluation.currentScoreDeg;
                            best.projectedFovScore = fovEvaluation.projectedScoreDeg;
                            best.motion = TargetingDetail::EstimateMotionState(entities[i], Vec2);
                            best.lockPolicy = lockPolicy;
                            best.weaponSpec = weaponSpec;
                            best.effectiveHitWindow = ResolveEffectiveHitWindow(
                                entities[i].HeroID,
                                best.boneId,
                                weaponSpec,
                                Config::View_hitbox(),
                                Config::kLegacyDefaultHitboxRadius);
                        }
                    }
                }
            }
        }

        // Ashe B.O.B targeting
        if (local_entity.HeroID == eHero::HERO_ASHE) {
            for (hpanddy hppack : hp_dy_entities) {
                if (hppack.entityid == 0x400000000002533) {
                    const bool currentLockedBob = activeLock.active &&
                        activeLock.entityKey == hppack.entityid;
                    const bool applyTrackingExitMargin =
                        trackingContinuityEnabled && currentLockedBob;
                    if (TargetingDetail::CandidateBlockedByMinLock(lockPolicy, activeLock, hppack.entityid, now))
                        continue;
                    const Vector3 bobPosition(hppack.POS.x, hppack.POS.y, hppack.POS.z);
                    const float bobDistance = TargetingDetail::CameraPosition().DistTo(bobPosition);
                    if (!TargetingDetail::DistancePassesAimFilter(
                            bobDistance,
                            applyTrackingExitMargin))
                        break;
                    const float effectiveFovDeg =
                        TrackingContinuityPolicy::EffectiveFovDeg(
                            Config::ResolveDynamicAimFovForDistance(
                                Config::View_Fov(),
                                bobDistance),
                            applyTrackingExitMargin);
                    float fovScoreDeg = 0.0f;
                    if (!TargetingDetail::IsWithinFovDeg(fovContext, bobPosition, effectiveFovDeg, &fovScoreDeg))
                        break;
                    if (currentLockedBob)
                        activeLockCandidateSelectable = true;
                    Vector2 Vec2 = CrossHair;
                    aimViewMatrix.WorldToScreen(bobPosition, &Vec2, Vector2(OW::WX, OW::WY));
                    const float lockAdjustedScore = TargetingDetail::ApplyRetargetHysteresis(fovScoreDeg, lockPolicy, activeLock, hppack.entityid);
                    if (lockAdjustedScore < origin) {
                        best = TargetCandidate{};
                        best.valid = true;
                        best.entityIndex = -1;
                        best.entityKey = hppack.entityid;
                        best.boneId = BONE_CHEST;
                        best.rawAimPoint = bobPosition;
                        best.predictedAimPoint = best.rawAimPoint;
                        best.aimPoint = best.rawAimPoint;
                        best.screenPoint = Vec2;
                        best.distance = bobDistance;
                        best.fovScore = fovScoreDeg;
                        best.effectiveFovDeg = effectiveFovDeg;
                        best.dynamicFov = Config::IsDynamicAimFovActive();
                        best.dynamicFovPresetId = best.dynamicFov ? Config::View_aimbotDynamicFovPresetId() : -1;
                        best.lockPolicy = lockPolicy;
                        best.weaponSpec = weaponSpec;
                        best.effectiveHitWindow = ResolveEffectiveHitWindow(
                            local_entity.HeroID,
                            best.boneId,
                            weaponSpec,
                            Config::View_hitbox(),
                            Config::kLegacyDefaultHitboxRadius);
                        origin = lockAdjustedScore;
                        selectedFovScoreDeg = fovScoreDeg;
                        targetFromBob = true;
                    }
                    break;
                }
            }
        }
        const bool holdAlternateDuringDropout = trackingContinuityEnabled &&
            activeLock.active &&
            activeLock.entityKey != 0 &&
            !activeLockCandidateSelectable &&
            best.valid &&
            best.entityKey != activeLock.entityKey &&
            TrackingContinuityPolicy::IsWithinTargetDropoutGrace(
                now,
                activeLock.lastSeenTick,
                trackingContinuityGraceMs);
        if (holdAlternateDuringDropout) {
            best = TargetCandidate{};
            TarGetIndex = -1;
            origin = TargetingDetail::kNoTargetScore;
            targetFromBob = false;
        }
        Config::Targetenemyi = best.valid ? best.entityIndex : -1;
        if (best.valid && best.entityIndex >= 0 &&
            static_cast<size_t>(best.entityIndex) < entities.size()) {
            Config::health = entities[static_cast<size_t>(best.entityIndex)].PlayerHealth;
        }
        if (best.valid) {
            best.effectiveFovDeg = Config::ResolveDynamicAimFovForDistance(Config::View_Fov(), best.distance);
            best.dynamicFov = Config::IsDynamicAimFovActive();
            best.dynamicFovPresetId = best.dynamicFov ? Config::View_aimbotDynamicFovPresetId() : -1;
            const Config::RuntimeDrawFovState drawState = Config::SnapshotRuntimeDrawFov();
            if (drawState.active &&
                drawState.slotKind == static_cast<int>(Config::FovRingSlotKind::Aim)) {
                Config::SetRuntimeDrawFov(best.effectiveFovDeg, drawState.slotKind, drawState.slotIndex);
            }
        }
        Config::aimbotEffectiveHitWindow = best.valid
            ? best.effectiveHitWindow
            : 0.0f;
        TargetingDetail::CommitTargetLockRuntime(
            best,
            origin,
            lockPolicy,
            now,
            trackingContinuityGraceMs);

        if (!best.valid) {
            Diagnostics::Aim("target.primary result none reason=no_selectable_target entities=%zu candidates=%zu fovDeg=%.6f targetIndex=%d",
                entities.size(),
                selectableCandidates,
                Config::View_Fov(),
                TarGetIndex);
        } else if (targetFromBob) {
            Diagnostics::Aim("target.primary result source=ashe_bob target=(%.9f,%.9f,%.9f) score=%.9f fovScoreDeg=%.9f",
                best.aimPoint.X,
                best.aimPoint.Y,
                best.aimPoint.Z,
                origin,
                selectedFovScoreDeg);
        } else if (TarGetIndex >= 0 && static_cast<size_t>(TarGetIndex) < entities.size()) {
            const c_entity& selected = entities[static_cast<size_t>(TarGetIndex)];
            Diagnostics::Aim("target.primary result index=%d target=(%.9f,%.9f,%.9f) score=%.9f fovScoreDeg=%.9f distance=%.3f bone=%d hitWindow=%.4f predictedEntry=%d entryHorizonMs=%.3f currentFov=%.3f projectedFov=%.3f health=%.3f hero=0x%llX address=0x%llX vis=%d team=%d",
                TarGetIndex,
                best.aimPoint.X,
                best.aimPoint.Y,
                best.aimPoint.Z,
                origin,
                selectedFovScoreDeg,
                best.distance,
                best.boneId,
                best.effectiveHitWindow,
                best.predictedFovEntry ? 1 : 0,
                best.fovEntryHorizonMs,
                best.currentFovScore,
                best.projectedFovScore,
                selected.PlayerHealth,
                static_cast<unsigned long long>(selected.HeroID),
                static_cast<unsigned long long>(selected.address),
                selected.Vis ? 1 : 0,
                selected.Team ? 1 : 0);
        }
        TargetingDetail::StoreLastTargetCandidate(best);
        return best;
    }

    inline TargetCandidate AcquireTriggerTarget(const ConsumptionFrame& frame, TriggerBoneMask rawBoneMask,
                                                bool predit = false,
                                                bool ignoreInvisible = Config::View_triggerbotIgnoreInvisible(),
                                                bool secondary = false) {
    OW::Config::ScopedConfigRead configRead;
        TargetCandidate best{};
        const TriggerBoneMask boneMask = NormalizeTriggerBoneMask(rawBoneMask);
        const auto& entities = frame.entities;
        const auto& localEntity = frame.local;
        const Matrix& view = frame.view;
        const TargetingDetail::FovRuntimeContext fovContext =
            TargetingDetail::SnapshotFovRuntimeContext();
        const WeaponSpec* weaponSpec = ResolveWeaponSpec(localEntity.HeroID, Config::View_aimbotAttack());
        const bool resolvedPrediction = ResolvePredictionEnabled(
            ClampPredictionOverride(Config::View_aimbotPredictionMode()),
            weaponSpec,
            predit);
        const ProjectileRuntimeSpec projectileSpec =
            TargetingDetail::ResolveProjectileRuntimeSpec(weaponSpec, localEntity, secondary);
        const float hitboxScale = secondary ? Config::hitbox2 : Config::View_hitbox();
        float bestNormalizedScore = (std::numeric_limits<float>::max)();

        if (fovContext.valid) {
            for (std::size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
                const c_entity& entity = entities[entityIndex];
                if (!TargetingDetail::IsSelectableCandidate(entity, Config::View_aimbotTeam(), localEntity))
                    continue;
                if (ignoreInvisible && !entity.Vis)
                    continue;
                if (entity.skill2act &&
                    entity.HeroID == eHero::HERO_GENJI &&
                    !entity.SameTeamAs(localEntity)) {
                    continue;
                }

                const Vector3 distanceAnchor = !TargetingDetail::IsZeroVector(entity.chest_pos)
                    ? entity.chest_pos
                    : entity.pos;
                const float initialDistance = fovContext.camera.DistTo(distanceAnchor);
                if (!TargetingDetail::DistancePassesAimFilter(initialDistance))
                    continue;

                for (std::size_t renderSlot = 0;
                     renderSlot < kTriggerBoneGroupForRenderSlot.size();
                     ++renderSlot) {
                    if (!TriggerBoneMaskIncludesRenderSlot(boneMask, renderSlot))
                        continue;

                    int boneId = Plexies20260609::kUnusedRenderSkeletonBone;
                    Vector3 rawAimPoint{};
                    if (!TargetingDetail::TryResolveSkeletonBonePoint(
                            entity,
                            renderSlot,
                            boneId,
                            rawAimPoint)) {
                        continue;
                    }

                    const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                        entity,
                        rawAimPoint,
                        projectileSpec,
                        resolvedPrediction,
                        secondary);
                    const Vector3 aimPoint = lead.finalAimPoint;
                    if (TargetingDetail::IsZeroVector(aimPoint) ||
                        !TargetingDetail::IsFiniteVector(aimPoint))
                        continue;

                    const float hitWindow = ResolveEffectiveHitWindow(
                        entity.HeroID,
                        boneId,
                        weaponSpec,
                        hitboxScale,
                        Config::kLegacyDefaultHitboxRadius);
                    const float distance = fovContext.camera.DistTo(aimPoint);
                    if (!std::isfinite(hitWindow) || hitWindow <= 0.0f ||
                        !std::isfinite(distance) || distance <= 0.0001f) {
                        continue;
                    }

                    const float angularWindowDeg = RAD2DEG(hitWindow / distance);
                    const float angularScoreDeg = TargetingDetail::FovScoreDeg(fovContext, aimPoint);
                    if (!std::isfinite(angularWindowDeg) || angularWindowDeg <= 0.0f ||
                        !std::isfinite(angularScoreDeg) ||
                        angularScoreDeg > angularWindowDeg + 0.0001f) {
                        continue;
                    }

                    const float normalizedScore = angularScoreDeg /
                        (std::max)(angularWindowDeg, 0.0001f);
                    if (best.valid && normalizedScore >= bestNormalizedScore)
                        continue;

                    bestNormalizedScore = normalizedScore;
                    best.valid = true;
                    best.entityIndex = static_cast<int>(entityIndex);
                    best.entityKey = entity.address ? entity.address : entity.LinkBase;
                    best.entitySnapshot = entity;
                    best.boneId = boneId;
                    best.rawAimPoint = rawAimPoint;
                    best.predictedAimPoint = aimPoint;
                    best.aimPoint = aimPoint;
                    best.screenPoint = TargetingDetail::CrosshairCenter();
                    view.WorldToScreen(
                        aimPoint,
                        &best.screenPoint,
                        Vector2(OW::WX, OW::WY));
                    best.distance = distance;
                    best.fovScore = angularScoreDeg;
                    best.effectiveFovDeg = secondary
                        ? Config::ResolveDynamicAimFovForDistance(Config::View_Fov2(), distance)
                        : Config::ResolveDynamicAimFovForDistance(Config::View_Fov(), distance);
                    best.dynamicFov = Config::IsDynamicAimFovActive();
                    best.dynamicFovPresetId = best.dynamicFov
                        ? Config::View_aimbotDynamicFovPresetId()
                        : -1;
                    best.effectiveHitWindow = hitWindow;
                    best.motion = TargetingDetail::EstimateMotionState(entity, best.screenPoint);
                    best.weaponSpec = weaponSpec;
                }
            }
        }

        Config::Targetenemyi = best.valid ? best.entityIndex : -1;
        if (best.valid)
            Config::health = best.entitySnapshot.PlayerHealth;
        if (secondary)
            Config::aimbotEffectiveHitWindow2 = best.valid ? best.effectiveHitWindow : 0.0f;
        else
            Config::aimbotEffectiveHitWindow = best.valid ? best.effectiveHitWindow : 0.0f;

        TargetingDetail::StoreLastTargetCandidate(best);
        Diagnostics::Aim(
            "target.trigger result=%s secondary=%d boneMask=0x%03X entityIndex=%d bone=%d scoreDeg=%.6f hitWindow=%.6f",
            best.valid ? "hit" : "none",
            secondary ? 1 : 0,
            static_cast<unsigned int>(boneMask),
            best.entityIndex,
            best.boneId,
            best.fovScore,
            best.effectiveHitWindow);
        return best;
    }

    inline Vector3 GetVector3(bool predit = false, bool ignoreInvisible = Config::View_aimbotIgnoreInvisible()) {
        const auto frame=consumerFrame?consumerFrame:SnapshotFrame();
        const TargetCandidate candidate = AcquireTarget(*frame, predit, ignoreInvisible);
        return candidate.valid ? candidate.aimPoint : Vector3{};
    }

    // =========================================================================
    // GetVector3 for track back
    // =========================================================================

    inline Vector3 GetVector3fortrackback(bool predit = false) {
        int TarGetIndex = -1;
        Vector3 target{};
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        if (local_entity.HeroID == eHero::HERO_HANJO) predit = true;
        const WeaponSpec* weaponSpec = ResolveWeaponSpec(local_entity.HeroID, Config::View_aimbotAttack());
        const bool resolvedPrediction = ResolvePredictionEnabled(
            ClampPredictionOverride(Config::View_aimbotPredictionMode()),
            weaponSpec,
            predit);
        const ProjectileRuntimeSpec projectileSpec =
            TargetingDetail::ResolveProjectileRuntimeSpec(weaponSpec, local_entity, false);
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        const Matrix aimViewMatrix = OW::SnapshotViewMatrix();
        Config::ScopedConfigRead configRead;
        const TargetingDetail::FovRuntimeContext fovContext = TargetingDetail::SnapshotFovRuntimeContext();
        auto entities = TargetingDetail::SnapshotEntities();
        float origin = 100000.f;
        if (entities.size() > 0) {
            Vector3 PreditPos, RootPos;
            for (size_t i = 0; i < entities.size(); i++) {
                const bool teamPass = TargetingDetail::TargetTeamMatches(
                    entities[i], Config::View_aimbotTeam(), local_entity);
                if (TargetingDetail::IsRuntimeTargetValid(entities[i], false) && teamPass) {
                    if (Config::View_Bone() == 1)       { PreditPos = entities[i].head_pos; RootPos = entities[i].head_pos; }
                    else if (Config::View_Bone() == 2)  { PreditPos = entities[i].neck_pos; RootPos = entities[i].neck_pos; }
                    else                         { PreditPos = entities[i].chest_pos; RootPos = entities[i].chest_pos; }
                    const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                        entities[i],
                        RootPos,
                        projectileSpec,
                        resolvedPrediction,
                        false);
                    PreditPos = lead.finalAimPoint;
                    const Vector3 fovPoint = lead.finalAimPoint;
                    const float distance = TargetingDetail::CameraPosition().DistTo(RootPos);
                    const float effectiveFovDeg =
                        Config::ResolveDynamicAimFovForDistance(Config::View_Fov(), distance);
                    float fovScoreDeg = 0.0f;
                    if (TargetingDetail::IsWithinFovDeg(fovContext, fovPoint, effectiveFovDeg, &fovScoreDeg)) {
                        float score;
                        if (Config::View_aimbotPriority() == 0) {
                            score = fovScoreDeg;
                        } else if (Config::View_aimbotPriority() == 1) {
                            score = entities[i].PlayerHealth;
                        } else {
                            score = distance;
                        }
                        if (score < origin) {
                            target = lead.finalAimPoint;
                            origin = score;
                            TarGetIndex = i;
                        }
                    }
                }
            }
            if (TarGetIndex != -1) {
                if ((Config::View_autobone() || Config::autobone2) && entities[TarGetIndex].HeroID != 0x16dd && entities[TarGetIndex].HeroID != 0x16ee) {
                    int closestBoneId = BONE_CHEST;
                    Vector3 closestRoot{};
                    if (TargetingDetail::TrySelectClosestCoreAimPoint(
                            entities[TarGetIndex],
                            fovContext,
                            aimViewMatrix,
                            CrossHair,
                            closestBoneId,
                            closestRoot)) {
                        RootPos = closestRoot;
                        PreditPos = RootPos;
                        target = RootPos;
                        if (resolvedPrediction) {
                            const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                                entities[TarGetIndex],
                                RootPos,
                                projectileSpec,
                                true,
                                false);
                            PreditPos = lead.finalAimPoint;
                            target = lead.finalAimPoint;
                        }
                    }
                }
                Config::health = entities[TarGetIndex].PlayerHealth;
            }
        }
        return target;
    }

    // =========================================================================
    // GetVector3 for Genji blade
    // =========================================================================

    inline Vector3 GetVector3forgenji(bool predit = false) {
        int TarGetIndex = -1;
        Vector3 target{};
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        const Vector3 camera = TargetingDetail::CameraPosition();
        float origin = 100000.f;
        if (entities.size() > 0) {
            for (size_t i = 0; i < entities.size(); i++) {
                if (entities[i].HeroID == 0x16dd || entities[i].HeroID == 0x16ee) continue;
                if (entities[i].HeroID == eHero::HERO_GENJI && entities[i].skill2act) continue;
                const bool teamPass = TargetingDetail::TargetTeamMatches(
                    entities[i], Config::View_aimbotTeam(), local_entity);
                if (TargetingDetail::IsRuntimeTargetValid(entities[i], false) && teamPass) {
                    Vector3 PreditPos;
                    if (!local_entity.skillcd1)
                        PreditPos = TargetingDetail::CachedSkeletonBoneRawPosition(entities[i],entities[i].GetSkel()[16]);
                    else
                        PreditPos = TargetingDetail::CachedSkeletonBoneRawPosition(entities[i],entities[i].GetSkel()[2]);
                    bool isBot = GameData::IsTrainingBotHeroId(entities[i].HeroID);
                    if (isBot) {
                        PreditPos = TargetingDetail::CachedSkeletonBoneRawPosition(entities[i],3);
                        if (!local_entity.skillcd1) PreditPos.Y -= 0.4f;
                    }
                    Vector3 RootPos = PreditPos;
                    if (entities[i].HeroID == eHero::HERO_WRECKINGBALL) {
                        PreditPos.Y -= 0.7f;
                        RootPos.Y -= 0.7f;
                    }
                    float score;
                    if (Config::View_aimbotPriority() == 0) {
                        score = camera.DistTo(PreditPos);
                        if (entities[i].PlayerHealth > 200.f) score = entities[i].PlayerHealth;
                        if (entities[i].HeroID == eHero::HERO_ZENYATTA && entities[i].ultimate == 0.f) score = 1000.f;
                    } else if (Config::View_aimbotPriority() == 1) {
                        score = entities[i].PlayerHealth;
                    } else {
                        score = camera.DistTo(PreditPos);
                    }
                    if (score < origin && score <= 6000.f) {
                        target = RootPos;
                        origin = score;
                        TarGetIndex = i;
                    }
                }
            }
            if (TarGetIndex != -1) {
                Config::health = entities[TarGetIndex].PlayerHealth;
                Config::Targetenemyi = TarGetIndex;
            }
        }
        return target;
    }

    // =========================================================================
    // GetVector3 for secondary aim
    // =========================================================================

    inline Vector3 GetVector3aim2(bool predit = false, bool ignoreInvisible = Config::View_aimbotIgnoreInvisible()) {
        int TarGetIndex = -1;
        Vector3 target{};
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        const Matrix aimViewMatrix = OW::SnapshotViewMatrix();
        Config::ScopedConfigRead configRead;
        const TargetingDetail::FovRuntimeContext fovContext = TargetingDetail::SnapshotFovRuntimeContext();
        const Vector3 camera = TargetingDetail::CameraPosition();
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        const WeaponSpec* weaponSpec = ResolveWeaponSpec(local_entity.HeroID, Config::View_aimbotAttack());
        const bool resolvedPrediction = ResolvePredictionEnabled(
            ClampPredictionOverride(Config::View_aimbotPredictionMode()),
            weaponSpec,
            predit);
        const ProjectileRuntimeSpec projectileSpec =
            TargetingDetail::ResolveProjectileRuntimeSpec(weaponSpec, local_entity, true);
        float origin = 100000.f;
        int selectedBoneId = AimBoneToSkeletonBoneId(Config::Bone2);
        bool selectedPredictedEntry = false;
        if (entities.size() > 0) {
            for (size_t i = 0; i < entities.size(); i++) {
                const bool teamPass = TargetingDetail::TargetTeamMatches(
                    entities[i], Config::View_aimbotTeam(), local_entity);
                if (TargetingDetail::IsRuntimeTargetValid(entities[i], false) && teamPass) {
                    if (ignoreInvisible && !entities[i].Vis)
                        continue;
                    const float initialDistance = camera.DistTo(entities[i].chest_pos);
                    if (!TargetingDetail::DistancePassesAimFilter(initialDistance))
                        continue;
                    Vector3 rootPosition{};
                    if (entities[i].Team) {
                        if (Config::Bone2 == 1)      rootPosition = entities[i].head_pos;
                        else if (Config::Bone2 == 2) rootPosition = entities[i].neck_pos;
                        else                         rootPosition = entities[i].chest_pos;
                    } else {
                        if (Config::View_Bone() == 1)       rootPosition = entities[i].head_pos;
                        else if (Config::View_Bone() == 2)  rootPosition = entities[i].neck_pos;
                        else                         rootPosition = entities[i].chest_pos;
                    }

                    int candidateBoneId = AimBoneToSkeletonBoneId(
                        entities[i].Team ? Config::Bone2 : Config::View_Bone());
                    if (Config::autobone2 &&
                        entities[i].HeroID != 0x16dd &&
                        entities[i].HeroID != 0x16ee) {
                        int closestBoneId = BONE_CHEST;
                        Vector3 closestRoot{};
                        if (TargetingDetail::TrySelectClosestCoreAimPoint(
                                entities[i],
                                fovContext,
                                aimViewMatrix,
                                CrossHair,
                                closestBoneId,
                                closestRoot)) {
                            candidateBoneId = closestBoneId;
                            rootPosition = closestRoot;
                        }
                    }

                    const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                        entities[i],
                        rootPosition,
                        projectileSpec,
                        resolvedPrediction,
                        true);
                    const float fovEntryHorizonMs =
                        Config::ClampFovEntryPredictionMs(Config::View_aimbotFovEntryPredictionMs());
                    const Vector3 fovEntryAimPoint = lead.finalAimPoint +
                        lead.targetVelocity * (fovEntryHorizonMs / 1000.0f);
                    FovGeometry::PredictedEntryEvaluation fovEvaluation{};
                    if (FovGeometry::EvaluateCandidateFovWithPredictedEntry(
                             fovContext,
                             rootPosition,
                             lead.finalAimPoint,
                             fovEntryAimPoint,
                             Config::View_Fov2(),
                             &Config::ResolveDynamicAimFovForDistance,
                             Config::View_aimbotPredictFovEntry(),
                             Config::ClampFovEntryMaxOutsideDeg(Config::View_aimbotFovEntryMaxOutsideDeg()),
                             fovEvaluation)) {
                        const float distance = fovEvaluation.distance;
                        const float fovScoreDeg = fovEvaluation.acceptedByPrediction
                            ? fovEvaluation.projectedScoreDeg
                            : fovEvaluation.currentScoreDeg;
                        if (!TargetingDetail::DistancePassesAimFilter(distance))
                            continue;
                        float score;
                        if (Config::View_aimbotPriority() == 0) {
                            score = fovScoreDeg;
                        } else if (Config::View_aimbotPriority() == 1) {
                            score = entities[i].PlayerHealth;
                        } else {
                            score = distance;
                        }
                        const bool preferCurrentInside = TarGetIndex != -1 &&
                            selectedPredictedEntry &&
                            !fovEvaluation.acceptedByPrediction;
                        const bool predictedEntryLosesToCurrentInside = TarGetIndex != -1 &&
                            !selectedPredictedEntry &&
                            fovEvaluation.acceptedByPrediction;
                        if (!predictedEntryLosesToCurrentInside &&
                            (preferCurrentInside || score < origin)) {
                            target = fovEvaluation.acceptedAimPoint;
                            origin = score;
                            TarGetIndex = i;
                            selectedBoneId = candidateBoneId;
                            selectedPredictedEntry = fovEvaluation.acceptedByPrediction;
                        }
                    }
                }
            }
            if (TarGetIndex != -1) {
                Config::health = entities[TarGetIndex].PlayerHealth;
                Config::Targetenemyi = TarGetIndex;
                Config::aimbotEffectiveHitWindow2 = ResolveEffectiveHitWindow(
                    entities[TarGetIndex].HeroID,
                    selectedBoneId,
                    weaponSpec,
                    Config::hitbox2,
                    Config::kLegacyDefaultHitboxRadius);
            }
        } else {
            Config::aimbotEffectiveHitWindow2 = 0.0f;
        }
        if (TarGetIndex == -1)
            Config::aimbotEffectiveHitWindow2 = 0.0f;
        return target;
    }

    // =========================================================================
    // GetVector3 for auto-FOV target
    // =========================================================================

    inline Vector3 GetVector3forfov(bool predit = false) {
        int TarGetIndex = -1;
        Vector3 target{};
        Config::ScopedConfigRead configRead;
        const TargetingDetail::FovRuntimeContext fovContext = TargetingDetail::SnapshotFovRuntimeContext();
        const Vector3 camera = TargetingDetail::CameraPosition();
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        float origin = 100000.f;
        if (entities.size() > 0) {
            Vector3 Vel, PreditPos, RootPos;
            for (size_t i = 0; i < entities.size(); i++) {
                const bool teamPass = TargetingDetail::TargetTeamMatches(
                    entities[i], Config::View_aimbotTeam(), local_entity);
                if (TargetingDetail::IsRuntimeTargetValid(entities[i], false) && teamPass) {
                    const float distance = camera.DistTo(entities[i].chest_pos);
                    if (!TargetingDetail::DistancePassesAimFilter(distance))
                        continue;
                    PreditPos = entities[i].head_pos;
                    RootPos = entities[i].head_pos;
                    float score;
                    if (Config::View_aimbotPriority() == 0) {
                        score = TargetingDetail::FovScoreDeg(fovContext, RootPos);
                    } else if (Config::View_aimbotPriority() == 1) {
                        score = entities[i].PlayerHealth;
                    } else {
                        score = distance;
                    }
                    if (score < origin) {
                        target = RootPos;
                        origin = score;
                        TarGetIndex = i;
                    }
                }
            }
            if (TarGetIndex != -1) {
                Config::health = entities[TarGetIndex].PlayerHealth;
                Config::Targetenemyifov = TarGetIndex;
            }
        }
        return target;
    }

} // namespace OW
