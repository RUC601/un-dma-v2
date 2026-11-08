#pragma once
#include <cstdint>

namespace OW {
struct FieldDeadline {
    uint32_t next = 0, attemptedAt = 0;
    bool attempted = false;
    bool Due(uint32_t now, uint32_t interval) const {
        // Enabling a faster consumer must not wait for the previous slow deadline.
        return !attempted || int32_t(now - next) >= 0 || uint32_t(now - attemptedAt) >= interval;
    }
    void Schedule(uint32_t now, uint32_t interval) {
        attempted = true; attemptedAt = now; next = now + interval;
    }
};
}
