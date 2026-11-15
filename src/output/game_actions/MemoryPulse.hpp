#pragma once
#include <cstdint>

namespace V2::Output::GameActions {
// All three 155284 dumps use this input word; Pure 1106 confirms the melee bit.
// Evidence: 03_DOWNP/auto-melee-input-155284-20261111/static-evidence.json.
inline constexpr uint64_t kActionOffset = 0x121C;
inline constexpr uint32_t kActionMask = 0x800;
inline constexpr uint64_t kHoldMs = 50;

struct MemoryPulse {
    uint64_t address = 0;
    uint64_t startedMs = 0;
    uint64_t holdMs = kHoldMs;
    uint32_t mask = kActionMask;
    bool owned = false;
    bool releasing = false;

    template<class Read, class Write, class Current>
    bool Start(uint64_t destination, uint64_t now, Read read, Write write, Current current,
               uint32_t requestedMask = kActionMask, uint64_t requestedHoldMs = kHoldMs)
    {
        if (owned || !destination || !current())
            return false;
        uint32_t value = 0;
        if (!read(destination, &value, sizeof(value)) || !current() || (value & requestedMask))
            return false;
        address = destination;
        startedMs = now;
        holdMs = requestedHoldMs;
        mask = requestedMask;
        owned = true;
        value |= mask;
        // A failed transport can be partial; retain release-only cleanup until checked.
        releasing = !write(address, &value, sizeof(value));
        return !releasing;
    }

    template<class Read, class Write, class Current>
    bool Update(uint64_t now, bool keepHolding, Read read, Write write, Current current)
    {
        if (!owned)
            return true;
        if (!current()) {
            *this = {};
            return false;
        }
        releasing = releasing || !keepHolding || now - startedMs >= holdMs;
        uint32_t value = 0;
        if (!read(address, &value, sizeof(value))) {
            releasing = true;
            return false;
        }
        if (!current()) {
            *this = {};
            return false;
        }
        const uint32_t next = releasing ? value & ~mask : value | mask;
        if (next != value && !write(address, &next, sizeof(next))) {
            releasing = true;
            return false;
        }
        if (releasing)
            *this = {};
        return true;
    }
};

}
