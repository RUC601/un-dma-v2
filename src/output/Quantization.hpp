#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "Game/RuntimeState.hpp"
#include "Game/CalibrationPolicy.hpp"
#include "output/OutputRuntime.hpp"
#include "Utils/Config.hpp"
#include "Utils/ProcessConnection.hpp"

// 每个 Behavior 会话持有自己的余量；数学换算不连接设备。
namespace OW {
    struct MouseMoveQuantizationState {
        float residualX = 0.0f;
        float residualY = 0.0f;
        std::uint64_t sessionGeneration = 0;
        std::uint64_t connectionEpoch = 0;
        std::uint64_t targetKey = 0;
        int lastForcedAxis = -1;
        bool initialized = false;
    };

    struct MouseMoveQuantizationOptions {
        MouseMoveQuantizationState* state = nullptr;
        Vector3 fallbackDirectionDelta{};
        bool forceNonZero = false;
    };

    // Integer relative device counts; residuals are fractional counts.
    struct MouseMoveQuantizationResult {
        int countX = 0;
        int countY = 0;
        int status = 0;
        bool forcedMinimumStep = false;
        bool dispatched = false;
    };

    inline void ResetMouseMoveQuantizationState(MouseMoveQuantizationState& state) {
        state = MouseMoveQuantizationState{};
    }

    inline bool HasMouseMoveQuantizationState(const MouseMoveQuantizationState& state) {
        return state.initialized ||
            state.residualX != 0.0f ||
            state.residualY != 0.0f;
    }

    inline bool PrepareMouseMoveQuantizationState(
        MouseMoveQuantizationState& state,
        std::uint64_t sessionGeneration,
        std::uint64_t connectionEpoch,
        std::uint64_t targetKey) {
        const bool changed =
            !state.initialized ||
            state.sessionGeneration != sessionGeneration ||
            state.connectionEpoch != connectionEpoch ||
            state.targetKey != targetKey;
        if (!changed)
            return false;

        ResetMouseMoveQuantizationState(state);
        state.sessionGeneration = sessionGeneration;
        state.connectionEpoch = connectionEpoch;
        state.targetKey = targetKey;
        state.initialized = true;
        return true;
    }

    inline MouseMoveQuantizationResult QuantizeMouseMoveCounts(
        float scaledX,
        float scaledY,
        float fallbackScaledX,
        float fallbackScaledY,
        MouseMoveQuantizationState& state,
        bool forceNonZero) {
        MouseMoveQuantizationResult result{};
        constexpr float kDirectionEpsilon = 0.0000001f;

        if (!std::isfinite(scaledX))
            scaledX = 0.0f;
        if (!std::isfinite(scaledY))
            scaledY = 0.0f;
        if (!std::isfinite(fallbackScaledX))
            fallbackScaledX = 0.0f;
        if (!std::isfinite(fallbackScaledY))
            fallbackScaledY = 0.0f;
        if (!std::isfinite(state.residualX))
            state.residualX = 0.0f;
        if (!std::isfinite(state.residualY))
            state.residualY = 0.0f;

        const float directionX = std::fabs(scaledX) > kDirectionEpsilon
            ? scaledX
            : fallbackScaledX;
        const float directionY = std::fabs(scaledY) > kDirectionEpsilon
            ? scaledY
            : fallbackScaledY;

        if (forceNonZero) {
            if (std::fabs(directionX) <= kDirectionEpsilon ||
                state.residualX * directionX < 0.0f) {
                state.residualX = 0.0f;
            }
            if (std::fabs(directionY) <= kDirectionEpsilon ||
                state.residualY * directionY < 0.0f) {
                state.residualY = 0.0f;
            }
        }

        state.residualX += scaledX;
        state.residualY += scaledY;
        result.countX = static_cast<int>(state.residualX);
        result.countY = static_cast<int>(state.residualY);
        state.residualX -= static_cast<float>(result.countX);
        state.residualY -= static_cast<float>(result.countY);

        if (!forceNonZero || result.countX != 0 || result.countY != 0)
            return result;

        // Pick the minimum-step axis from the current error, not from an older
        // residual.  The residual on the other axis is still retained below.
        const float candidateX = directionX;
        const float candidateY = directionY;

        const float absX = std::fabs(candidateX);
        const float absY = std::fabs(candidateY);
        if (absX <= kDirectionEpsilon && absY <= kDirectionEpsilon)
            return result;

        bool forceX = absX > absY;
        if (std::fabs(absX - absY) <= kDirectionEpsilon)
            forceX = state.lastForcedAxis != 0;
        if (forceX && absX <= kDirectionEpsilon)
            forceX = false;
        else if (!forceX && absY <= kDirectionEpsilon)
            forceX = true;

        if (forceX) {
            result.countX = candidateX > 0.0f ? 1 : -1;
            state.residualX = 0.0f;
            state.lastForcedAxis = 0;
        } else {
            result.countY = candidateY > 0.0f ? 1 : -1;
            state.residualY = 0.0f;
            state.lastForcedAxis = 1;
        }
        result.forcedMinimumStep = true;
        return result;
    }


}
