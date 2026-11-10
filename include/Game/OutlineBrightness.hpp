#pragma once
#include "Game/Offsets.hpp"
#include <bit>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <mutex>

namespace OW::OutlineBrightness {
// 155284: constructors, last-item getters and alpha multiply paths in
// 03_DOWNP/outline-alpha-20261111. World/Nexon are statically verified only.
constexpr uint64_t VtableRva(offset::RuntimeProfile profile) {
    switch (profile) {
    case offset::RuntimeProfile::Cn: return 0x35A1220;
    case offset::RuntimeProfile::World155284: return 0x2FCB2C0;
    case offset::RuntimeProfile::Nexon155284: return 0x358B290;
    default: return 0;
    }
}
inline bool Valid(float value) { return std::isfinite(value) && value >= 0.f; }
struct Command { float value = 2.f; uint64_t revision = 0; bool enabled = false; };
inline std::mutex commandMutex;
inline Command command;
inline Command Snapshot() { std::lock_guard lock(commandMutex); return command; }
inline bool Submit(float value) {
    if (!Valid(value)) return false;
    std::lock_guard lock(commandMutex);
    command.value = value;
    ++command.revision;
    command.enabled = true;
    return true;
}
inline void Disable() { std::lock_guard lock(commandMutex); command.enabled = false; }
inline std::atomic<uint32_t> AppliedCount{0}, FailureCount{0}, RestoreCount{0};
enum class Result { Idle, Applied, Changed, ReadFailed, WriteFailed, RestorePending };
inline bool Pointer(uint64_t p) { return p >= 0x10000 && p <= 0x00007FFFFFFFF000ULL; }
struct Target {
    uint64_t component = 0, data = 0, value = 0;
    int32_t count = 0;
    bool operator==(const Target&) const = default;
};
template<class Read>
bool Resolve(uint64_t component, uint64_t base, offset::RuntimeProfile profile, Read read, Target& target) {
    const auto vtableRva = VtableRva(profile);
    uint64_t vtable = 0;
    uint8_t type = 0;
    struct List { uint64_t data; int32_t count; } before{}, after{};
    if (!vtableRva || !Pointer(base) || !Pointer(base + vtableRva) || !Pointer(component) ||
        !read(component, &vtable, 8) || vtable != base + vtableRva ||
        !read(component + 0x10, &type, 1) || type != 0x5A ||
        !read(component + 0x1C0, &before, 12)) return false;
    if (!Pointer(before.data) || before.data % 4 || before.count <= 0 || before.count > 4096) return false;
    const uint64_t value = before.data + uint64_t(before.count - 1) * 20 + 16;
    if (!Pointer(value) || !read(component + 0x1C0, &after, 12) ||
        before.data != after.data || before.count != after.count) return false;
    target = {component, before.data, value, before.count};
    return true;
}
struct State {
    Target target{};
    uint32_t original = 0, written = 0, nextAttempt = 0;
    uint64_t appliedRevision = 0, triedRevision = 0;
    bool owned = false;
    Result result = Result::Idle;
    bool Due(uint32_t now, bool enabled, uint64_t revision) const {
        return (enabled || owned) && (triedRevision != revision || int32_t(now - nextAttempt) >= 0);
    }
    template<class Read, class Write, class Current>
    bool Restore(Read read, Write write, Current current) {
        if (!owned) return true;
        uint32_t value = 0;
        if (!current() || !read(target.value, &value, 4)) { result = Result::RestorePending; return false; }
        // Restore only our own last write; a game replacement has a new owner.
        if (value != written) { owned = false; result = Result::Changed; return true; }
        if (!current() || !write(target.value, &original, 4) || !current() ||
            !read(target.value, &value, 4) || value != original) {
            result = Result::RestorePending;
            return false;
        }
        owned = false;
        result = Result::Idle;
        return true;
    }
    template<class Read, class Write, class Current>
    bool Apply(Target next, float value, uint64_t revision, Read read, Write write, Current current) {
        if (!Valid(value) || !revision || !current()) return false;
        // A relocated last item is a new allocation; never restore the retired address.
        if (target != next) { target = next; owned = false; appliedRevision = 0; }
        uint32_t previous = 0;
        if (!current() || !read(target.value, &previous, 4) || !Valid(std::bit_cast<float>(previous))) {
            result = Result::ReadFailed;
            return false;
        }
        if (appliedRevision == revision) {
            if (owned && previous != written) { owned = false; result = Result::Changed; }
            return result == Result::Applied;
        }
        if (!owned || previous != written) original = previous;
        const uint32_t bits = std::bit_cast<uint32_t>(value);
        if (!current()) return false;
        written = bits;
        owned = true; // A backend failure can still leave a completed remote write.
        if (!write(target.value, &bits, 4) || !current() ||
            !read(target.value, &previous, 4) || previous != bits) {
            result = Result::WriteFailed;
            return false;
        }
        appliedRevision = revision;
        result = Result::Applied;
        return true;
    }
};
}
