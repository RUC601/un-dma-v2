#pragma once
#include "vision/ContourTypes.hpp"

namespace V2::Vision {
struct AsheAmmoSnapshot {
    bool known = false;
    int remaining = -1;
    std::uint64_t sequence = 0, epoch = 0;
    double arrivalMs = 0.0, ageMs = 0.0;
    double confidence = 0.0;
};

// Local hero identity is supplied by the runtime owner. A different hero,
// unavailable/stale frame, or ambiguous HUD reading always remains unknown.
AsheAmmoSnapshot ReadAsheAmmo(bool localIsAshe);
// Explicit guarded-sequence activation; reuses the UI capture service and
// existing settings without saving or creating a competing capture session.
bool RequestAsheAmmoCapture();

// Capture-worker only: reads this immutable frame's original pixels, never
// writes input or infers ammunition from clicks, elapsed time, or clip size.
void DetectAsheAmmo(Frame& frame);
}
