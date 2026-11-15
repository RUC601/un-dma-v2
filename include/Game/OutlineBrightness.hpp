#pragma once
#include "Game/NativeOutlineSetting.hpp"
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace OW::OutlineBrightness {
// Commands are percentages; native storage is float32 percent / 100.
// Values above 100 are intentional; 8888% affects newly created outlines.
inline bool Valid(float percent) {
    return std::isfinite(percent) && percent >= 0.f && std::isfinite(percent / 100.f);
}
struct Command { float value = 100.f; uint64_t revision = 0; bool enabled = false; };
inline std::mutex commandMutex;
inline Command command;
inline Command Snapshot() { std::lock_guard lock(commandMutex); return command; }
inline bool Submit(float percent) {
    if (!Valid(percent)) return false;
    std::lock_guard lock(commandMutex);
    command.value = percent;
    if (++command.revision == 0) ++command.revision;
    command.enabled = true;
    return true;
}
inline void Disable() { std::lock_guard lock(commandMutex); command.enabled = false; }
inline std::atomic<uint32_t> AppliedCount{0}, FailureCount{0}, RestoreCount{0};
inline std::atomic<uint64_t> ShutdownRestoreRequest{0}, ShutdownRestoreAcknowledged{0};
enum class Result {
    Idle, Applied, Restored, Changed, InvalidValue, UnsupportedProfile,
    IdentityMismatch, SignatureMismatch, ReadFailed, SettingMissing,
    AmbiguousSetting, InvalidTarget, TargetChanged, WriteFailed,
    WriteReadbackFailed, WriteMismatch, RestorePending, RestoreReadFailed,
    RestoreWriteFailed, RestoreReadbackFailed, RestoreMismatch
};
inline std::atomic<Result> LastResult{Result::Idle};
inline constexpr const char* ResultText(Result result) {
    switch (result) {
    case Result::Idle: return "idle";
    case Result::Applied: return "applied";
    case Result::Restored: return "restored";
    case Result::Changed: return "game value changed; ownership released";
    case Result::InvalidValue: return "invalid percentage";
    case Result::UnsupportedProfile: return "unsupported native setting profile";
    case Result::IdentityMismatch: return "native setting image identity mismatch";
    case Result::SignatureMismatch: return "native setting code/signature mismatch";
    case Result::ReadFailed: return "native setting read failed";
    case Result::SettingMissing: return "native setting key absent";
    case Result::AmbiguousSetting: return "duplicate native setting key";
    case Result::InvalidTarget: return "invalid native setting structure";
    case Result::TargetChanged: return "native setting target/session changed";
    case Result::WriteFailed: return "write reported failure; completion unknown";
    case Result::WriteReadbackFailed: return "write completed; readback failed";
    case Result::WriteMismatch: return "write readback differs";
    case Result::RestorePending: return "restore awaits the same target/session";
    case Result::RestoreReadFailed: return "restore pre-read failed";
    case Result::RestoreWriteFailed: return "restore write reported failure";
    case Result::RestoreReadbackFailed: return "restore readback failed";
    case Result::RestoreMismatch: return "restore readback differs";
    }
    return "unknown";
}
inline constexpr bool IsFailure(Result result) {
    return result != Result::Idle && result != Result::Applied && result != Result::Restored && result != Result::Changed;
}
using Target = NativeOutlineSetting::Target;
inline bool Pointer(uint64_t p) { return NativeOutlineSetting::Pointer(p); }
inline Result ToResult(NativeOutlineSetting::Status status) {
    using S = NativeOutlineSetting::Status;
    switch (status) {
    case S::Ready: return Result::Idle;
    case S::UnsupportedProfile: return Result::UnsupportedProfile;
    case S::IdentityMismatch: return Result::IdentityMismatch;
    case S::SignatureMismatch: return Result::SignatureMismatch;
    case S::ReadFailed: return Result::ReadFailed;
    case S::SettingMissing: return Result::SettingMissing;
    case S::AmbiguousSetting: return Result::AmbiguousSetting;
    case S::InvalidTarget: return Result::InvalidTarget;
    case S::TargetChanged: return Result::TargetChanged;
    }
    return Result::InvalidTarget;
}
template<class Read>
bool Resolve(uint64_t base, offset::RuntimeProfile profile, Read read, Target& target, Result* failure = nullptr) {
    const auto status = NativeOutlineSetting::Resolve(base, profile, read, target);
    if (failure) *failure = ToResult(status);
    return status == NativeOutlineSetting::Status::Ready;
}
struct State {
    Target target{};
    uint32_t original = 0, written = 0, nextAttempt = 0;
    uint64_t appliedRevision = 0, triedRevision = 0, writeRevision = 0;
    bool owned = false, restoreAttempted = false;
    Result result = Result::Idle;
    void SetResult(Result value) { result = value; LastResult.store(value); }
    // Scheduler owns connection/process identity. Call on disconnect, process
    // generation or profile change; never restore through a different link.
    // Preserve the consumed revision so a stale command cannot rewrite.
    void Abandon() {
        target = {}; owned = false; restoreAttempted = false;
        original = written = nextAttempt = 0; appliedRevision = 0;
        SetResult(Result::Idle);
    }
    bool Due(uint32_t now, bool enabled, uint64_t revision) const {
        return ((enabled && revision != writeRevision) || owned) &&
            (triedRevision != revision || int32_t(now - nextAttempt) >= 0);
    }
    // current() must prove the same connection/process generation AND a fresh
    // Resolve(base,profile) == target. Scene and entity membership are irrelevant.
    template<class Read, class Write, class Current>
    bool Restore(Read read, Write write, Current current) {
        if (!owned) return true;
        if (!current()) { SetResult(Result::RestorePending); return false; }
        uint32_t value = 0;
        if (!read(target.value, &value, 4)) { SetResult(Result::RestoreReadFailed); return false; }
        if (!current()) { SetResult(Result::RestorePending); return false; }
        // Resolve can perform many reads. Sample the payload again after that
        // proof, immediately before the conditional write, so an intervening
        // game change is not mistaken for our earlier value. DMA is not a CAS.
        if (!read(target.value, &value, 4)) { SetResult(Result::RestoreReadFailed); return false; }
        if (restoreAttempted && value == original) {
            owned = false; SetResult(Result::Restored); return true;
        }
        if (value != written) { owned = false; SetResult(Result::Changed); return true; }
        if (original == written) { owned = false; SetResult(Result::Restored); return true; }
        restoreAttempted = true;
        if (!write(target.value, &original, 4)) { SetResult(Result::RestoreWriteFailed); return false; }
        if (!current()) { SetResult(Result::RestorePending); return false; }
        if (!read(target.value, &value, 4)) { SetResult(Result::RestoreReadbackFailed); return false; }
        if (value != original) {
            if (value != written) owned = false;
            SetResult(Result::RestoreMismatch); return false;
        }
        owned = false; SetResult(Result::Restored); return true;
    }
    template<class Read, class Write, class Current>
    bool Apply(Target next, float percent, uint64_t revision, Read read, Write write, Current current) {
        if (!Valid(percent) || !revision) { SetResult(Result::InvalidValue); return false; }
        if (!current()) { SetResult(Result::TargetChanged); return false; }
        const bool moved = target != next;
        if (moved) { target = next; owned = false; restoreAttempted = false; appliedRevision = 0; }
        // Relocation never re-arms an already consumed command.
        if (moved && writeRevision == revision) { SetResult(Result::TargetChanged); return false; }
        uint32_t previous = 0;
        if (!read(target.value, &previous, 4) || !Valid(std::bit_cast<float>(previous))) {
            SetResult(Result::ReadFailed); return false;
        }
        if (writeRevision == revision) {
            if (owned && previous != written) { owned = false; SetResult(Result::Changed); }
            return result == Result::Applied;
        }
        if (!current()) { SetResult(Result::TargetChanged); return false; }
        if (!read(target.value, &previous, 4) || !Valid(std::bit_cast<float>(previous))) {
            SetResult(Result::ReadFailed); return false;
        }
        if (!owned || previous != written) original = previous;
        const uint32_t bits = std::bit_cast<uint32_t>(percent / 100.f);
        written = bits; owned = true; restoreAttempted = false;
        // A failed backend can still complete remotely. Consume before writing;
        // retain conditional restore but require a new Submit to retry the write.
        writeRevision = triedRevision = revision;
        if (!write(target.value, &bits, 4)) { SetResult(Result::WriteFailed); return false; }
        if (!current()) { SetResult(Result::TargetChanged); return false; }
        if (!read(target.value, &previous, 4)) { SetResult(Result::WriteReadbackFailed); return false; }
        if (previous != bits) { owned = false; SetResult(Result::WriteMismatch); return false; }
        appliedRevision = revision; SetResult(Result::Applied); return true;
    }
};
} // namespace OW::OutlineBrightness
