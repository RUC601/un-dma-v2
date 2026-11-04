#pragma once

#include <cstdint>

namespace OW::AimHoldWindow {

struct State {
    bool active = false;
    bool timedOut = false;
    std::uint32_t startedAt = 0;
    std::uint32_t timeoutMs = 0;
};

inline void Observe(State& state, bool sampleAvailable, bool held,
                    std::uint32_t now, std::uint32_t timeoutMs, bool canStart) {
    // Missing input evidence must never rearm a held key.
    if (!sampleAvailable)
        return;
    if (!held) {
        state = {};
    } else if (!state.active && canStart) {
        state = {true, false, now, timeoutMs};
    }
}

inline bool Expired(State& state, std::uint32_t now) {
    if (state.active && state.timeoutMs != 0 &&
        now - state.startedAt >= state.timeoutMs)
        state.timedOut = true;
    return state.timedOut;
}

} // namespace OW::AimHoldWindow
