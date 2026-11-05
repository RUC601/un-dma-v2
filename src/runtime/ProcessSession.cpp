#include "ProcessSession.hpp"
#include "WorkerGroup.hpp"
#include "Alerts/Service.hpp"
#include "output/DeviceConnection.hpp"
#include "Memory/Memory.h"
#include "Memory/KeyState.hpp"
#include "Memory/DeviceRecovery.hpp"
#include "Game/Overwatch.hpp"
#include "Game/profiles/ProfileState.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/ProcessConnection.hpp"
#include <chrono>
#include <thread>
namespace V2::Runtime {
using V2::Output::InitializeOutputFromConfig;
using V2::Output::ReleaseOutputMouseStateForShutdown;
namespace {
constexpr const char* kTargetProcessName = "Overwatch.exe";
constexpr DWORD kProcessScanIntervalMs = 1000;
constexpr DWORD kHeroSkillRuntimeIntervalMs = 16;
std::atomic<bool> g_ProcessConnectionThreadRunning{false};
WorkerGroup g_Workers;
std::thread g_ProcessConnectionThread;
std::mutex g_ProcessConnectionMutex;
    static void RunDmaPeHeaderProbe()
    {
        if (!OW::Config::outputDebugLog) {
            Diagnostics::SetDmaProbeResult(false, false);
            return;
        }

        const uint64_t baseAddress = OW::SDK->dwGameBase;
        IMAGE_DOS_HEADER dosHeader{};
        bool readOk = false;
        if (baseAddress)
            readOk = mem.Read(static_cast<uintptr_t>(baseAddress), &dosHeader, sizeof(dosHeader));

        const uint16_t magic = readOk ? static_cast<uint16_t>(dosHeader.e_magic) : 0;
        const bool mzOk = readOk && dosHeader.e_magic == IMAGE_DOS_SIGNATURE;
        Diagnostics::SetDmaProbeResult(true, mzOk, baseAddress, magic);

        if (mzOk) {
            Diagnostics::Info("[PIPELINE] Stage 1 DMA PE header read OK: base=0x%llX magic=0x%04X.",
                static_cast<unsigned long long>(baseAddress),
                static_cast<unsigned int>(magic));
        } else {
            Diagnostics::Error("[PIPELINE] Stage 1 DMA PE header read failed: base=0x%llX read=%d magic=0x%04X.",
                static_cast<unsigned long long>(baseAddress),
                readOk ? 1 : 0,
                static_cast<unsigned int>(magic));
        }
    }

    static void HeroSkillRuntimeThread()
    {
        bool disconnectHandled = false;

        while (OW::RuntimeWorkersRunning()) {
            const DWORD tick = GetTickCount();

            if (OW::ProcessConnection::IsConnected()) {
                disconnectHandled = false;
                const OW::c_entity localSnapshot = OW::TargetingDetail::SnapshotLocalEntity();
                OW::HeroPerkRuntime::UpdateContext(localSnapshot.HeroID, true, localSnapshot.Team);
                OW::ProcessHeroSkills();
            } else if (!disconnectHandled) {
                OW::HeroPerkRuntime::ResetForDisconnect();
                OW::CancelActiveSkill();
                disconnectHandled = true;
            }

            const DWORD elapsed = GetTickCount() - tick;
            if (elapsed < kHeroSkillRuntimeIntervalMs)
                Sleep(kHeroSkillRuntimeIntervalMs - elapsed);
            else
                Sleep(1);
        }

        OW::HeroPerkRuntime::ResetForDisconnect();
        OW::CancelActiveSkill();
    }


}
// =============================================================================
// Background thread launcher
// =============================================================================

bool StartBackgroundThreads()
{
    try {
        const bool committed = OW::TryCommitRuntimeOperation([] {
            if(g_Workers.Running())return;
            OW::SetRuntimeWorkersRunning(true);
            std::vector<V2::Runtime::Worker> entries{
                {"acquisition",entity_thread},
                {"UN behaviors",aimbot_thread},{"configuration",configsavenloadthread},
                {"hero skills",HeroSkillRuntimeThread}, {"alerts",V2::Alerts::SkillWorker}
            };
            if(OW::PresentInterpolationEnabled())entries.push_back({"presentation",present_interp_thread});
            g_Workers.Start(entries, [] {
                OW::SetRuntimeOutputSessionEnabled(false);
                OW::SetRuntimeWorkersRunning(false);
            });
            KeyState::Start();
        });
        return committed && g_Workers.Running() && !g_Workers.Failed();
    } catch(const std::exception& error) {
        Diagnostics::Error("Runtime worker startup failed: %s",error.what());
        OW::SetRuntimeOutputSessionEnabled(false);
        // TryCommit lock has unwound: stopping here cannot deadlock its gate.
        OW::SetRuntimeWorkersRunning(false);
        g_Workers.Join();
        KeyState::Stop();
        return false;
    }
}

static void StopBackgroundThreads()
{
    OW::SetRuntimeWorkersRunning(false);
    g_Workers.Join();
}

static void ClearProcessRuntimeSnapshots()
{
    V2::Profiles::Publish({});
    OW::entitySceneGeneration.fetch_add(1,std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(OW::entityBodyValidityMutex);
        OW::ClearEntityBodyAuthoritiesLocked();OW::entityBodyDiscoveries.clear();OW::blockedEntityBodies.clear();
        OW::entityBodyAbsenceRevisions.clear();
    }
    OW::SDK->Reset();
    OW::ClearCriticalMatrixReadPlan();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        OW::ow_entities.clear();
        OW::ow_entities_scan.clear();
        OW::local_entity = OW::c_entity{};
        OW::abletotread = 0;
        OW::entity_fast_scan_until_tick = 0;
        OW::entity_topology_rescan_completed_request_count =
            OW::entity_topology_rescan_request_count;
    }
    {
        std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
        OW::latest_raw_scan_snapshot = OW::RawEntityScanSnapshot{};
        OW::last_consumed_raw_scan_generation = 0;
        OW::entity_discoveries={};OW::empty_scene_watch={};OW::raw_discovery_stamps.clear();
    }

    OW::TargetingDetail::PublishEntitySnapshots({}, {});
    OW::SetViewMatrices(OW::Matrix{}, OW::Matrix{});
    Diagnostics::SetEntityCount(0);
    Diagnostics::SetEntityProcessStats(Diagnostics::EntityProcessStats{});
    Diagnostics::SetPlayerInfoStats(Diagnostics::PlayerInfoStats{});
    Diagnostics::SetLocalEntityStats(Diagnostics::LocalEntityStats{});
    Diagnostics::SetRosterStats(Diagnostics::RosterStats{});
    Diagnostics::SetViewMatrixStatus(false, false);
    Diagnostics::SetDmaProbeResult(false, false);
}

void DrainProcessRuntimeWorkers(const char* reason)
{
    OW::SetRuntimeOutputSessionEnabled(false);
    // Signal every producer before joining any one of them. The device remains
    // alive until all workers have returned and closed their scatter handles.
    {
        // StartBackgroundThreads owns the same lock throughout construction;
        // every newly created worker must see this stop before we join it.
        std::lock_guard<std::mutex> lifecycleLock(OW::runtimeLifecycleMutex);
        OW::SetRuntimeWorkersRunning(false);
        mem.CancelPendingOperations();
    }
    OW::RuntimeOutputScheduler().CancelAllAndJoin(
        OW::OutputActionCancelReason::RuntimeChanged);
    ReleaseOutputMouseStateForShutdown(reason);
    KeyState::Stop();
    StopBackgroundThreads();
    OW::CancelActiveSkill();
}

void ResumeConnectionOperations()
{
    // Shutdown and reset of the cancellation flag must be indivisible. A late
    // recovery may not undo the exit request while StopProcessConnection joins.
    OW::TryCommitRuntimeOperation([] { mem.ResetPendingOperations(); });
}

static DmaDeviceRecovery::Result RebuildDmaDeviceSession(bool manualRequest)
{
    struct Operations {
        bool Stopping() const {
            return OW::RuntimeShutdownRequested() ||
                !g_ProcessConnectionThreadRunning.load(std::memory_order_acquire);
        }
        void Quarantine() {
            OW::SetRuntimeOutputSessionEnabled(false);
            OW::ProcessConnection::SetStatus(false, true, 0, 0,
                "Rebuilding DMA device; waiting for old calls to finish...");
            Diagnostics::SetDmaReady(false);
            Diagnostics::SetProcessAttached(false);
        }
        void DrainWorkers() { DrainProcessRuntimeWorkers("dma_device_rebuild"); }
        void ClearSnapshots() {
            ClearProcessRuntimeSnapshots();
            KeyState::ResetForDmaRecovery();
        }
        void CloseDevice() { mem.CloseDma(); }
        bool InitializeDevice() {
            ResumeConnectionOperations();
            if (Stopping() || !mem.LoadDmaDeviceConfig())
                return false;
            // A replacement host/device must not inherit the physical map
            // cached against the local computer's boot time.
            return mem.InitDma(false);
        }
    } operations;
    Diagnostics::Info("%s DMA device rebuild requested.", manualRequest ? "Manual" : "Automatic");
    const auto result = DmaDeviceRecovery::Rebuild(operations);
    if (result != DmaDeviceRecovery::Result::Stopping) {
        const bool ready = result == DmaDeviceRecovery::Result::Ready;
        Diagnostics::SetDmaReady(ready);
        OW::ProcessConnection::SetStatus(false, false, 0, 0, ready
            ? "DMA rebuilt; waiting for Overwatch.exe"
            : "DMA rebuild failed; check device/configuration; retrying");
    }
    return result;
}

static void MarkProcessDisconnected(const char* statusText)
{
    // Close the process-scoped output session before stopping producers. This
    // prevents a late scheduler call from self-synchronizing while disconnect
    // cleanup is still draining the old hardware generation.
    OW::SetRuntimeOutputSessionEnabled(false);
    const bool wasConnected = OW::ProcessConnection::IsConnected();
    OW::ProcessConnection::SetStatus(false, false, 0, 0,
        statusText ? statusText : "Waiting for Overwatch.exe");

    if (wasConnected) {
        Diagnostics::Warn("Target process disconnected.");
        std::printf("[MAIN] Target process disconnected; waiting for %s...\n", kTargetProcessName);
    }

    if (wasConnected || g_Workers.Running())
        DrainProcessRuntimeWorkers("target_disconnect");

    ClearProcessRuntimeSnapshots();
    mem.DetachProcess();
    ResumeConnectionOperations();
    Diagnostics::SetProcessAttached(false);
}

static bool TryConnectTargetProcess(bool forceReconnect, bool manualRequest)
{
    std::lock_guard<std::mutex> lock(g_ProcessConnectionMutex);
    if (OW::RuntimeShutdownRequested())
        return false;

    const bool wasConnected = OW::ProcessConnection::IsConnected();
    // A manual/PID-change reconnect is the same output ownership boundary as
    // a detected disconnect. Close normal output before publishing the
    // reconnecting state, then drain the old process generation completely.
    OW::SetRuntimeOutputSessionEnabled(false);
    OW::ProcessConnection::SetStatus(false, true, 0, 0,
        manualRequest ? "Reconnecting UN..." : "Connecting UN...");
    if (forceReconnect || wasConnected || g_Workers.Running())
        DrainProcessRuntimeWorkers("target_reconnect");
    ResumeConnectionOperations();
    // Global process discovery is permitted only after old readers/output have
    // drained. The active session uses uncached lifetime checks instead.
    if (!mem.RefreshProcessMetadata(manualRequest ? "manual_reconnect" : "process_discovery")) {
        MarkProcessDisconnected("Process metadata unavailable; retrying");
        return false;
    }
    const DWORD livePid = mem.GetPidFromName(kTargetProcessName);
    if (livePid == 0) {
        MarkProcessDisconnected("Waiting for Overwatch.exe");
        return false;
    }
    Diagnostics::SetProcessAttached(false);
    Diagnostics::Info("%s process connect attempt. target=%s pid=%lu",
        manualRequest ? "Manual" : "Automatic",
        kTargetProcessName,
        static_cast<unsigned long>(livePid));

    ClearProcessRuntimeSnapshots();
    if (forceReconnect)
        mem.DetachProcess();

    if (!OW::SDK->Initialize()) {
        mem.DetachProcess();
        Diagnostics::SetProcessAttached(false);
        OW::ProcessConnection::SetStatus(false, false, 0, 0,
            "Connect failed; waiting for Overwatch.exe");
        return false;
    }

    V2::Profiles::State profileState;
    profileState.module = OW::SDK->dwGameBase;
    profileState.pid = mem.GetCurrentProcessId();
    const int requested = V2::Profiles::requestedProfile.load();
    profileState.detection = V2::Profiles::Detect(profileState.module,
        [](uint64_t at, void* dst, size_t size) { return mem.ReadFreshExact(at, dst, size); },
        [&] { return !OW::RuntimeShutdownRequested() && mem.GetCurrentProcessId() == profileState.pid &&
            OW::SDK->dwGameBase == profileState.module; }, requested);
    const auto* matched = V2::Profiles::MatchedProfile(profileState.detection);
    if (!matched) {
        const bool ambiguous = profileState.detection.status == V2::Profiles::DetectionStatus::Ambiguous;
        MarkProcessDisconnected(ambiguous ? "Multiple region layouts matched; select a region"
                                         : "Waiting for a matching region layout");
        V2::Profiles::Publish(profileState);
        Diagnostics::Info("Region probe: request=%d state=%d matches=%d.", requested,
            int(profileState.detection.status), profileState.detection.matches);
        return false;
    }
    const int attachedPid = mem.GetCurrentProcessId();
    // Shutdown and attach commit share a short lock; DMA resolution stays outside it.
    if (!OW::TryCommitRuntimeOperation([&] {
        OW::offset::SetActiveProfile(matched->runtime);
        V2::Profiles::Publish(profileState);
        Diagnostics::SetProcessAttached(true);
        OW::ProcessConnection::SetStatus(true, false, attachedPid, OW::SDK->dwGameBase,
            std::string("Connected to Overwatch.exe (") + matched->Offsets().name + ")");
        OW::SetRuntimeOutputSessionEnabled(true);
    }))
        return false;
    Diagnostics::Info("Process attached: %s pid=%d base=0x%llX. Region %s %s (request=%d).",
        kTargetProcessName,
        attachedPid,
        static_cast<unsigned long long>(OW::SDK->dwGameBase), matched->region, matched->build, requested);
    std::printf("[MAIN] %s attached. PID=%d base=0x%llX\n",
        kTargetProcessName,
        attachedPid,
        static_cast<unsigned long long>(OW::SDK->dwGameBase));

    RunDmaPeHeaderProbe();

    if(!StartBackgroundThreads()) {
        MarkProcessDisconnected("Worker startup failed; retrying");
        return false;
    }
    return true;
}

void StartProcessConnectionThread()
{
    std::lock_guard<std::mutex> lifecycleLock(OW::runtimeLifecycleMutex);
    if (OW::RuntimeShutdownRequested() || g_ProcessConnectionThread.joinable())
        return;
    mem.ResetPendingOperations();
    g_ProcessConnectionThreadRunning.store(true, std::memory_order_release);
    OW::SetRuntimeOutputSessionEnabled(false);
    OW::ProcessConnection::SetStatus(false, false, 0, 0, "Waiting for DMA device");
    g_ProcessConnectionThread = std::thread([]() {
        Diagnostics::Info("Process connection thread started. target=%s interval_ms=%lu.",
            kTargetProcessName,
            static_cast<unsigned long>(kProcessScanIntervalMs));
        InitializeOutputFromConfig();

        DWORD lastAttemptTick = 0;
        DmaDeviceRecovery::HealthFailures healthFailures;
        while (g_ProcessConnectionThreadRunning.load(std::memory_order_acquire)) {
            const bool cacheRefreshRequest = OW::ProcessConnection::ConsumeCacheRefreshRequest();
            const bool manualRequest = OW::ProcessConnection::ConsumeReconnectRequest() || cacheRefreshRequest;
            const bool rebuildRequest = OW::ProcessConnection::ConsumeDmaRecoveryRequest();
            if (cacheRefreshRequest)
                Diagnostics::Info("Manual cache refresh requested; drain, clear snapshots, refresh VMM caches and reconnect.");
            const DWORD now = GetTickCount();
            const bool scanDue = lastAttemptTick == 0 || now - lastAttemptTick >= kProcessScanIntervalMs;

            if (rebuildRequest || (mem.IsDmaInitialized() && scanDue &&
                    healthFailures.Observe(mem.CheckSessionHealth()))) {
                const auto result = RebuildDmaDeviceSession(rebuildRequest);
                healthFailures.Reset();
                lastAttemptTick = GetTickCount();
                if (result == DmaDeviceRecovery::Result::Stopping)
                    break;
                continue;
            }

            if (!mem.IsDmaInitialized()) {
                if (manualRequest || scanDue) {
                    OW::ProcessConnection::SetStatus(false, true, 0, 0, "Connecting DMA device...");
                    const bool configReady = mem.LoadDmaDeviceConfig();
                    const bool ready = configReady && !OW::RuntimeShutdownRequested() &&
                        mem.InitDma(false);
                    Diagnostics::SetDmaReady(ready);
                    OW::ProcessConnection::SetStatus(false, false, 0, 0, ready
                        ? "DMA ready; waiting for Overwatch.exe"
                        : "DMA unavailable; check device settings or retry connection");
                    lastAttemptTick = GetTickCount();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            if (manualRequest) {
                TryConnectTargetProcess(true, true);
                lastAttemptTick = now;
            } else if (!OW::ProcessConnection::IsConnected()) {
                if (scanDue) {
                    TryConnectTargetProcess(false, false);
                    lastAttemptTick = now;
                }
            } else if (scanDue) {
                if(g_Workers.Failed() || !OW::RuntimeWorkersRunning())
                    MarkProcessDisconnected("Runtime worker failed; reconnecting");
                else if (!mem.CheckAttachedProcessAlive())
                    MarkProcessDisconnected("Target lifetime changed or unreadable; reconnecting");
                lastAttemptTick = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        Diagnostics::Info("Process connection thread stopping.");
    });
}

void StopProcessConnectionThread()
{
    g_ProcessConnectionThreadRunning.store(false, std::memory_order_release);
    mem.CancelPendingOperations();
    if (g_ProcessConnectionThread.joinable())
        g_ProcessConnectionThread.join();
}


bool ThreadsStopped() {
    return !g_Workers.Running() && !g_ProcessConnectionThread.joinable();
}
}
