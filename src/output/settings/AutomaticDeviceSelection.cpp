#include <Windows.h>
#include "output/AutomaticDeviceSelection.hpp"
#include "output/OutputRuntime.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"
#include <atomic>
#include <future>
#include <memory>
#include <mutex>

namespace V2::Output {
namespace {
struct Configuration {
    bool enabled = false;
    bool automatic = false;
    int deviceId = 0;
    DeviceDiscoveryRequest request;
    std::string key;
};

bool Supported(int id) {
    return id == int(DeviceId::KmBoxNet) || id == int(DeviceId::Makcu4M) ||
        id == int(DeviceId::Makcu115200) || id == int(DeviceId::Ferrum);
}

Configuration ReadConfigurationUnlocked() {
    OW::Config::SynchronizeOutputDeviceProfileUnlocked();
    Configuration value;
    value.enabled = OW::Config::outputEnabled;
    value.deviceId = OW::Config::outputDeviceType;
    value.automatic = OW::Config::outputAutoDetect && Supported(value.deviceId);
    auto& request = value.request;
    request.preferredId = static_cast<DeviceId>(value.deviceId);
    const auto makcu = OW::Config::GetOutputDeviceProfileUnlocked(int(DeviceId::Makcu115200));
    const auto ferrum = OW::Config::GetOutputDeviceProfileUnlocked(int(DeviceId::Ferrum));
    request.makcu = {makcu.port, makcu.instanceId,
        makcu.preferredDeviceId == int(DeviceId::Makcu4M) ? 4000000u : 115200u};
    request.ferrum = {ferrum.port, ferrum.instanceId, 115200};
    request.network = {OW::Config::outputIp,
        OW::Config::IsValidOutputUdpPort(OW::Config::outputPort)
            ? static_cast<uint16_t>(OW::Config::outputPort) : uint16_t{0},
        OW::Config::outputMac};
    // Length prefixes avoid collisions from arbitrary saved instance IDs.
    const auto append = [&](const std::string& part) {
        value.key += std::to_string(part.size()) + ":" + part;
    };
    append(std::to_string(value.enabled)); append(std::to_string(value.automatic));
    append(std::to_string(value.deviceId));
    append(request.makcu.port); append(request.makcu.instanceId);
    append(std::to_string(request.makcu.baud));
    append(request.ferrum.port); append(request.ferrum.instanceId);
    append(request.network.ip); append(std::to_string(request.network.commandPort));
    append(request.network.mac); append(std::to_string(OW::Config::EffectiveOutputMonitorPort()));
    return value;
}

Configuration ReadConfiguration() {
    std::lock_guard lock(OW::Config::mutex);
    return ReadConfigurationUnlocked();
}

struct DiscoveryResult {
    std::string key;
    std::vector<DeviceDiscoveryCandidate> candidates;
};
struct SelectionState {
    std::mutex mutex;
    bool started = false;
    uint64_t nextScanMs = 0;
    std::string appliedKey;
    std::string requestedKey;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::future<DiscoveryResult> pending;
    AutomaticDeviceStatus status;
};
SelectionState& State() { static SelectionState state; return state; }
void CancelPending(SelectionState& state) {
    if (state.cancel) state.cancel->store(true);
}
}

int ApplyOutputConfiguration(std::chrono::milliseconds timeout, bool forceRescan) {
#ifdef UNLEASHED_OBSERVATION_ONLY
    return ReconcileRuntimeFromConfig(timeout);
#else
    const auto config = ReadConfiguration();
    auto& state = State();
    std::lock_guard guard(state.mutex);
    if (!forceRescan && state.started && config.enabled && config.automatic) {
        if (state.appliedKey == config.key && IsRuntimeOutputGateOpen()) return success;
        if (state.pending.valid() && state.requestedKey == config.key &&
            state.cancel && !state.cancel->load()) return success;
    }
    state.started = true;
    CancelPending(state);
    state.nextScanMs = 0;
    state.appliedKey.clear();
    if (config.enabled && config.automatic) {
        state.status = {false, false, "Waiting to identify a connected device", {}};
        // Release the current owner before a temporary identification channel opens.
        return ShutdownActiveRuntime(timeout);
    }
    if (state.pending.valid()) {
        try { (void)state.pending.get(); } catch (...) {}
    }
    state.status = {false, false, config.enabled ? "Manual device selection" : "Output disabled", {}};
    return ReconcileRuntimeFromConfig(timeout);
#endif
}

void PollAutomaticDeviceSelection() {
#ifndef UNLEASHED_OBSERVATION_ONLY
    auto& state = State();
    std::lock_guard guard(state.mutex);
    if (!state.started) return;
    const auto config = ReadConfiguration();
    if (!config.enabled || !config.automatic) {
        CancelPending(state);
        state.status.scanning = false;
        // A finished cancelled future must be consumed before a later scan.
        if (state.pending.valid() && state.pending.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
            try { (void)state.pending.get(); } catch (...) {}
        }
        return;
    }
    if (state.pending.valid()) {
        if (state.pending.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
        DiscoveryResult result;
        try { result = state.pending.get(); }
        catch (const std::exception& error) {
            state.status = {false, false, std::string("Device detection failed: ") + error.what(), {}};
            state.nextScanMs = GetTickCount64() + 3000;
            return;
        }
        state.status.scanning = false;
        if (!state.cancel || state.cancel->load() || result.key != config.key) {
            state.nextScanMs = 0;
            return;
        }
        state.status.candidates = result.candidates;
        const auto choice = SelectDiscoveredDevice(result.candidates, config.request.preferredId);
        state.status.ambiguous = choice.ambiguous;
        state.nextScanMs = GetTickCount64() + 3000;
        if (!choice.index) {
            state.status.message = choice.ambiguous
                ? "Multiple devices are available; select the preferred device below"
                : "No ready device found; saved settings are kept";
            return;
        }
        const auto& selected = result.candidates[*choice.index];
        {
            std::lock_guard configGuard(OW::Config::mutex);
            if (ReadConfigurationUnlocked().key != result.key) return;
            auto profile = OW::Config::GetOutputDeviceProfileUnlocked(int(selected.id));
            if (!selected.port.empty()) {
                profile.port = selected.port;
                profile.instanceId = selected.instanceId;
            }
            profile.preferredDeviceId = int(selected.id);
            if (!OW::Config::SetOutputDeviceProfileUnlocked(int(selected.id), profile)) {
                state.status.message = "The identified device could not connect; retrying";
                return;
            }
            OW::Config::SelectOutputDeviceProfileUnlocked(int(selected.id));
        }
        const int status = ReconcileRuntimeFromConfig(std::chrono::milliseconds(500));
        if (status == success && IsRuntimeOutputGateOpen()) {
            state.appliedKey = ReadConfiguration().key;
            state.status.message = "The connected device is using its saved settings";
            Diagnostics::Info("[OUTPUT-AUTO] selected id=%d port=%s baud=%u; restored saved device settings",
                int(selected.id), selected.port.c_str(), selected.baud);
        } else {
            OW::Config::SelectOutputDeviceProfile(config.deviceId);
            state.status.message = "The identified device could not connect; retrying";
            Diagnostics::Warn("[OUTPUT-AUTO] identified device initialization failed id=%d status=%d", int(selected.id), status);
        }
        return;
    }
    if (ActiveRuntimeConnectionState() == OutputConnectionState::Connected && state.appliedKey == config.key)
        return;
    const auto now = GetTickCount64();
    if (now < state.nextScanMs) return;
    // A disconnected serial worker can still own the COM handle while retrying.
    // Shut it down before probing, with the existing release/cancel transaction.
    if (ActiveRuntimeSnapshot().descriptor.backend != OutputRuntimeBackend::None) {
        const int status = ShutdownActiveRuntime(std::chrono::milliseconds(500));
        if (status != success) {
            state.status.message = "Waiting for the previous device to finish disconnecting";
            state.nextScanMs = now + 1000;
            return;
        }
    }
    state.cancel = std::make_shared<std::atomic<bool>>(false);
    auto request = config.request;
    request.cancelled = [cancel = state.cancel] { return cancel->load(); };
    state.status = {true, false, "Identifying connected devices", {}};
    state.requestedKey = config.key;
    state.pending = std::async(std::launch::async, [request, key = config.key] {
        return DiscoveryResult{key, DiscoverDevices(request)};
    });
#endif
}

void StopAutomaticDeviceSelection() {
    auto& state = State();
    std::lock_guard guard(state.mutex);
    state.started = false;
    CancelPending(state);
    if (state.pending.valid()) {
        try { (void)state.pending.get(); } catch (...) {}
    }
    state.status.scanning = false;
}

AutomaticDeviceStatus GetAutomaticDeviceStatus() {
    auto& state = State();
    std::lock_guard guard(state.mutex);
    return state.status;
}
}
