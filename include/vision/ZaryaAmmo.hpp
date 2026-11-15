#pragma once
#include "vision/ContourTypes.hpp"

namespace V2::Vision {
struct ZaryaAmmoSnapshot {
    bool known = false;
    int remaining = -1;
    std::uint64_t sequence = 0, epoch = 0;
    double arrivalMs = 0.0, ageMs = 0.0;
    double confidence = 0.0;
};

// Only the current local Zarya may consume this frame's cannon/100 HUD reading.
// Missing, ambiguous, stale, or foreign-hero evidence remains unknown.
ZaryaAmmoSnapshot ReadZaryaAmmo(bool localIsZarya);
// Reuses the existing capture service; callers request once per activation.
bool RequestZaryaAmmoCapture();
// Capture-worker/fixture entrypoint. It never infers ammunition from input.
void DetectZaryaAmmo(Frame& frame);
}
