#ifdef UNLEASHED_SELF_TESTS
namespace AimbotDetail {int RunAimReleaseOutputSelfTest();}
namespace OW {int RunSkillModeScopeSelfTest();}
#endif
#include "features/visuals/EntityVisuals.hpp"
// =============================================================================
// Unleashed DMA -- Overwatch 2 External Cheat
//
// Entry point.  Initialises DMA, waits for Overwatch.exe, resolves global
// encryption keys, starts all background threads, then hands control to the
// DX11 full-screen display (message loop).
// =============================================================================

#include <Windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <string>

#include "Memory/Memory.h"        // ::mem   (global DMA instance)
#include "Memory/KeyState.hpp"    // host keyboard/mouse state via DMA
#include "Memory/DeviceRecovery.hpp"
#include "Game/AbilityIcons.hpp"  // ability icon lookup table
#include "Game/FovGeometry.hpp"   // shared angular FOV and overlay projection rules
#include "Game/HeroPerkRuntime.hpp" // manual perk variant runtime
#include "Game/HeroSkills.hpp"    // generic hero skill primitives
#include "Game/InputOrchestrator.hpp" // shared generated-output ownership
#include "Game/SDK.hpp"           // OW::SDK
#include "Game/Decrypt.hpp"       // OW::GetGlobalKey, OW::DecryptComponent
#include "game/profiles/ProfileState.hpp"
#include "runtime/ProcessSession.hpp"
#include "output/DeviceConnection.hpp"
#include "features/visuals/SceneRenderer.hpp"
#include "Game/Overwatch.hpp"     // main threads: viewmatrix, entity, aimbot, ...
#include "Game/Offsets.hpp"       // offset constants
#include "output/OutputRuntime.hpp" // single-owner Output runtime controller
#include "Utils/Config.hpp"       // OW::Config
#include "Utils/HostMouseDpi.hpp" // OW::RefreshHostMouseDpi
#include "Utils/ProcessConnection.hpp"
#include "Renderer/IconManager.hpp" // IconManager
#include "Renderer/Overlay.hpp"   // g_Overlay
#include "Renderer/Renderer.hpp"  // Render:: drawing primitives
#include "Features/UI.hpp"        // UI::Render
#include "Utils/CrashDump.hpp"    // crash dump capture for postmortem analysis
#include "Utils/Diagnostics.hpp"   // lightweight runtime diagnostics
#include "Utils/RuntimeTrace.hpp"
#include "Utils/TestServer.hpp"    // optional Scenario telemetry server

// =============================================================================
// Render callback  --  called every frame by the overlay
// =============================================================================

namespace {

    constexpr const char* kProductVariantName = "un-dma-v2";

    std::atomic<bool> g_DiagnosticsThreadRunning{ false };
    std::thread g_DiagnosticsThread;

    bool ProcessTimerResolutionRequested()
    {
        char buffer[16] = {};
        const DWORD length = GetEnvironmentVariableA(
            "UN_DMA_PROCESS_TIMER_RESOLUTION",
            buffer,
            static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return true;
        return !(std::strcmp(buffer, "0") == 0 ||
            _stricmp(buffer, "false") == 0 ||
            _stricmp(buffer, "off") == 0);
    }

    class ProcessTimerResolutionGuard {
    public:
        ProcessTimerResolutionGuard()
            : m_requested(ProcessTimerResolutionRequested())
        {
            if (!m_requested) {
                std::printf("[TIMER] Process 1 ms timer resolution disabled by experiment override.\n");
                return;
            }

            m_result = timeBeginPeriod(1);
            m_enabled = m_result == TIMERR_NOERROR;
            if (m_enabled) {
                std::printf("[TIMER] Process 1 ms timer resolution enabled.\n");
            } else {
                std::printf("[TIMER] timeBeginPeriod(1) failed. result=%u\n",
                    static_cast<unsigned int>(m_result));
            }
        }

        ~ProcessTimerResolutionGuard()
        {
            if (!m_enabled)
                return;
            const MMRESULT endResult = timeEndPeriod(1);
            std::printf("[TIMER] Process 1 ms timer resolution released. result=%u\n",
                static_cast<unsigned int>(endResult));
        }

        bool Requested() const { return m_requested; }
        bool Enabled() const { return m_enabled; }
        MMRESULT Result() const { return m_result; }

    private:
        bool m_requested = false;
        bool m_enabled = false;
        MMRESULT m_result = TIMERR_NOERROR;
    };

    std::string AbsolutePathForLog(const std::string& path)
    {
        try {
            return std::filesystem::absolute(path).string();
        } catch (...) {
            return path;
        }
    }

    bool HasCommandLineFlag(int argc, char** argv, const char* flag)
    {
        for (int index = 1; index < argc; ++index) {
            if (argv[index] && std::strcmp(argv[index], flag) == 0)
                return true;
        }
        return false;
    }

    bool TryGetCommandLineInt(int argc, char** argv, const char* flag, int& value)
    {
        for (int index = 1; index + 1 < argc; ++index) {
            if (!argv[index] || std::strcmp(argv[index], flag) != 0)
                continue;

            char* end = nullptr;
            errno = 0;
            const long parsed = std::strtol(argv[index + 1], &end, 10);
            if (end == argv[index + 1] ||
                *end != '\0' ||
                errno == ERANGE ||
                parsed < (std::numeric_limits<int>::min)() ||
                parsed > (std::numeric_limits<int>::max)()) {
                return false;
            }

            value = static_cast<int>(parsed);
            return true;
        }
        return false;
    }

    static void PreloadHeroAvatars()
    {
        IconManager* icons = Render::GetIconManager();
        if (!icons)
            return;

        static constexpr const char* kHeroAvatarNames[] = {
            "Ana", "Anran", "Ashe", "Baptiste", "Bastion", "Brigitte",
            "Cassidy", "Doctrine", "Domina", "Doomfist", "D.Va", "D.Mon", "Echo", "Emre",
            "Freja", "Genji", "Hanzo", "Hazard", "Illari", "Jetpack Cat",
            "Junker Queen", "Junkrat", "Juno", "Kiriko", "LifeWeaver",
            "Lucio", "Mauga", "Mei", "Mercy", "Mizuki", "Moira", "Orisa",
            "Pharah", "Ramattra", "Reaper", "Reinhardt", "Roadhog",
            "Shion", "Sierra", "Sigma", "Sojourn", "Soldier 76", "Sombra",
            "Symmetra", "Torbjorn", "Tracer", "Vendetta", "Venture",
            "Widowmaker", "Winston", "Wrecking Ball", "Wuyang", "Zarya",
            "Zenyatta"
        };

        int loadedCount = 0;
        int overlayLoadedCount = 0;
        for (const char* heroName : kHeroAvatarNames) {
            const std::string slug = OW::HeroDisplayNameToSlug(heroName);
            if (!slug.empty() && icons->LoadHeroAvatar(slug))
                ++loadedCount;
            if (!slug.empty() && icons->LoadHeroAvatar(slug, IconManager::AvatarUse::Overlay))
                ++overlayLoadedCount;
        }

        constexpr size_t heroCount = sizeof(kHeroAvatarNames) / sizeof(kHeroAvatarNames[0]);
        std::printf("[MAIN] Hero avatars loaded: UI %d/%zu, overlay %d/%zu.\n",
            loadedCount, heroCount, overlayLoadedCount, heroCount);
        Diagnostics::Info("Hero avatars loaded: UI %d/%zu, overlay %d/%zu.",
            loadedCount, heroCount, overlayLoadedCount, heroCount);
    }

    static void StartDiagnosticStatusThread()
    {
        g_DiagnosticsThreadRunning.store(true, std::memory_order_release);
        g_DiagnosticsThread = std::thread([]() {
            Diagnostics::Info("Diagnostic status thread started with 5s interval.");
            unsigned intervals = 0;
            while (g_DiagnosticsThreadRunning.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (!g_DiagnosticsThreadRunning.load(std::memory_order_acquire))
                    break;
                if (++intervals == 50) {
                    Diagnostics::DumpStatus();
                    intervals = 0;
                }
            }
        });
    }

    static void StopDiagnosticStatusThread()
    {
        g_DiagnosticsThreadRunning.store(false, std::memory_order_release);
        if (g_DiagnosticsThread.joinable())
            g_DiagnosticsThread.join();
    }

    static void PreloadAbilityIcons()
    {
        IconManager* iconManager = Render::GetIconManager();
        if (!iconManager) {
            Diagnostics::Warn("Ability icon preload skipped: icon manager is not ready.");
            return;
        }

        int loadedHeroes = 0;
        for (const OW::HeroAbilityIcons& icons : OW::AllHeroAbilityIcons()) {
            if (iconManager->LoadAbilityIcons(icons.heroSlug, {
                    icons.ability1Icon,
                    icons.ability2Icon,
                    icons.ultimateIcon
                })) {
                ++loadedHeroes;
            }
        }

        Diagnostics::Info("Ability icon preload complete. heroes=%d/%zu.",
            loadedHeroes,
            OW::HeroAbilityIconCount());
        std::printf("[MAIN] Ability icons preloaded for %d/%zu hero entries.\n",
            loadedHeroes,
            OW::HeroAbilityIconCount());
    }

} // namespace

static void LoadRuntimeConfigForDiagnostics()
{
    OW::Config::InitializeActiveConfigProfile();
    const std::string configPath = OW::Config::ConfigPath();
    const std::string configPathForLog = AbsolutePathForLog(configPath);
    OW::Config::LoadConfig(configPath);
    OW::RefreshHostMouseDpi();
    OW::RefreshScreenSizeFromConfig();
    Diagnostics::Aim("main.config_loaded configPath=%s screen=%.0fx%.0f outputEnabled=%d deviceType=%d ip=%s commandPort=%d effectiveMonitorPort=%d monitorPortSetting=%d manualOverride=%d countsPerRadian=%.6f calibratedCountsPerRadian=%.6f gameMouseSensitivity=%.6f referenceGameSensitivity=%.6f autoScaleByGameSensitivity=%d hostMouseDpi=%.6f hostDpiDetected=%d",
        configPathForLog.c_str(),
        OW::WX,
        OW::WY,
        OW::Config::outputEnabled ? 1 : 0,
        OW::Config::outputDeviceType,
        OW::Config::outputIp,
        OW::Config::outputPort,
        OW::Config::EffectiveOutputMonitorPort(),
        OW::Config::outputMonitorPort,
        OW::Config::outputMonitorPortManualOverride ? 1 : 0,
        OW::Config::outputCountsPerRadian,
        OW::Config::calibratedCountsPerRadian,
        OW::Config::gameMouseSensitivity,
        OW::Config::referenceGameSensitivity,
        OW::Config::autoScaleByGameSensitivity ? 1 : 0,
        OW::Config::hostMouseDpi,
        OW::Config::hostMouseDpiAutoDetected ? 1 : 0);
}

#include "Alerts/Service.hpp"
using V2::Runtime::StartProcessConnectionThread;
using V2::Runtime::StopProcessConnectionThread;
using V2::Output::ShutdownOutputRuntime;

static void ShutdownHeadlessRuntime(const char* reason = "headless_shutdown")
{
    OW::RequestRuntimeShutdown([] {
        OW::SetRuntimeOutputSessionEnabled(false);
    });
    mem.CancelPendingOperations();
    StopProcessConnectionThread();
    V2::Alerts::Stop();
    V2::Runtime::DrainProcessRuntimeWorkers(reason);
    TestServer::Stop();
    KeyState::Stop();
    OW::ProcessConnection::SetStatus(false, false, 0, 0, "Shutting down");
    StopDiagnosticStatusThread();
    mem.CloseDma();
    Diagnostics::SetDmaReady(false);
    Diagnostics::SetProcessAttached(false);
    ShutdownOutputRuntime(reason);
    Diagnostics::ShutdownAimLog();
    Diagnostics::Shutdown();
    RuntimeTrace::Stop();
}

static int RunConfigCheckCli()
{
    OW::offset::SetActiveProfile(OW::offset::kProductRuntimeProfile);
    OW::Config::InitializeActiveConfigProfile();
    const std::string configPath = OW::Config::ConfigPath();
    if (OW::Config::LoadConfig(configPath) != OW::Config::ConfigLoadResult::Loaded) {
        std::fprintf(stderr, "[CONFIG] Load failed: %s\n", OW::Config::LastPersistenceError().c_str());
        return EXIT_FAILURE;
    }

    std::printf("[PRODUCT] variant=%s executable=UNDMA_V2.exe lockedProfile=%s\n",
        kProductVariantName,
        OW::offset::ActiveProfileName());
    std::printf("[CONFIG] directory=%s\n", OW::Config::ConfigDirectoryPath().c_str());
    std::printf("[CONFIG] profile=%s\n", OW::Config::configFileName.c_str());
    std::printf("[CONFIG] path=%s\n", AbsolutePathForLog(configPath).c_str());
    std::printf("[CONFIG] heroPath=%s\n", AbsolutePathForLog(OW::Config::HeroConfigPath(configPath)).c_str());
    std::printf("[CONFIG] ordinaryFlick repeatWindowMs=%.3f maxSpeedDegPerSec=%.3f windowCap=half_cycle fireStateEvidence=missing policy=time_and_known_restrictions shortcuts=blocked\n",
        OW::Config::flickRepeatWindowMs, OW::Config::flickRepeatMaxSpeedDegPerSec);
    const auto cassFlick = OW::Config::GetHeroAimPresetOrDefault(
        static_cast<uint64_t>(OW::eHero::HERO_MCCREE), 1);
    const auto* cassCadence = OW::RuntimeWeaponCadenceTable().Find("cassidy_peacekeeper");
    std::printf("[CONFIG] cassMouse5 behavior=%d key=%d action=%d keepFiring=%d smoothPct=%.3f behaviorPresetId=%d shotClampMs=%.3f postFireDelayMs=%.3f cadenceMs=%.3f evidence=compiled_catalog_not_live\n",
        cassFlick.aimBehavior, cassFlick.key, cassFlick.trigger.action, cassFlick.keepFiring ? 1 : 0,
        cassFlick.smooth, cassFlick.aimBehaviorPresetId, cassFlick.flickShotClampMs, cassFlick.flickPostFireDelayMs,
        cassCadence && cassCadence->triggerCycleIntervalMs ? *cassCadence->triggerCycleIntervalMs : 0.0f);
    const auto printOffsetProfile = [](const OW::offset::RuntimeOffsetProfile& profile) {
        std::printf("[OFFSETS] profile=%s team=0x%02X playerController=0x%02X rotation=0x%02X link=0x%02X health=0x%02X hero=0x%02X entityRoot=0x%llX\n",
            profile.name,
            static_cast<unsigned int>(profile.typeTeam),
            static_cast<unsigned int>(profile.typePlayerController),
            static_cast<unsigned int>(profile.typeRotation),
            static_cast<unsigned int>(profile.typeLink),
            static_cast<unsigned int>(profile.typeHealth),
            static_cast<unsigned int>(profile.typeHeroId),
            static_cast<unsigned long long>(profile.Address_entity_base));
        const char* viewMode = "encrypted-chain";
        switch (profile.viewMatrixMode) {
        case OW::offset::ViewMatrixMode::DirectChain:
            viewMode = "direct-chain";
            break;
        case OW::offset::ViewMatrixMode::EncryptedDirectMatrix:
            viewMode = "encrypted-direct-matrix";
            break;
        case OW::offset::ViewMatrixMode::EncryptedChainSubXorSub:
            viewMode = "encrypted-chain-sub-xor-sub";
            break;
        case OW::offset::ViewMatrixMode::EncryptedChainXorAdd:
            viewMode = "encrypted-chain-xor-add";
            break;
        default:
            break;
        }
        std::printf("[OFFSETS] viewMatrix mode=%s root=0x%llX p1=0x%llX p2=0x%llX parent=0x%llX ptr=0x%llX matrix=0x%llX camera=0x%llX projection=0x%llX globalAdmin=0x%llX\n",
            viewMode,
            static_cast<unsigned long long>(profile.Address_viewmatrix_base),
            static_cast<unsigned long long>(profile.VM_P1),
            static_cast<unsigned long long>(profile.VM_P2),
            static_cast<unsigned long long>(profile.VM_ViewProjectionParent),
            static_cast<unsigned long long>(profile.VM_ViewProjectionPtr),
            static_cast<unsigned long long>(profile.VM_ViewProjectionMatrix),
            static_cast<unsigned long long>(profile.VM_ViewMatrix),
            static_cast<unsigned long long>(profile.VM_ProjMatrix),
            static_cast<unsigned long long>(profile.GlobalAdmin_RVA));
        const char* chain = profile.viewMatrixChain == OW::offset::ViewMatrixChainMode::CameraProjectionShort
            ? "camera-projection-short" : "legacy-long";
        std::printf("[OFFSETS] viewMatrix selected_chain=%s (profile-static)\n", chain);
        std::printf("[OFFSETS] projection_source=%s composition=camera_times_normalized_projection\n",
            profile.projectionFromRender ? "render-output" : "context-primary");
    };
    printOffsetProfile(OW::offset::Active());
    std::printf("[CONFIG] countsPerRadian=%.6f calibratedCountsPerRadian=%.6f gameMouseSensitivity=%.6f referenceGameSensitivity=%.6f autoScaleByGameSensitivity=%d\n",
        OW::Config::outputCountsPerRadian,
        OW::Config::calibratedCountsPerRadian,
        OW::Config::gameMouseSensitivity,
        OW::Config::referenceGameSensitivity,
        OW::Config::autoScaleByGameSensitivity ? 1 : 0);
    std::printf("[CONFIG] commandPort=%d effectiveMonitorPort=%d monitorPortSetting=%d manualOverride=%d\n",
        OW::Config::outputPort,
        OW::Config::EffectiveOutputMonitorPort(),
        OW::Config::outputMonitorPort,
        OW::Config::outputMonitorPortManualOverride ? 1 : 0);
    std::printf("[CONFIG] triggerBoneMask=0x%03X triggerBoneMask2=0x%03X default=0x%03X all=0x%03X\n",
        static_cast<unsigned int>(OW::Config::View_triggerbotBoneMask()),
        static_cast<unsigned int>(OW::Config::triggerbotBoneMask2),
        static_cast<unsigned int>(OW::kDefaultTriggerBoneMask),
        static_cast<unsigned int>(OW::kTriggerBoneAllMask));
    std::printf("[CONFIG] triggerDisableWhileReloading=%d triggerDisableWhileReloading2=%d\n",
        OW::Config::View_triggerbotDisableWhileReloading() ? 1 : 0,
        OW::Config::triggerbotDisableWhileReloading2 ? 1 : 0);
    std::printf("[CONFIG] aimBoneMask=0x%03X aimBoneMask2=0x%03X default=0x%03X all=0x%03X\n",
        static_cast<unsigned int>(OW::Config::View_aimbotBoneMask()),
        static_cast<unsigned int>(OW::Config::aimbotBoneMask2),
        static_cast<unsigned int>(OW::kDefaultAimBoneMask),
        static_cast<unsigned int>(OW::kSkeletonBoneAllMask));
    const OW::AimStartLimiterProfile trackingStartLimiter =
        OW::Config::ResolveAimStartLimiterProfile(OW::Config::kAimBehaviorTracking);
    const OW::Config::AimBehaviorPreset* trackingBehaviorPreset =
        OW::Config::ActiveAimBehaviorPreset(OW::Config::kAimBehaviorTracking);
    std::printf("[CONFIG] trackingStartLimiter source=%s presetId=%d enabled=%d initialCapDegPerSec=%.3f capRiseDegPerSec2=%.3f restartOnTargetChange=%d\n",
        trackingBehaviorPreset ? "behavior-preset" : "class-default",
        trackingBehaviorPreset ? trackingBehaviorPreset->id : -1,
        trackingStartLimiter.enabled ? 1 : 0,
        trackingStartLimiter.initialCapDegPerSec,
        trackingStartLimiter.capRiseDegPerSec2,
        trackingStartLimiter.restartOnTargetChange ? 1 : 0);
    return 0;
}

// =============================================================================
// Main
// =============================================================================

#ifdef UNLEASHED_SELF_TESTS
#include "OverlayStatusSelfTest.inl"
#include "RuntimeControlSelfTest.inl"
#include "OverlayFrameRateProbe.inl"
#endif

int main(int argc, char** argv)
{
    CrashDump::InstallUnhandledExceptionFilter(L"crash_dumps");

    // ---- Console ----
    SetConsoleTitleA("UNDMA V2");

    std::printf("\n");
    std::printf("  ============================================\n");
    std::printf("     UNDMA V2 -- Overwatch CN\n");
    std::printf("  ============================================\n");
    std::printf("\n");

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] ? argv[index] : "";
        if (argument == "--help" || argument == "-h") {
            std::printf("Usage: UNDMA_V2 [--config-check] [--test-server]\n"
                        "       [--test-server-port 1..65535] [--test-server-cors] [--box-perf-mode]\n       [--run-seconds 1..1800] [--reconnect-after 1..1799] [--canvas-only]\n       [--profile auto|cn|world|nexon]\n");
            return 0;
        }
        if (argument == "--profile") {
            if (++index>=argc) { std::fprintf(stderr,"Missing --profile value.\n"); return 2; }
            const std::string value=argv[index];
            if (value == "world-candidate" || value == "nexon-candidate" || value == "kr-candidate") {
                std::fprintf(stderr, "Candidate-only profiles were retired. Use --profile world or --profile nexon for the full runtime.\n");
                return 2;
            }
            const int selected = value == "auto" ? -1 : value == "cn" ? 0 : value == "world" ? 1 : (value == "nexon" || value == "kr") ? 2 : -2;
            if (selected == -2) { std::fprintf(stderr, "Profile must be auto, cn, world or nexon.\n"); return 2; }
            V2::Profiles::requestedProfile.store(selected);
            continue;
        }
        if (argument == "--test-server-port" || argument == "--run-seconds" || argument == "--reconnect-after") {
            if (++index >= argc) {
                std::fprintf(stderr, "[FATAL] %s requires a value.\n", argument.c_str());
                return 1;
            }
            continue;
        }
        if (argument == "--config-check" || argument == "--test-server" ||
            argument == "--test-server-cors" || argument == "--box-perf-mode" || argument == "--canvas-only")
            continue;
#ifdef UNLEASHED_SELF_TESTS
        if (argument == "--skill-mode-scope-selftest" || argument == "--aim-release-output-selftest" || argument == "--config-concurrency-selftest" || argument == "--config-persistence-selftest" ||
            argument == "--test-server-lifecycle-selftest" ||
            argument == "--overlay-status-selftest" || argument == "--runtime-control-selftest" ||
            argument == "--overlay-frame-rate-probe")
            continue;
#endif
        std::fprintf(stderr, "[FATAL] Unknown or retired option: %s\n", argument.c_str());
        return 1;
    }

    // ---- Config ----

#ifdef UNLEASHED_SELF_TESTS
    if (HasCommandLineFlag(argc, argv, "--overlay-frame-rate-probe"))
        return RunOverlayFrameRateProbe();
    if (HasCommandLineFlag(argc, argv, "--runtime-control-selftest"))
        return RunRuntimeControlSelfTest();
    if (HasCommandLineFlag(argc, argv, "--overlay-status-selftest"))
        return RunOverlayStatusSelfTest();
    if (HasCommandLineFlag(argc, argv, "--config-concurrency-selftest"))
        return OW::Config::RunConfigConcurrencySelfTest();
    if (HasCommandLineFlag(argc, argv, "--skill-mode-scope-selftest"))
        return OW::RunSkillModeScopeSelfTest();
    if (HasCommandLineFlag(argc, argv, "--aim-release-output-selftest"))
        return AimbotDetail::RunAimReleaseOutputSelfTest();
    if (HasCommandLineFlag(argc, argv, "--config-persistence-selftest"))
        return OW::Config::RunPersistenceSelfTest();
    if (HasCommandLineFlag(argc, argv, "--test-server-lifecycle-selftest"))
        return TestServer::RunLifecycleSelfTest();
#endif
    if (HasCommandLineFlag(argc, argv, "--config-check"))
        return RunConfigCheckCli();
    const bool testServerRequested = HasCommandLineFlag(argc, argv, "--test-server");
    const bool testServerCorsRequested = HasCommandLineFlag(argc, argv, "--test-server-cors");
    const bool boxPerfModeRequested = HasCommandLineFlag(argc, argv, "--box-perf-mode");
    int testServerPortValue = 19560;
    if (HasCommandLineFlag(argc, argv, "--test-server-port")) {
        if (!TryGetCommandLineInt(argc, argv, "--test-server-port", testServerPortValue) ||
            testServerPortValue <= 0 ||
            testServerPortValue > 65535) {
            std::fprintf(stderr, "[FATAL] Invalid --test-server-port. Expected 1..65535.\n");
            return 1;
        }
    }

    int runSeconds=0, reconnectAfter=0;
    for(const auto& option : {std::pair{"--run-seconds", &runSeconds},
                              std::pair{"--reconnect-after", &reconnectAfter}}) {
        if(HasCommandLineFlag(argc,argv,option.first) &&
           (!TryGetCommandLineInt(argc,argv,option.first,*option.second) ||
            *option.second<1 || *option.second>1800)) {
            std::fprintf(stderr,"[FATAL] Invalid %s; expected 1..1800.\n",option.first);
            return 1;
        }
    }
    if(reconnectAfter && (!runSeconds || reconnectAfter>=runSeconds)) {
        std::fprintf(stderr,"[FATAL] --reconnect-after must be less than --run-seconds.\n");
        return 1;
    }

    if (!RuntimeTrace::Start()) { std::fprintf(stderr,"Trace file open failed.\n"); return 2; }
    ProcessTimerResolutionGuard processTimerResolution;

    // ---------------------------------------------------------------
    // Step 1 -- Load settings before starting hardware connections
    // ---------------------------------------------------------------
    Diagnostics::Initialize(Diagnostics::LogLevel::Info, "./undma_v2_diag.log");
    Diagnostics::InitializeAimLog("./undma_v2_aim_diag.log");
    Diagnostics::SetProcessTimerResolutionStatus(
        processTimerResolution.Requested(),
        processTimerResolution.Enabled(),
        static_cast<uint32_t>(processTimerResolution.Result()));
    Diagnostics::Info(
        "Process timer resolution: requested=%d enabled=%d result=%u period_ms=1.",
        processTimerResolution.Requested() ? 1 : 0,
        processTimerResolution.Enabled() ? 1 : 0,
        static_cast<unsigned int>(processTimerResolution.Result()));
    OW::Config::InitializeActiveConfigProfile();
    const std::string configPath = OW::Config::ConfigPath();
    const std::string configPathForLog = AbsolutePathForLog(configPath);
    if (OW::Config::LoadConfig(configPath) != OW::Config::ConfigLoadResult::Loaded) {
        Diagnostics::Error("Config load failed: %s", OW::Config::LastPersistenceError().c_str());
        return EXIT_FAILURE;
    }
    if (boxPerfModeRequested) {
        OW::Config::boxPerfMode = true;
        OW::Config::boxPerfFastRect = true;
    }
    OW::RefreshHostMouseDpi();
    OW::RefreshScreenSizeFromConfig();
    Diagnostics::Aim("main.config_loaded configPath=%s screen=%.0fx%.0f outputEnabled=%d deviceType=%d ip=%s commandPort=%d effectiveMonitorPort=%d monitorPortSetting=%d manualOverride=%d countsPerRadian=%.6f calibratedCountsPerRadian=%.6f gameMouseSensitivity=%.6f referenceGameSensitivity=%.6f autoScaleByGameSensitivity=%d hostMouseDpi=%.6f hostDpiDetected=%d",
        configPathForLog.c_str(),
        OW::WX,
        OW::WY,
        OW::Config::outputEnabled ? 1 : 0,
        OW::Config::outputDeviceType,
        OW::Config::outputIp,
        OW::Config::outputPort,
        OW::Config::EffectiveOutputMonitorPort(),
        OW::Config::outputMonitorPort,
        OW::Config::outputMonitorPortManualOverride ? 1 : 0,
        OW::Config::outputCountsPerRadian,
        OW::Config::calibratedCountsPerRadian,
        OW::Config::gameMouseSensitivity,
        OW::Config::referenceGameSensitivity,
        OW::Config::autoScaleByGameSensitivity ? 1 : 0,
        OW::Config::hostMouseDpi,
        OW::Config::hostMouseDpiAutoDetected ? 1 : 0);
    std::printf("[MAIN] Screen size: %.0fx%.0f\n", OW::WX, OW::WY);
    if (OW::Config::boxPerfMode) {
        std::printf("[MAIN] Box performance mode enabled (%s box path).\n",
            OW::Config::boxPerfFastRect ? "fast rect" : "corner");
    }
    Diagnostics::Info("Overlay box performance mode: enabled=%d fast_rect=%d cli=%d.",
        OW::Config::boxPerfMode ? 1 : 0,
        OW::Config::boxPerfFastRect ? 1 : 0,
        boxPerfModeRequested ? 1 : 0);
    Diagnostics::SetDmaReady(false);
    StartDiagnosticStatusThread();

    if (testServerRequested) {
        TestServer::Options testServerOptions{};
        testServerOptions.port = static_cast<uint16_t>(testServerPortValue);
        testServerOptions.allowWildcardCors = testServerCorsRequested;
        if (!TestServer::Start(testServerOptions)) {
            std::fprintf(stderr, "[FATAL] Failed to start UNDMA CN test server on 127.0.0.1:%d.\n",
                testServerPortValue);
            ShutdownHeadlessRuntime();
            return 2;
        }
        std::printf("[MAIN] Test server ready on http://127.0.0.1:%d%s\n\n",
            testServerPortValue,
            testServerCorsRequested ? " (wildcard CORS enabled)" : "");
    }

    // ---------------------------------------------------------------
    // Step 2 -- Start target process connector
    // ---------------------------------------------------------------
    OW::ProcessConnection::SetStatus(false, false, 0, 0, "Waiting for DMA device");

    // ---------------------------------------------------------------
    // Step 3 -- Resolve global encryption keys (SKIP: vestigial)
    // May 2026 DecryptComponent reads key material directly from game
    // memory and does not use GlobalKey1/2.  GetGlobalKey() is kept
    // as a no-op for diagnostic probes but no longer blocks startup.
    // ---------------------------------------------------------------
    std::printf("[MAIN] Skipping global key resolution (not used by current decrypt).\n\n");
    Diagnostics::SetKeyStatus(Diagnostics::KeyStatus::Skipped);
    Diagnostics::Info("Global key resolution skipped; current decrypt path reads key material directly.");

    // ---------------------------------------------------------------
    // Step 4 -- Initialise the DX11 overlay windows
    // ---------------------------------------------------------------
    std::printf("[MAIN] Initialising overlay...\n");
    Diagnostics::Info("Initialising overlay.");
    if (!g_Overlay.Initialize(L"UNDMA V2")) {
        std::fprintf(stderr, "[FATAL] Overlay initialisation failed.\n");
        Diagnostics::Error("Overlay initialisation failed.");
        ShutdownHeadlessRuntime("overlay_init_failure");
        std::printf("[INFO] Press Enter to exit.\n");
        std::getchar();
        return 1;
    }
    std::printf("[MAIN] Overlay ready.\n\n");
    Diagnostics::Info("Overlay ready.");
    PreloadHeroAvatars();
    PreloadAbilityIcons();
    V2::Alerts::Start();
    StartProcessConnectionThread();
    Diagnostics::Info("Settings ready; hardware and process connection run in background.");

    // ---------------------------------------------------------------
    // Step 5 -- Main loop (blocks until overlay / game closes)
    // ---------------------------------------------------------------
    std::printf("[MAIN] Entering message loop.  Press HOME to toggle menu.\n");
    std::printf("[MAIN] Close the canvas process/window to exit.\n\n");
    Diagnostics::Info("Entering overlay message loop.");
    Diagnostics::SetRenderThread();

#ifdef UNLEASHED_OBSERVATION_ONLY
    if(std::getenv("UN_DMA_OBSERVE_SHAPES")) OW::Config::draw_hitbox=true;
    // Keep the canvas running while the observer inspects OBS on this machine.
    // This is a validation launch option; it does not alter saved settings.
    if (std::getenv("UN_DMA_OBSERVE_CANVAS_ONLY"))
        g_Overlay.HideMenu();
    if(const char* seconds=std::getenv("UN_DMA_OBSERVE_SECONDS")) {
        const auto duration=std::clamp(std::strtoul(seconds,nullptr,10),1ul,1800ul)*1000;
        SetTimer(nullptr,0,static_cast<UINT>(duration),[](HWND,UINT,UINT_PTR timer,DWORD) {
            KillTimer(nullptr,timer);g_Overlay.RequestExit();
        });
    }
#endif
    // 有界现场验证使用正式消息循环和正式停机/重连路径，不强杀、不改持久配置。
    if(HasCommandLineFlag(argc,argv,"--canvas-only"))g_Overlay.HideMenu();
    if(runSeconds)SetTimer(nullptr,0,UINT(runSeconds*1000),[](HWND,UINT,UINT_PTR timer,DWORD) {
        KillTimer(nullptr,timer);
        Diagnostics::Info("Validation duration reached; requesting normal exit.");
        g_Overlay.RequestExit();
    });
    if(reconnectAfter)SetTimer(nullptr,0,UINT(reconnectAfter*1000),[](HWND,UINT,UINT_PTR timer,DWORD) {
        KillTimer(nullptr,timer);
        Diagnostics::Info("Validation timer requesting normal reconnect.");
        OW::ProcessConnection::RequestReconnect();
    });
    g_Overlay.Run(V2::Visuals::DrawFrame);

    // ---------------------------------------------------------------
    // Cleanup  --  Run() calls Shutdown() internally, so we just tidy up
    // ---------------------------------------------------------------
    std::printf("\n[MAIN] Display closed.  Shutting down...\n");
    Diagnostics::Info("Display closed. Shutting down.");
    Diagnostics::DumpStatus();
    ShutdownHeadlessRuntime("overlay_shutdown");

    std::printf("[MAIN] Goodbye.\n");
    return 0;
}
