#include "DeviceConnection.hpp"
#include "output/OutputRuntime.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"
#include <chrono>
#include <cstdio>
namespace V2::Output {
int InitializeOutputFromConfig()
{
#ifdef UNLEASHED_OBSERVATION_ONLY
    Diagnostics::Info("Observation build: physical output is disabled.");
    return 0;
#endif
    constexpr auto timeout = std::chrono::milliseconds(500);
    const int status = V2::Output::ReconcileRuntimeFromConfig(timeout);
    const V2::Output::OutputRuntimeAppliedState applied = V2::Output::RuntimeController().Applied();

    if (!OW::Config::outputEnabled) {
        std::printf("[OUTPUT] Disabled by config; runtime is stopped.\n");
        Diagnostics::Info("Output disabled by config; runtime reconciled to None.");
        Diagnostics::Aim("output.init reconciled disabled status=%d generation=%llu",
            status,
            static_cast<unsigned long long>(applied.generation));
        return status;
    }

    if (status == success && applied.descriptor.backend != V2::Output::OutputRuntimeBackend::None) {
        std::printf("[OUTPUT] Runtime ready. backend=%d generation=%llu\n",
            static_cast<int>(applied.descriptor.backend),
            static_cast<unsigned long long>(applied.generation));
        Diagnostics::Info("Output runtime ready. backend=%d generation=%llu",
            static_cast<int>(applied.descriptor.backend),
            static_cast<unsigned long long>(applied.generation));
        Diagnostics::Aim("output.init reconciled success backend=%d generation=%llu",
            static_cast<int>(applied.descriptor.backend),
            static_cast<unsigned long long>(applied.generation));
        return success;
    }

    std::printf("[OUTPUT] Runtime initialisation failed. status=%d\n", status);
    Diagnostics::Error("Output runtime initialisation failed. status=%d", status);
    Diagnostics::Aim("output.init reconciled failure status=%d", status);
    return status != success ? status : err_queue_stopped;
}

int ReleaseOutputMouseStateForShutdown(
    const char* reason,
    DWORD timeoutMs)
{
    Diagnostics::Aim("output.shutdown_output_cleanup reason=%s timeoutMs=%lu",
        reason ? reason : "unknown",
        static_cast<unsigned long>(timeoutMs));
    const auto timeout = std::chrono::milliseconds(timeoutMs);
    const int status = V2::Output::ReleaseActiveRuntime(timeout);
    if (status != success) {
        Diagnostics::Warn(
            "Device output cleanup completed with status=%d reason=%s timeoutMs=%lu.",
            status,
            reason ? reason : "unknown",
            static_cast<unsigned long>(timeoutMs));
    }
    return status;
}

int ShutdownOutputRuntime(const char* reason, DWORD timeoutMs)
{
    Diagnostics::Aim("output.lifecycle_shutdown reason=%s timeoutMs=%lu",
        reason ? reason : "unknown",
        static_cast<unsigned long>(timeoutMs));

    const int status = V2::Output::ShutdownActiveRuntime(
        std::chrono::milliseconds(timeoutMs));
    if (status != success) {
        Diagnostics::Warn("Output lifecycle shutdown completed with status=%d reason=%s.",
            status, reason ? reason : "unknown");
    }
    return status;
}


}
