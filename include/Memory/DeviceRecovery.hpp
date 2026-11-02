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

} // namespace DmaDeviceRecovery
