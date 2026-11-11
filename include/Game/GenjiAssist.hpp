#pragma once
#include <cmath>
#include <cstdint>

namespace OW::GenjiAssist {
inline constexpr uint64_t kDrawMs = 1000;
inline constexpr uint64_t kWindowMs = 7000;
inline constexpr uint64_t kDashRetryMs = 500;
inline constexpr uint64_t kSwingMs = 650;
inline constexpr float kSwingRange = 5.f;

// shortcut: blade-active is unverified; use Rigel's bounded Q window plus
// charge consumption until live mapping can detect early cancellation.
struct BladeWindow {
    bool armed = false, held = false, active = false, consumed = false;
    uint64_t started = 0;
    bool Advance(bool down, bool available, float charge, uint64_t now) {
        if (!available || !std::isfinite(charge)) { Cancel(); return false; }
        const bool pressed = down && !held;
        held = down;
        if (!down) armed = true;
        if (!active && pressed && armed && charge >= 100.f) {
            active = true; consumed = false; started = now; armed = false;
        }
        if (!active) return false;
        const auto elapsed = now - started;
        consumed |= charge < 100.f;
        if (elapsed >= kWindowMs || (!consumed && elapsed > 1500)) {
            active = false;
            return false;
        }
        return consumed && elapsed >= kDrawMs;
    }
    void Cancel() { *this = {}; }
};

// The production reader has no verified Genji recharge/reset value. A fresh
// inactive sample permits a bounded retry, never a claim that dash is ready.
inline bool MayTryDash(bool active, uint32_t sample, uint32_t now,
                       uint64_t lastTry, uint64_t clock) {
    return sample && uint32_t(now - sample) <= 250 && !active &&
        (!lastTry || clock - lastTry >= kDashRetryMs);
}
}
