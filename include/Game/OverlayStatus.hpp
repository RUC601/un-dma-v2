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

    inline constexpr float kReferenceWidth = 1920.0f;
    inline constexpr float kReferenceHeight = 1080.0f;
    inline constexpr float kMinResolutionScale = 0.5f;
    inline constexpr float kMaxResolutionScale = 3.0f;

    // Adaptive HUD/ESP layout scale relative to the 1920x1080 (1K) baseline.
    // Using min(w/1920, h/1080) maps 1080p -> 1.0, 2K (2560x1440) -> 1.3333,
    // ultrawide 2K (3440x1440) -> 1.3333, and 4K (3840x2160) -> 2.0.
    inline float ResolutionScale(float width, float height) {
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f)
            return 1.0f;
        const float scale = (std::min)(width / kReferenceWidth, height / kReferenceHeight);
        if (!std::isfinite(scale) || scale <= 0.0f)
            return 1.0f;
        return std::clamp(scale, kMinResolutionScale, kMaxResolutionScale);
    }

    inline constexpr Layout Custom{72, 24, 60, 100, 14, 164, 110, 76, 104, 77};
    inline constexpr Layout Compact{54, 18, 0, 54, 5, 76, 76, 58, 0, 57};
    inline constexpr Layout GetLayout(int style) { return style == 0 ? Compact : Custom; }
    inline float Position(float fraction, float canvas, float extent) {
        return std::clamp(fraction,0.0f,1.0f) * (std::max)(0.0f,canvas-extent);
    }

    inline float PositionInset(float fraction, float canvas, float extent, float margin) {
        const float safeMargin = (std::max)(0.0f, margin);
        const float usable = (std::max)(0.0f, canvas - safeMargin * 2.0f);
        return safeMargin + Position(fraction, usable, extent);
    }

    struct HeroRosterPlacement {
        float uiScale = 1.0f;
        float effectiveScale = 1.0f;
        int rows = 1;
        int columns = 0;
        float usedWidth = 0.0f;
        float usedHeight = 0.0f;
        float originX = 0.0f;
        float originY = 0.0f;
    };

    inline HeroRosterPlacement ComputeHeroRosterPlacement(
        int style,
        int visibleCount,
        float canvasWidth,
        float canvasHeight,
        float userScale,
        float posX,
        float posY) {
        HeroRosterPlacement result{};
        const Layout layout = GetLayout(style);
        result.uiScale = ResolutionScale(canvasWidth, canvasHeight);
        const float safeUserScale = (std::isfinite(userScale) && userScale > 0.0f) ? userScale : 1.0f;
        result.effectiveScale = safeUserScale * result.uiScale;
        if (visibleCount <= 0)
            return result;
        const float cellHeight = (std::max)(1.0f, layout.height * result.effectiveScale);
        result.rows = (std::max)(1, static_cast<int>(canvasHeight / cellHeight));
        result.columns = (visibleCount + result.rows - 1) / result.rows;
        result.usedHeight = static_cast<float>((std::min)(visibleCount, result.rows)) * layout.height * result.effectiveScale;
        result.usedWidth = (static_cast<float>(result.columns) * (layout.width + 16.0f) - 16.0f) * result.effectiveScale;
        result.originX = Position(posX, canvasWidth, result.usedWidth);
        result.originY = Position(posY, canvasHeight, result.usedHeight);
        return result;
    }

    struct OverlayInfoPlacement {
        float uiScale = 1.0f;
        float effectiveScale = 1.0f;
        float margin = 10.0f;
        float width = 222.0f;
        float height = 196.0f;
        float originX = 10.0f;
        float originY = 10.0f;
        float titleFont = 13.5f;
        float bodyFont = 12.0f;
        float lineHeight = 14.0f;
        float titleGap = 18.0f;
    };

    inline OverlayInfoPlacement ComputeOverlayInfoPlacement(
        float canvasWidth,
        float canvasHeight,
        float userScale,
        float posX,
        float posY) {
        OverlayInfoPlacement result{};
        result.uiScale = ResolutionScale(canvasWidth, canvasHeight);
        const float safeUserScale = (std::isfinite(userScale) && userScale > 0.0f) ? userScale : 1.0f;
        result.effectiveScale = safeUserScale * result.uiScale;
        result.margin = 10.0f * result.uiScale;
        result.width = 222.0f * result.effectiveScale;
        result.height = 196.0f * result.effectiveScale;
        result.originX = PositionInset(posX, canvasWidth, result.width, result.margin);
        result.originY = PositionInset(posY, canvasHeight, result.height, result.margin);
        result.titleFont = 13.5f * result.effectiveScale;
        result.bodyFont = 12.0f * result.effectiveScale;
        result.lineHeight = 14.0f * result.effectiveScale;
        result.titleGap = 18.0f * result.effectiveScale;
        return result;
    }

    struct RadarPlacement {
        float uiScale = 1.0f;
        float radius = 164.0f;
        float markerSize = 16.0f;
        float centerX = 164.0f;
        float centerY = 164.0f;
    };

    inline RadarPlacement ComputeRadarPlacement(
        float canvasWidth,
        float canvasHeight,
        float baseRadius,
        float baseMarkerSize,
        float posX,
        float posY) {
        RadarPlacement result{};
        result.uiScale = ResolutionScale(canvasWidth, canvasHeight);
        const float maxRadius = (std::max)(1.0f, (std::min)(canvasWidth, canvasHeight) * 0.5f - 2.0f * result.uiScale);
        const float safeRadius = (std::isfinite(baseRadius) && baseRadius > 0.0f) ? baseRadius : 164.0f;
        result.radius = (std::min)(safeRadius * result.uiScale, maxRadius);
        const float safeMarker = (std::isfinite(baseMarkerSize) && baseMarkerSize > 0.0f) ? baseMarkerSize : 16.0f;
        result.markerSize = safeMarker * result.uiScale;
        result.centerX = result.radius + Position(posX, canvasWidth, 2.0f * result.radius);
        result.centerY = result.radius + Position(posY, canvasHeight, 2.0f * result.radius);
        return result;
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
