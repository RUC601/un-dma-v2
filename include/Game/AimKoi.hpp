#pragma once

#include "Game/AimSmoothingTiming.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace OW::AimKoi {

// Adapted from KOI's primary proportional controller, whose recovered integral
// weight is zero and previous-error field is not consumed. This is angular UN
// motion, not a replay of KOI's pixel coordinates or original thread timing.
// At speed 100%, remove 10% of the error per UN reference step (1/144 second).
// Do not import KOI's duplicate centre subtraction, shared-axis overwrite or
// unbounded random gain walk. The existing speed control scales this gain.
inline constexpr double kBaseGain = 0.10;
inline constexpr double kReferenceSeconds = AimSmoothingTiming::kReferenceDeltaTimeSeconds;
inline constexpr double kMaximumGapSeconds = 0.10;

struct State {
    std::uint64_t lastSample = 0;
};

struct Move {
    double pitch = 0;
    double yaw = 0;
};

inline Move Advance(State& state, double pitchError, double yawError,
                    double deltaSeconds, double speed, std::uint64_t sample)
{
    if (!std::isfinite(pitchError) || !std::isfinite(yawError) ||
        !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0 ||
        !std::isfinite(speed) || speed <= 0.0) {
        state = {};
        return {};
    }

    // Target freshness is checked by the targeting consumer. Here a timestamp
    // may be consumed only once; delayed/reordered samples must not replay moves.
    if (sample && state.lastSample) {
        if (sample <= state.lastSample)
            return {};
        deltaSeconds = static_cast<double>(sample - state.lastSample) / 1e6;
    }
    state.lastSample = sample;
    // A resumed stream starts gently instead of spending its whole missing
    // interval on one command. With no timestamp, use the caller's elapsed time.
    if (deltaSeconds > kMaximumGapSeconds)
        deltaSeconds = kReferenceSeconds;

    const double gain = std::clamp(kBaseGain * speed, 0.0, 1.0);
    const double factor = gain >= 1.0 ? 1.0 :
        -std::expm1(std::log1p(-gain) * deltaSeconds / kReferenceSeconds);
    yawError = std::remainder(yawError, 6.2831853071795864769);
    // There is no retained velocity/integral or predicted command to commit:
    // every new sample corrects only the current measured error. Common output
    // scaling, quantization and device acceptance remain owned by UN.
    return {pitchError * factor, yawError * factor};
}

} // namespace OW::AimKoi
