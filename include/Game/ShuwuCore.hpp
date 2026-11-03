#ifndef KOI_SHUWU_HPP
#define KOI_SHUWU_HPP

#include <cmath>

// C++11, header-only. Extracted from ow_hex_fixed.lua v2.2.2.
// One state per independently controlled target/session; retain it between frames.
// No device output, allocation, UI, Lua runtime, or hidden static state.
// This is the Shuwu controller and its optional suppression/soft-lock layer.
// Shared script stages (target selection/prediction, anti-overshoot, trajectory
// shaping, final movement limits, rounding and device I/O) are NOT included.
// 使用方法：每个目标会话保留一个 ShuwuState，每帧传入当前误差和帧间隔。
// 调用 calculateShuwuMove 得到浮点 X/Y 位移；本函数不会实际移动鼠标。
// 松键、丢失/切换目标或修改配置时，将 input.reset 设为 true 调用一次，
// 然后恢复为 false。复位这一帧输出为零；请勿每帧重新创建 state。
namespace koi {

struct ShuwuConfig {
    double kp = 0.35, kd = 0.08;
    double predict_x = 0.40, predict_y = 0.10;
    double rate = 0.25, smooth = 0.20, deadzone = 1.0, reset_jump = 30.0;
    double integral_threshold = 50.0, integral_rate = 0.025;
    double kp_threshold = 1920.0, gain_decay = 0.10;
    double velocity_q = 0.01, velocity_r = 1.0;
    double integral_q = 0.50, integral_r = 1.0;
    double micro_error = 1.0, micro_diff = 0.10, ki_deadband = 1.0;
    double soft_a = 0.2666667, soft_b = 0.60, ki_scale = 1000.0;
    double max_move_per_frame = 30.0;

    bool operator==(const ShuwuConfig&) const = default;

    // Optional independent layer, applied AFTER the core's per-axis output limit,
    // just as in Lua. Multipliers >1 can exceed max_move_per_frame; the caller
    // must apply its final device/output limit if enabling such multipliers.
    bool suppression_enabled = true;
    double suppression_x = 1.0, suppression_y = 1.0;
    bool soft_lock_enabled = false;
    double soft_lock_x = 0.50, soft_lock_y = 0.30;
    double soft_lock_delay_ms = 0.0;
    // Lua's legacy "bandwidth" setting is unused by its actual formula and is
    // deliberately not exposed here. No invented bandwidth effect is added.
};

struct ShuwuAxisState {
    double u_filtered = 0.0, integral_term = 0.0, last_integral_term = 0.0;
    double last_error = 0.0, last_u = 0.0, kp_gain = 0.0, integral_gain = 0.0;
    double velocity_filter_x = 0.0, velocity_filter_p = 1.0;
    double integral_filter_x = 0.0, integral_filter_p = 1.0;
};

struct ShuwuState {
    ShuwuAxisState x, y;
    bool soft_lock_timing = false;
    double soft_lock_start_ms = 0.0, soft_lock_last_ms = 0.0;
};

struct ShuwuInput {
    // Signed CURRENT error in pixels, not absolute screen coordinates.
    double error_x = 0.0, error_y = 0.0;
    // 0.1 = the Lua script's natural 1x speed; this is not pixels/second.
    double speed_x = 0.1, speed_y = 0.1;
    double delta_ms = 1000.0 / 240.0;
    // Monotonic milliseconds, used only for optional soft-lock timing.
    double now_ms = 0.0;
    // Caller reads keys; this header never polls an input device.
    bool soft_lock_down = false, restore_down = false;
    // Reset emits zero for this call. Use on target loss/switch, hotkey release,
    // pause, algorithm/config changes, reconnect, or a new tracking session.
    bool reset = false;
};

struct ShuwuMove {
    double x = 0.0, y = 0.0;
};

// Example:
//   koi::ShuwuState state;       // outside the frame loop
//   koi::ShuwuConfig config;
//   koi::ShuwuInput input;
//   input.error_x = 20; input.error_y = -8;
//   const auto move = koi::calculateShuwuMove(state, input, config);
// Keep fractional outputs; perform integer rounding/residual accumulation later.
// On a jump > reset_jump an axis resets and emits zero for that frame, matching
// Lua, including the FIRST frame if its error is already above the threshold.
inline ShuwuMove calculateShuwuMove(
    ShuwuState& state, const ShuwuInput& input,
    const ShuwuConfig& config = ShuwuConfig())
{
    ShuwuMove result;
    if (input.reset || !std::isfinite(input.error_x) || !std::isfinite(input.error_y)
        || !std::isfinite(input.speed_x) || !std::isfinite(input.speed_y)) {
        state = ShuwuState();
        return result;
    }
    const auto clamp = [](double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    };
    const auto sane = [&](double v, double fallback, double lo, double hi) {
        return clamp(std::isfinite(v) ? v : fallback, lo, hi);
    };
    // Same accepted parameter ranges as the Lua UI/config loader.
    const ShuwuConfig defaults;
    ShuwuConfig p = config;
    p.kp = sane(p.kp, defaults.kp, 0, 2);
    p.kd = sane(p.kd, defaults.kd, 0, 1);
    p.predict_x = sane(p.predict_x, defaults.predict_x, -2, 3);
    p.predict_y = sane(p.predict_y, defaults.predict_y, -2, 3);
    p.rate = sane(p.rate, defaults.rate, 0.01, 1);
    p.smooth = sane(p.smooth, defaults.smooth, 0, 1);
    p.deadzone = sane(p.deadzone, defaults.deadzone, 0, 10);
    p.reset_jump = sane(p.reset_jump, defaults.reset_jump, 5, 160);
    p.integral_threshold = sane(p.integral_threshold, defaults.integral_threshold, 1, 320);
    p.integral_rate = sane(p.integral_rate, defaults.integral_rate, 0.001, 1);
    p.kp_threshold = sane(p.kp_threshold, defaults.kp_threshold, 1, 10000);
    p.gain_decay = sane(p.gain_decay, defaults.gain_decay, 0.001, 1);
    p.velocity_q = sane(p.velocity_q, defaults.velocity_q, 0.0001, 10);
    p.velocity_r = sane(p.velocity_r, defaults.velocity_r, 0.0001, 10);
    p.integral_q = sane(p.integral_q, defaults.integral_q, 0.0001, 10);
    p.integral_r = sane(p.integral_r, defaults.integral_r, 0.0001, 10);
    p.micro_error = sane(p.micro_error, defaults.micro_error, 0, 10);
    p.micro_diff = sane(p.micro_diff, defaults.micro_diff, 0, 5);
    p.ki_deadband = sane(p.ki_deadband, defaults.ki_deadband, 0, 10);
    p.soft_a = sane(p.soft_a, defaults.soft_a, 0, 2);
    p.soft_b = sane(p.soft_b, defaults.soft_b, 0, 2);
    p.ki_scale = sane(p.ki_scale, defaults.ki_scale, 10, 100000);
    p.max_move_per_frame = sane(p.max_move_per_frame, defaults.max_move_per_frame, 1, 100);

    const double base_ms = 1000.0 / 240.0;
    const double dt = clamp((std::isfinite(input.delta_ms) ? input.delta_ms : base_ms) / base_ms, 0.25, 4);
    const auto smooth = [&](double value, double reference) {
        reference = std::fmax(1.0, std::fabs(reference));
        if (p.smooth <= 0) return value;
        const double ratio = value / reference;
        const double squared = ratio * ratio;
        const double mapped = ratio * (1 + p.soft_a * squared) / (1 + p.soft_b * squared) * reference;
        return value + (mapped - value) * p.smooth;
    };
    const auto filter = [](double measurement, double q, double r, double& x, double& covariance) {
        const double predicted_p = std::fmax(0.0001, covariance) + q;
        const double gain = predicted_p / (predicted_p + r);
        x = x + gain * (measurement - x);
        covariance = std::fmax(0.0001, (1 - gain) * predicted_p);
        return x;
    };
    const auto axis = [&](ShuwuAxisState& s, double error, double speed, double predict) {
        speed = std::fmax(0.0, speed);
        if (std::fabs(error) <= p.deadzone || speed <= 0) {
            s.last_error = error;
            s.last_u = s.u_filtered = s.integral_term = 0;
            return 0.0; // Lua deliberately retains filter/gain history here.
        }
        const double delta = error - s.last_error;
        if (std::fabs(delta) > p.reset_jump) {
            s = ShuwuAxisState();
            s.last_error = error;
            return 0.0;
        }
        if (error * s.last_error < 0) {
            s.integral_gain *= 0.25;
            s.kp_gain *= 0.60;
            s.last_u = 0;
        }
        const double magnitude = std::fabs(error);
        if (magnitude < p.integral_threshold) {
            const double target = 1 - magnitude / p.integral_threshold;
            s.integral_gain += (target - s.integral_gain) * p.integral_rate * dt;
        } else {
            const double target = p.integral_threshold / magnitude;
            s.integral_gain += (target * s.integral_gain - s.integral_gain) * p.gain_decay * dt;
        }
        if (magnitude < p.kp_threshold) {
            const double target = 1 - magnitude / p.kp_threshold;
            s.kp_gain += (target - s.kp_gain) * p.rate * dt;
        } else {
            const double target = p.kp_threshold / magnitude;
            s.kp_gain += (target * s.kp_gain - s.kp_gain) * p.gain_decay * dt;
        }
        s.integral_gain = clamp(s.integral_gain, 0, 1);
        s.kp_gain = clamp(s.kp_gain, 0, 1);
        double velocity = filter(delta / dt + s.last_u, p.velocity_q, p.velocity_r,
                                 s.velocity_filter_x, s.velocity_filter_p);
        if (magnitude <= p.micro_error && std::fabs(delta) <= p.micro_diff)
            velocity = delta + s.last_u * 0.5;
        double ki_raw = std::fabs(velocity) <= p.ki_deadband ? 0.0 : velocity;
        ki_raw = ki_raw * predict * s.integral_gain;
        const double integral = filter(ki_raw, p.integral_q, p.integral_r,
                                       s.integral_filter_x, s.integral_filter_p);
        const double reference = std::fmax(1.0, magnitude * std::fmax(1.0, speed / 0.1));
        const double kp_term = smooth(p.kp * error, reference);
        const double kd_term = smooth(p.kd * delta / dt, reference);
        const double ki_term = smooth(integral, reference * std::fmax(0.1, p.ki_scale / 1000));
        double output = (kp_term + ki_term + kd_term) * s.kp_gain * clamp(speed / 0.1, 0, 6);
        output = clamp(output, -p.max_move_per_frame, p.max_move_per_frame);
        s.last_integral_term = s.integral_term;
        s.integral_term = ki_term;
        s.u_filtered = s.last_u = output;
        s.last_error = error;
        return output;
    };
    result.x = axis(state.x, input.error_x, input.speed_x, p.predict_x);
    result.y = axis(state.y, input.error_y, input.speed_y, p.predict_y);
    if (!std::isfinite(result.x) || !std::isfinite(result.y)) {
        state = ShuwuState();
        return ShuwuMove();
    }
    if (p.suppression_enabled) {
        result.x *= sane(p.suppression_x, defaults.suppression_x, 0, 3);
        result.y *= sane(p.suppression_y, defaults.suppression_y, 0, 3);
    }
    if (p.soft_lock_enabled && input.soft_lock_down && !input.restore_down
        && std::isfinite(input.now_ms)) {
        // Explicit flag also works at time zero; a clock reversal restarts delay.
        if (!state.soft_lock_timing || input.now_ms < state.soft_lock_last_ms) {
            state.soft_lock_start_ms = input.now_ms;
            state.soft_lock_timing = true;
        }
        state.soft_lock_last_ms = input.now_ms;
        if (input.now_ms - state.soft_lock_start_ms >= sane(p.soft_lock_delay_ms, 0, 0, 5000)) {
            result.x *= sane(p.soft_lock_x, defaults.soft_lock_x, 0, 3);
            result.y *= sane(p.soft_lock_y, defaults.soft_lock_y, 0, 3);
        }
    } else {
        state.soft_lock_timing = false;
        state.soft_lock_start_ms = state.soft_lock_last_ms = 0;
    }
    return result;
}

} // namespace koi
#endif // KOI_SHUWU_HPP
