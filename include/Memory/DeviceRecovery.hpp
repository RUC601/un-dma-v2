#pragma once

namespace DmaDeviceRecovery {

enum class Result { Ready, Retry, Stopping };

// Shared by the runtime and software regression. Closing is unconditional:
// even a stop received while draining must leave no old device handle alive.
template<class Operations>
Result Rebuild(Operations& operations)
{
    operations.Quarantine();
    operations.DrainWorkers();
    operations.ClearSnapshots();
    operations.CloseDevice();
    if (operations.Stopping())
        return Result::Stopping;
    const bool ready = operations.InitializeDevice();
    if (operations.Stopping()) {
        operations.CloseDevice();
        return Result::Stopping;
    }
    return ready ? Result::Ready : Result::Retry;
}

// Failure means a failed SDK process enumeration, never merely an absent
// game, failed entity-field read, or an unsupported game version.
struct HealthFailures {
    unsigned consecutive = 0;
    bool Observe(bool available) noexcept {
        consecutive = available ? 0 : consecutive + 1;
        return consecutive >= 3;
    }
    void Reset() noexcept { consecutive = 0; }
};

// Detects when an attached target process is still alive in kernel _EPROCESS
// but its user-mode PE image (DTB/CR3 translation) has become unreadable.
// Three consecutive 1s failures trigger a safe drain + cache refresh + FixCr3
// reconnect; if two reconnect attempts still fail while the session is healthy,
// escalate once to a full DMA session rebuild so stale VMM DTB plugin state is
// discarded without looping during normal game startup.
struct TargetImageRecovery {
    static constexpr unsigned kUnreadableThreshold = 3;
    static constexpr unsigned kReconnectRebuildThreshold = 2;

    HealthFailures unreadable;
    bool reconnectPending = false;
    unsigned failedReconnects = 0;

    bool ObserveAttachedImageReadable(bool readable) noexcept {
        if (!unreadable.Observe(readable))
            return false;
        unreadable.Reset();
        reconnectPending = true;
        failedReconnects = 0;
        return true;
    }

    bool ObserveReconnectResult(bool connected) noexcept {
        if (connected) {
            Reset();
            return false;
        }
        if (!reconnectPending)
            return false;
        ++failedReconnects;
        if (failedReconnects >= kReconnectRebuildThreshold) {
            Reset();
            return true;
        }
        return false;
    }

    void OnConnected() noexcept { Reset(); }

    void Reset() noexcept {
        unreadable.Reset();
        reconnectPending = false;
        failedReconnects = 0;
    }
};

} // namespace DmaDeviceRecovery
