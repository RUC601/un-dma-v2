#pragma once
#include "Smoothing.hpp"
#include "Game/AimHybridController.hpp"
#include <chrono>
#include <vector>
namespace OW {
    namespace AimSmoothingDetail {

        constexpr float kDefaultDeltaTime = 1.0f / 144.0f;
        constexpr float kMinDeltaTime = 1.0f / 1000.0f;
        constexpr float kMaxDeltaTime = 0.1f;
        constexpr float kRetargetAngleThreshold = 2.0f;

        struct PIDState {
            Vector3 integral{};
            Vector3 previousError{};
            Vector3 filteredDerivative{};
            Vector3 lastTarget{};
            bool initialized = false;
        };

        struct BezierState {
            std::vector<Vector3> controlPoints{};
            Vector3 lastTarget{};
            float t = 0.0f;
            bool initialized = false;
        };

        struct OvershootState {
            Vector3 offset{};
            Vector3 lastTarget{};
            bool initialized = false;
        };


        AimHybrid::State& GetHybridState();
        PIDState& GetPIDState();
        BezierState& GetBezierState();
        OvershootState& GetOvershootState();
        std::chrono::steady_clock::time_point& LastSmoothingTick();
        float ComputeDeltaTime();
    }

}
