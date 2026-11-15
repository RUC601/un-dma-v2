#pragma once
#include <cmath>
#include <cstdint>

namespace OW::Contour {

// A colour or the game's aggregate Vis bit must never identify a target.
struct Identity {
    std::uint64_t entity = 0, body = 0, scene = 0, connection = 0;
    bool operator==(const Identity&) const = default;
    bool Valid() const { return entity && body && scene && connection; }
};

struct Evidence {
    Identity identity{};
    std::uint64_t epoch = 0, revision = 0, sequence = 0;
    double capturedMs = 0;
    bool fresh = false, exposed = false, touching = false, inside = false;
};

struct ContactState {
    Identity identity{};
    std::uint64_t epoch = 0, revision = 0, sequence = 0, touchSequence = 0, consumedSequence = 0;
    double lastExposureMs = 0, touchedMs = 0;
    bool touched = false, ready = false;

    void Reset() { *this = {}; }

    // Only a new capture can advance contact confirmation or renew exposure.
    // Missing evidence suspends fire immediately, but a bounded dropout does
    // not turn a white/red/yellow transition into a new contact or target.
    bool Observe(const Evidence& e, double nowMs, int delayMs, int graceMs) {
        ready = false;
        if (!e.identity.Valid() || !std::isfinite(nowMs)) { Reset(); return false; }
        if (identity != e.identity || epoch != e.epoch || revision != e.revision) {
            Reset(); identity = e.identity; epoch = e.epoch; revision = e.revision;
        }
        const bool currentCapture=e.fresh && e.sequence>=sequence && std::isfinite(e.capturedMs) &&
            e.capturedMs<=nowMs && e.capturedMs>=lastExposureMs;
        // Fresh frames can arrive with a stable processing age. Compare their
        // capture timestamps, otherwise that age falsely looks like a dropout.
        const double continuityMs=currentCapture?e.capturedMs:nowMs;
        if (touched && (continuityMs < lastExposureMs || continuityMs - lastExposureMs > graceMs)) {
            touched = false; touchSequence = 0;
        }
        if (!e.fresh || !e.exposed || !e.sequence || !std::isfinite(e.capturedMs) ||
            e.capturedMs > nowMs || e.sequence < sequence) return false;
        if (e.sequence == sequence) {
            // A caller can query a confirmed frame twice before submission,
            // but wall time alone cannot turn an unconfirmed frame into fire.
            ready = touched && (e.touching || e.inside) && sequence > touchSequence &&
                sequence > consumedSequence && e.capturedMs - touchedMs >= delayMs;
            return ready;
        }
        sequence = e.sequence;
        if (lastExposureMs && e.capturedMs < lastExposureMs) { Reset(); return false; }
        lastExposureMs = e.capturedMs;
        if (!e.touching && !e.inside) { touched = false; touchSequence = 0; return false; }
        if (!touched) {
            // Interior alone is not a new edge-contact event.
            if (!e.touching) return false;
            touched = true; touchedMs = e.capturedMs; touchSequence = sequence;
        }
        ready = sequence > touchSequence && sequence > consumedSequence &&
            e.capturedMs - touchedMs >= delayMs;
        return ready;
    }

    void Submitted() { consumedSequence = sequence; ready = false; }
};
} // namespace OW::Contour
