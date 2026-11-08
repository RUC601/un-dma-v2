#pragma once
#include <cstdint>

namespace OW {
// ED's independent field deadlines, with a shorter interval for UN targeting.
struct VisibilitySample {
    static constexpr uint32_t intervalMs = 16, maxAgeMs = 50;
    uint32_t nextDue = 0, sampledAt = 0;
    bool attempted = false, valid = false, visible = false;
    bool Due(uint32_t now) const { return !attempted || int32_t(now - nextDue) >= 0; }
    void Commit(uint64_t decoded, bool readable, uint32_t started, uint32_t completed) {
        attempted = true;
        nextDue = completed + intervalMs;
        sampledAt = started;
        valid = readable && uint32_t(completed - started) <= maxAgeMs;
        visible = valid && decoded != 0;
    }
    bool Known(uint32_t now) const { return valid && uint32_t(now - sampledAt) <= maxAgeMs; }
    bool Visible(uint32_t now) const { return Known(now) && visible; }
};
}
