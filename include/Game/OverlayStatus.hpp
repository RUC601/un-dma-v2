#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <chrono>

namespace OW::OverlayStatus {
    inline double MonotonicSeconds() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    struct Layout {
        float avatar, skill, ultimate, barWidth, barHeight, width, height;
        float skillX, ultimateX, healthY;
    };

    inline constexpr Layout Custom{72, 24, 60, 100, 14, 164, 110, 76, 104, 77};
    inline constexpr Layout Compact{54, 18, 0, 54, 5, 76, 76, 58, 0, 57};
    inline constexpr Layout GetLayout(int style) { return style == 0 ? Compact : Custom; }
    inline float Position(float fraction, float canvas, float extent) {
        return std::clamp(fraction,0.0f,1.0f) * (std::max)(0.0f,canvas-extent);
    }

    struct CooldownSample {
        float seconds = 0;
        uint32_t tick = 0;
        bool valid = false;
        bool readySentinel = false;
        bool estimated = false;
        double deadline = 0;
        float Remaining(double now = MonotonicSeconds()) const {
            return estimated ? float((std::max)(0.0, deadline - now)) : seconds;
        }
        bool Fresh(uint32_t now) const {
            return valid && tick != 0 && uint32_t(now - tick) <= 1000 &&
                std::isfinite(seconds) && seconds >= 0;
        }
    };

    // Render-only smoothing. Never feeds smoothed charge back to aiming or skill guards.
    struct ProgressAnimation {
        float value = 0;
        double lastTime = 0;
        bool initialized = false;
        float Update(float target, double now, bool reset = false) {
            target = std::clamp(target, 0.0f, 100.0f);
            if (!initialized || reset || target < value - 5.0f || now < lastTime) {
                value = target;
                initialized = true;
            } else {
                const double dt = (std::min)(now - lastTime, .25);
                value += (target - value) * float(-std::expm1(-dt / .08));
                if (std::fabs(target - value) < .005f) value = target;
            }
            lastTime = now;
            return value;
        }
    };

    // Missing/stale samples are not zero charge. Body replacement does not spend an ultimate.
    struct UltimatePresentation {
        std::array<uint64_t, 3> identity{}; // hero / connection / scene; participant is the map key
        ProgressAnimation arc;
        float percent = 0;
        bool known = false;
        float Update(const std::array<uint64_t, 3>& nextIdentity, bool readable, float target, double now) {
            if (identity != nextIdentity) {
                *this = {};
                identity = nextIdentity;
            }
            if (readable && std::isfinite(target) && target >= 0 && target <= 100) {
                percent = target;
                known = true;
            }
            return known ? arc.Update(percent, now) : 0;
        }
    };

    inline float Positive(float value) {
        return std::isfinite(value) && value > 0 ? value : 0;
    }

    struct Health {
        float total = 0;
        std::array<float, 3> fractions{};
    };

    inline Health ComposeHealth(float life, float armor, float shield, float maximum, float total) {
        const std::array<float, 3> parts{Positive(life), Positive(armor), Positive(shield)};
        const float sum = parts[0] + parts[1] + parts[2];
        Health result;
        result.total = Positive(total) > 0 ? Positive(total) : sum;
        const float denominator = Positive(maximum) > 0 ? Positive(maximum) : sum;
        if (denominator <= 0) return result;
        float remaining = 1;
        for (size_t i = 0; i < parts.size(); ++i) {
            result.fractions[i] = std::clamp(parts[i] / denominator, 0.0f, remaining);
            remaining -= result.fractions[i];
        }
        return result;
    }
}
