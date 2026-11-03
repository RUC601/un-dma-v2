#pragma once
#include "Game/ShuwuCore.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace OW::AimShuwu {
// Engineering units: 16 virtual error units per degree, never device counts or
// viewport pixels. Output stays angular; the existing output layer owns counts.
inline constexpr double kUnitsPerRadian = 16.0 * 180.0 / 3.14159265358979323846;
inline constexpr double kReferenceSeconds = 1.0 / 240.0;
struct State {
    koi::ShuwuState core{};
    uint64_t lastSample = 0;
    double lastStepRatio = 1;
    bool awaitingCommand = false;
};
struct Move { double pitch = 0, yaw = 0; };
inline Move Advance(State& state, double pitchError, double yawError, double dt,
                    double speed, uint64_t sample, koi::ShuwuConfig config) {
    if (!std::isfinite(pitchError) || !std::isfinite(yawError) ||
        !std::isfinite(dt) || dt <= 0 || !std::isfinite(speed)) {
        state = {}; return {};
    }
    state.awaitingCommand = false;
    if (sample && state.lastSample && sample == state.lastSample) return {};
    if (sample && state.lastSample) {
        if (sample < state.lastSample) state = {};
        else dt = double(sample - state.lastSample) / 1e6;
    }
    if (dt > .1) { state = {}; dt = kReferenceSeconds; }
    state.lastSample = sample;
    dt = (std::clamp)(dt, kReferenceSeconds * .25, kReferenceSeconds * 4);
    state.lastStepRatio = dt / kReferenceSeconds;
    yawError = std::remainder(yawError, 6.2831853071795864769);
    config.soft_lock_enabled = false; // no invented key binding
    koi::ShuwuInput input;
    input.error_x = yawError * kUnitsPerRadian;
    input.error_y = pitchError * kUnitsPerRadian;
    input.delta_ms = dt * 1000;
    input.speed_x = input.speed_y = .1 * (std::clamp)(speed, 0.0, 2.0);
    const auto raw = koi::calculateShuwuMove(state.core, input, config);
    const double limit = (std::clamp)(std::isfinite(config.max_move_per_frame)
        ? config.max_move_per_frame : 30.0, 1.0, 100.0);
    auto bounded = [&](double value, double error) {
        const double step = (std::clamp)(value, -limit, limit) * state.lastStepRatio / kUnitsPerRadian;
        return error >= 0 ? (std::clamp)(step, 0.0, error) : (std::clamp)(step, error, 0.0);
    };
    // Only accepted commands restore history, after all common scaling/limits.
    state.core.x.last_u = state.core.y.last_u = 0;
    state.awaitingCommand = true;
    return { bounded(raw.y, pitchError), bounded(raw.x, yawError) };
}
inline void CommitCommand(State& state, double pitch, double yaw) {
    if (!state.awaitingCommand) return;
    state.awaitingCommand = false;
    const double scale = kUnitsPerRadian / state.lastStepRatio;
    state.core.x.last_u = std::isfinite(yaw) ? yaw * scale : 0;
    state.core.y.last_u = std::isfinite(pitch) ? pitch * scale : 0;
}
} // namespace OW::AimShuwu
