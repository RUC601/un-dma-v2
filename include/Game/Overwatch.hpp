#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <mutex>
#include <ctime>
#include <utility>
#include <cstring>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>
#include "Utils/Diagnostics.hpp"
#include <process.h>
#include <DirectXMath.h>

#include "Game/AbilityIcons.hpp"
#include "Game/CameraProjection.hpp"
#include "game/camera/CameraWorker.hpp"
#include "game/entities/EntityWorkers.hpp"
#include "Game/AimFirePhase.hpp"
#include "Game/FlickRepeat.hpp"
#include "Game/AimStartLimiter.hpp"
#include "Game/BoneSlots.hpp"
#include "Game/EntityRosterPolicy.hpp"
#include "Game/EntityMaintenance/FieldDeadline.hpp"
#include "Game/EntityMaintenance/VisibilitySample.hpp"
#include "Game/EdOutline.hpp"
#include "Game/OutlineBrightness.hpp"
#include "Game/PresentationTiming.hpp"
#include "Utils/RuntimeTrace.hpp"
#include "Game/HeroProfileSwitch.hpp"
#include "Game/HeroPerkRuntime.hpp"
#include "Game/HeroSkills.hpp"
#include "Game/Motion.hpp"
#include "Game/Target.hpp"
#include "features/aim/BehaviorController.hpp"
#include "config/HeroProfileWorker.hpp"
#include "Game/snapshots/PresentationWorker.hpp"
#include "Game/WeaponCadence.hpp"
#include "output/MockBackend.hpp"
#include "Renderer/IconManager.hpp"
#include "Renderer/Renderer.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"
#include "Utils/ProcessConnection.hpp"
#include "Memory/KeyState.hpp"

using namespace OW;

// =========================================================================
// Global state variables
// =========================================================================

namespace OW {

    inline EntityTopologySample ProbeEntityTopologyMembers(bool readMemberIds);
    inline std::vector<std::pair<uint64_t,uint64_t>> ProbePriorityActor(uint64_t parent,const EntityTopologySample& topology,bool knownLink,
        std::vector<std::pair<uint64_t,uint64_t>>* bindingMembers,bool* bindingComplete);
    inline size_t ProbeEntityTopologyCandidateCount();
    inline bool DynamicEntityRegistryEnabled();
    inline EntityRegistry::State RefreshEntityRegistry(bool force);
    inline bool AcceptEntityRegistry(const EntityRegistry::Snapshot& snapshot);
    inline bool AcknowledgeEntityRegistry(const EntityRegistry::Snapshot& snapshot,EntityRegistry::ScanStatus status);

    inline std::string HeroDisplayNameToSlug(const std::string& displayName) {
        static const std::unordered_map<std::string, std::string> specialCases = {
            { "D.Mon", "dmon" },
            { "DMon", "dmon" },
            { "Soldier 76", "soldier-76" },
            { "Soldier76", "soldier-76" },
            { "D.Va", "dva" },
            { "DVa", "dva" },
            { "Dva", "dva" },
            { "Hana", "dva" },
            { "Wrecking Ball", "wrecking-ball" },
            { "WreckingBall", "wrecking-ball" },
            { "Junker Queen", "junker-queen" },
            { "JunkerQueen", "junker-queen" },
            { "JetpackCat", "jetpack-cat" },
            { "LifeWeaver", "lifeweaver" },
            { "Lifeweaver", "lifeweaver" },
            { "McCree", "cassidy" },
        };

        const auto special = specialCases.find(displayName);
        if (special != specialCases.end())
            return special->second;

        std::string slug;
        slug.reserve(displayName.size());
        bool previousWasHyphen = false;
        for (const unsigned char value : displayName) {
            if (value == '.' || value == '\'')
                continue;

            if (std::isspace(value) || value == '-' || value == '_') {
                if (!slug.empty() && !previousWasHyphen) {
                    slug.push_back('-');
                    previousWasHyphen = true;
                }
                continue;
            }

            slug.push_back(static_cast<char>(std::tolower(value)));
            previousWasHyphen = false;
        }

        if (!slug.empty() && slug.back() == '-')
            slug.pop_back();

        return slug;
    }

    // ---- View matrices (updated by viewmatrix_thread) ----
    inline uint64_t viewMatrixPtr = 0;
    inline uint64_t viewMatrix_xor_ptr = 0;
    inline Matrix viewMatrix{};
    inline Matrix viewMatrix_xor{};
    inline std::mutex g_viewMatrixMutex;
    inline ViewMatrixSampleState viewMatrixSampleState{};

    struct CriticalMatrixReadPlan {
        uint64_t cameraViewPtr = 0;
        uint64_t projectionPtr = 0;
        uint64_t generation = 0;
        uint64_t connectionEpoch = 0;
        bool valid = false;
    };

    inline CriticalMatrixReadPlan criticalMatrixReadPlan{};

    inline bool SetCriticalMatrixReadPlan(
        uint64_t cameraViewPtr,
        uint64_t projectionPtr,
        uint64_t expectedConnectionEpoch)
    {
        std::lock_guard<std::mutex> lock(g_viewMatrixMutex);
        if (!RuntimeWorkersRunning() || expectedConnectionEpoch == 0 ||
            !ProcessConnection::IsConnected() ||
            ProcessConnection::ConnectionEpoch() != expectedConnectionEpoch) {
            return false;
        }
        const uint64_t nextGeneration = criticalMatrixReadPlan.generation + 1;
        criticalMatrixReadPlan = {
            cameraViewPtr,
            projectionPtr,
            nextGeneration,
            expectedConnectionEpoch,
            cameraViewPtr != 0 && projectionPtr != 0 &&
                expectedConnectionEpoch != 0
        };
        return criticalMatrixReadPlan.valid;
    }

    inline CriticalMatrixReadPlan SnapshotCriticalMatrixReadPlan()
    {
        std::lock_guard<std::mutex> lock(g_viewMatrixMutex);
        return criticalMatrixReadPlan;
    }

    inline void ClearCriticalMatrixReadPlan()
    {
        std::lock_guard<std::mutex> lock(g_viewMatrixMutex);
        const uint64_t nextGeneration = criticalMatrixReadPlan.generation + 1;
        criticalMatrixReadPlan = {};
        criticalMatrixReadPlan.generation = nextGeneration;
    }

    inline void SetViewMatrices(const Matrix& vm, const Matrix& vmx) {
        std::lock_guard<std::mutex> lock(g_viewMatrixMutex);
        viewMatrix = vm;
        viewMatrix_xor = vmx;
        // This helper resets snapshots; only validated publication marks one fresh.
        viewMatrixSampleState = {};
    }

    inline void GetViewMatricesSnapshot(Matrix& vm, Matrix& vmx) {
        SnapshotViewMatrices(vm, vmx);
    }

    inline bool TryGetViewMatricesSnapshot(Matrix& vm, Matrix& vmx, uint64_t* sequence=nullptr) {
        return TrySnapshotViewMatrices(vm, vmx,sequence);
    }

    // ---- Entity containers ----
    inline std::vector<c_entity> entities{};
    inline std::vector<hpanddy> hp_dy_entities{};
    inline c_entity local_entity{};
    inline std::vector<c_entity> present_entities{};
    inline std::vector<hpanddy> present_hp_dy_entities{};
    inline std::mutex g_presentEntityMutex;
    inline std::vector<c_entity> present_render_entities{};
    inline std::vector<hpanddy> present_render_hp_dy_entities{};
    inline std::mutex g_presentRenderEntityMutex;

    // ---- Raw entity scan exchange buffer ----
    inline std::vector<std::pair<uint64_t, uint64_t>> ow_entities{};
    inline std::vector<std::pair<uint64_t, uint64_t>> ow_entities_scan{};
    inline EntityRegistry::Snapshot ow_entities_registry{},ow_entities_scan_registry{};
    inline bool ow_entities_scan_complete=false;
    inline uint64_t ow_entities_scene=0;

    struct RawEntityScanSnapshot {
        EntityRegistry::Snapshot registry{};
        std::vector<std::pair<uint64_t, uint64_t>> raw_entities{};
        std::vector<uint64_t> actual_members{};
        std::vector<std::pair<uint64_t, uint64_t>> membership_entities{};
        bool membership_known = false;
        uint64_t membership_revision = 0;
        std::vector<std::pair<uint64_t,uint64_t>> binding_entities{};
        bool binding_complete=false;
        uint64_t generation = 0;
        uint64_t connection_epoch = 0;
        uint64_t scene_generation = 0;
        std::vector<EntityRosterPolicy::Discovery> discoveries;
        uint8_t source = 1; // 1: full scan, 2: positive incremental discovery
        std::chrono::steady_clock::time_point scan_started_at{};
        std::chrono::steady_clock::time_point scan_finished_at{};
        bool valid = false;
        bool complete = false;
    };

    inline std::mutex raw_scan_mutex;
    inline RawEntityScanSnapshot latest_raw_scan_snapshot{};
    inline uint64_t last_consumed_raw_scan_generation = 0;
    inline uint64_t raw_scan_publication_sequence = 0;
    inline EntityTopologySample observed_entity_topology{};
    inline uint64_t observed_topology_epoch = 0, observed_topology_revision = 0;
    struct RawDiscoveryStamp { uint64_t link=0,epoch=0;DWORD tick=0; };
    inline std::unordered_map<uint64_t,RawDiscoveryStamp> raw_discovery_stamps;
    inline EntityRosterPolicy::DiscoveryBook entity_discoveries;
    inline EntityRosterPolicy::EmptySceneWatch empty_scene_watch;
    inline uint64_t entity_scene_clear_count = 0;
    inline uint64_t entity_seat_expired_count = 0;
    inline void RecordEntitySceneAudit(uint64_t epoch, uint64_t registry, uint64_t scene,
        EntityRosterPolicy::SceneAudit audit, DWORD tick) {
        std::lock_guard<std::mutex> lock(raw_scan_mutex);
        if (scene != EntitySceneGeneration() || epoch != ProcessConnection::ConnectionEpoch()) {
            ++rejectedEntitySceneResults; return;
        }
        if(DynamicEntityRegistryEnabled()) {
            std::lock_guard<std::mutex> registryLock(entityRegistryMonitor.mutex);
            const auto& state=entityRegistryMonitor.state;
            if(!state.available || registry!=state.snapshot.generation) {
                registry=state.snapshot.generation;audit=EntityRosterPolicy::SceneAudit::Unknown;
            }
        }
        empty_scene_watch.Observe(epoch, registry, scene, audit, tick);
    }
    inline void MergeRecentRawDiscovery(RawEntityScanSnapshot& sample) {
        const DWORD now=static_cast<DWORD>(EntityRegistryNowMs());
        // Reading the table does not give an empty player result deletion authority.
        sample.complete = sample.complete && !sample.discoveries.empty();
        entity_discoveries.SetContext(sample.connection_epoch, sample.registry.generation, sample.scene_generation);
        for (const auto& discovery : sample.discoveries) {
            entity_discoveries.Observe(discovery);RecordEntityBodyDiscovery(discovery);
        }
        entity_discoveries.Prune(now);
        for(const auto& discovery:sample.discoveries) empty_scene_watch.Observe(sample.connection_epoch,
            sample.registry.generation, sample.scene_generation, EntityRosterPolicy::SceneAudit::Present,discovery.tick);
        const bool sameRegistry=latest_raw_scan_snapshot.connection_epoch==sample.connection_epoch &&
            latest_raw_scan_snapshot.registry.generation==sample.registry.generation &&
            latest_raw_scan_snapshot.scene_generation==sample.scene_generation;
        if(!sameRegistry)raw_discovery_stamps.clear();
        if(!sample.complete && sample.source==1 && sameRegistry) {
            std::erase_if(sample.raw_entities,[&](const auto& incoming) {
                return std::find(latest_raw_scan_snapshot.membership_entities.begin(),latest_raw_scan_snapshot.membership_entities.end(),incoming)==latest_raw_scan_snapshot.membership_entities.end() &&
                    std::any_of(latest_raw_scan_snapshot.membership_entities.begin(),latest_raw_scan_snapshot.membership_entities.end(),[&](const auto& current) {
                    return current.second==incoming.second && current.first!=incoming.first;
                });
            });
            std::erase_if(sample.actual_members,[&](auto address) {
                return std::none_of(sample.raw_entities.begin(),sample.raw_entities.end(),[&](const auto& pair){return pair.first==address;});
            });
        }
        sample.membership_known=sample.complete ||
            (sameRegistry && latest_raw_scan_snapshot.membership_known);
        sample.membership_revision=(sameRegistry ? latest_raw_scan_snapshot.membership_revision : 0) +
            (sample.complete ? 1 : 0);
        sample.membership_entities=sameRegistry ? latest_raw_scan_snapshot.membership_entities :
            std::vector<std::pair<uint64_t,uint64_t>>{};
        if(sample.complete) {
            sample.membership_entities.clear();
            for(const auto& proof:sample.discoveries)sample.membership_entities.emplace_back(proof.address,proof.link);
        }
        if(!sample.complete) {
            // Fresh link resolution supersedes its former target set. Keep all
            // concurrently verified targets of that link, including legal pairs.
            if(sample.source==2 && sample.binding_complete)std::erase_if(sample.membership_entities,[&](const auto& old) {
                return std::any_of(sample.binding_entities.begin(),sample.binding_entities.end(),[&](const auto& pair){return old.second==pair.second;}) &&
                    std::find(sample.binding_entities.begin(),sample.binding_entities.end(),old)==sample.binding_entities.end();
            });
            for(const auto& pair:sample.binding_entities)
                if(std::find(sample.membership_entities.begin(),sample.membership_entities.end(),pair)==sample.membership_entities.end())
                    sample.membership_entities.push_back(pair);
            for(const auto& pair:sample.raw_entities) {
                if(std::find(sample.membership_entities.begin(),sample.membership_entities.end(),pair)==sample.membership_entities.end())
                    sample.membership_entities.push_back(pair);
            }
        }
        if(sample.membership_known) {
            std::lock_guard<std::mutex> bodyLock(entityBodyValidityMutex);
            for(const auto& [address,evidence]:entityBodyDiscoveries) {
                const bool missing=
                    std::find(sample.membership_entities.begin(),sample.membership_entities.end(),
                        std::make_pair(address,evidence.link))==sample.membership_entities.end();
                if(missing) {
                    activeEntityBodies.erase(address);
                    if(!blockedEntityBodies.contains(address)) {
                        blockedEntityBodies[address]=now;++entityBodyAbsenceRevisions[address];
                    }
                }
            }
        }
        if(sample.complete)raw_discovery_stamps.clear();
        for(const auto& pair:sample.raw_entities)raw_discovery_stamps[pair.first]={pair.second,sample.connection_epoch,now};
        if(!sample.complete && sameRegistry) {
            for(const auto& pair:latest_raw_scan_snapshot.raw_entities) {
                const auto stamp=raw_discovery_stamps.find(pair.first);
                if(stamp!=raw_discovery_stamps.end() && stamp->second.epoch==sample.connection_epoch &&
                    stamp->second.link==pair.second && now-stamp->second.tick<=20000 &&
                    std::find(sample.raw_entities.begin(),sample.raw_entities.end(),pair)==sample.raw_entities.end())
                    sample.raw_entities.push_back(pair);
            }
        }
        for(auto it=raw_discovery_stamps.begin();it!=raw_discovery_stamps.end();)
            if(it->second.epoch!=sample.connection_epoch || now-it->second.tick>20000)it=raw_discovery_stamps.erase(it);else ++it;
        // Candidate retention expiring is not proof of an actor's absence.
        // Only a complete audit or a freshly verified binding removes membership.
    }

    // ---- Target/source screen used for projection ----
    inline float WX = 0.f, WY = 0.f;
    inline int detectedScreenWidth = 0;
    inline int detectedScreenHeight = 0;

    // ---- Scan coordination ----
    inline int abletotread = 0;
    inline int howbigentitysize = 0;
    inline constexpr DWORD kEntityScanIntervalMs = 250;
    inline constexpr DWORD kEntityEmptyScanIntervalMs = 50;
    inline constexpr DWORD kEntityProcessIntervalMs = 12;
    inline constexpr DWORD kEntitySlowFieldIntervalMs = 500;
    inline constexpr DWORD kEntityTeamNameIntervalMs = 1000;
    inline constexpr DWORD kEntitySkillStatusIntervalMs = 1000;
    inline constexpr DWORD kEntityLocalSkillIntervalMs = kEntitySlowFieldIntervalMs;
    inline constexpr DWORD kEntityHealthIntervalMs = 16;
    inline constexpr DWORD kEntityHeroIntervalMs = 50;
    inline constexpr DWORD kEntityRosterTtlMs = EntityRosterPolicy::kRetentionMs;
    inline constexpr DWORD kCnEntityScanStableHoldMs = kEntityRosterTtlMs;
    inline constexpr DWORD kCnEntityRosterFreshGraceMs = PresentationTiming::kAnchorFreshMs;
    inline constexpr size_t kCnEntityScanStableMaxPairs = 128;
    inline constexpr DWORD kEntityDeadComponentRefreshMs = 250;
    inline constexpr DWORD kEntityLiveComponentRefreshMs = 2000;
    inline constexpr DWORD kEntityFastRescanWindowMs = 2000;
    inline constexpr DWORD kEntityTopologyRescanCooldownMs = 250;
    inline constexpr DWORD kEntityRespawnRescanDelayMs = 250;
    inline constexpr DWORD kEntityRespawnRescanRetryMs = 250;
    inline constexpr DWORD kEntityRespawnWatchMs = 20000;
    inline constexpr DWORD kEntityScannerIdleSleepMs = 5;
    inline DWORD entity_fast_scan_until_tick = 0;
    inline uint64_t entity_topology_rescan_request_count = 0;
    inline uint64_t entity_topology_rescan_completed_request_count = 0;
    inline std::atomic<uint32_t> entity_scan_dma_active_depth{0};
    inline std::atomic<DWORD> entity_scan_next_due_tick{0};
    inline std::atomic<uint64_t> viewmatrix_scan_backoff_count{0};
    inline std::atomic<uint64_t> viewmatrix_scan_due_guard_count{0};

    struct ScopedEntityScanDmaActivity {
        ScopedEntityScanDmaActivity() {
            entity_scan_dma_active_depth.fetch_add(1, std::memory_order_relaxed);
        }

        ~ScopedEntityScanDmaActivity() {
            entity_scan_dma_active_depth.fetch_sub(1, std::memory_order_relaxed);
        }

        ScopedEntityScanDmaActivity(const ScopedEntityScanDmaActivity&) = delete;
        ScopedEntityScanDmaActivity& operator=(const ScopedEntityScanDmaActivity&) = delete;
    };

    inline bool IsEntityScanDmaActive() {
        return entity_scan_dma_active_depth.load(std::memory_order_relaxed) != 0;
    }

    inline void SetEntityScanNextDueTick(DWORD tick) {
        entity_scan_next_due_tick.store(tick, std::memory_order_relaxed);
    }

    inline DWORD EntityScanNextDueTick() {
        return entity_scan_next_due_tick.load(std::memory_order_relaxed);
    }

    inline DWORD EntityScanDueGuardSleepMs(DWORD now, DWORD guardMs) {
        if (guardMs == 0)
            return 0;

        const DWORD dueTick = EntityScanNextDueTick();
        if (dueTick == 0)
            return 0;

        const LONG deltaMs = static_cast<LONG>(dueTick - now);
        if (deltaMs > static_cast<LONG>(guardMs))
            return 0;
        if (deltaMs < -static_cast<LONG>(guardMs))
            return 0;

        if (deltaMs <= 0)
            return 1;

        return static_cast<DWORD>((std::min)(static_cast<unsigned long>(guardMs),
            static_cast<unsigned long>(deltaMs)));
    }

    inline void RecordViewMatrixScanBackoff() {
        viewmatrix_scan_backoff_count.fetch_add(1, std::memory_order_relaxed);
    }

    inline void RecordViewMatrixScanDueGuard() {
        viewmatrix_scan_due_guard_count.fetch_add(1, std::memory_order_relaxed);
    }

    inline uint64_t ViewMatrixScanBackoffCount() {
        return viewmatrix_scan_backoff_count.load(std::memory_order_relaxed);
    }

    inline uint64_t ViewMatrixScanDueGuardCount() {
        return viewmatrix_scan_due_guard_count.load(std::memory_order_relaxed);
    }

    inline Vector2 ResolveScreenSize()
    {
        if (detectedScreenWidth > 0 && detectedScreenHeight > 0) {
            return Vector2(
                static_cast<float>(detectedScreenWidth),
                static_cast<float>(detectedScreenHeight));
        }

        if (Config::manualScreenWidth > 0 && Config::manualScreenHeight > 0) {
            return Vector2(
                static_cast<float>(Config::manualScreenWidth),
                static_cast<float>(Config::manualScreenHeight));
        }

        return Vector2(
            static_cast<float>(GetSystemMetrics(SM_CXSCREEN)),
            static_cast<float>(GetSystemMetrics(SM_CYSCREEN)));
    }

    inline float ResolveScreenWidth()
    {
        return ResolveScreenSize().X;
    }

    inline float ResolveScreenHeight()
    {
        return ResolveScreenSize().Y;
    }

    inline void RefreshScreenSizeFromConfig()
    {
        const Vector2 screenSize = ResolveScreenSize();
        WX = screenSize.X;
        WY = screenSize.Y;
    }

    inline void SetDetectedScreenSize(int width, int height)
    {
        detectedScreenWidth = width > 0 ? width : 0;
        detectedScreenHeight = height > 0 ? height : 0;
        RefreshScreenSizeFromConfig();
    }

    inline void ClearDetectedScreenSize()
    {
        detectedScreenWidth = 0;
        detectedScreenHeight = 0;
        RefreshScreenSizeFromConfig();
    }

    inline bool PipelineDebugEnabled()
    {
        return Config::outputDebugLog;
    }

    inline int ReadEnvFlagState(const char* name)
    {
        char buffer[16] = {};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return -1;
        return buffer[0] != '0' &&
            buffer[0] != 'n' &&
            buffer[0] != 'N' &&
            buffer[0] != 'f' &&
            buffer[0] != 'F'
            ? 1
            : 0;
    }

    inline DWORD ReadEnvDword(const char* name, DWORD fallback, DWORD minValue, DWORD maxValue)
    {
        char buffer[32] = {};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return fallback;

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(buffer, &end, 10);
        if (end == buffer || (end && *end != '\0'))
            return fallback;

        const DWORD value = static_cast<DWORD>(parsed);
        return std::clamp(value, minValue, maxValue);
    }

    inline bool EnvFlagEnabled(const char* name)
    {
        return ReadEnvFlagState(name) == 1;
    }

    inline bool CriticalBatchFusionEnabled()
    {
        static const int flag = ReadEnvFlagState("UN_DMA_CRITICAL_BATCH_FUSION");
        static const bool enabled = flag != 0;
        return enabled;
    }

    inline bool CriticalBatchFusionActive()
    {
        // Chain selection is a version-profile decision. The environment flag
        // remains diagnostic only and cannot change the memory layout used by
        // a running version.
        return offset::Active().viewMatrixChain ==
            offset::ViewMatrixChainMode::CameraProjectionShort;
    }

    inline bool SteadyEntitySchedulerEnabled()
    {
        static const int flag = ReadEnvFlagState("UN_DMA_STEADY_ENTITY_SCHEDULER");
        static const bool enabled = flag != 0;
        return enabled;
    }

    inline bool EntityPipelineV2Enabled()
    {
        static const int flag = ReadEnvFlagState("UN_DMA_ENTITY_PIPELINE_V2");
        static const bool enabled = flag != 0;
        return enabled;
    }

    inline bool ScanLatestWinsEnabled()
    {
        static const int scanFlag = ReadEnvFlagState("UN_DMA_SCAN_LATEST_WINS");
        if (scanFlag >= 0)
            return scanFlag == 1;
        return EntityPipelineV2Enabled();
    }

    inline bool ColdTopologyScanEnabled()
    {
        static const int scanFlag = ReadEnvFlagState("UN_DMA_COLD_TOPOLOGY_SCAN");
        if (scanFlag >= 0)
            return scanFlag == 1;
        return EntityPipelineV2Enabled();
    }

    inline DWORD EntitySoftRefreshGapMs()
    {
        static const DWORD value =
            ReadEnvDword("UN_DMA_ENTITY_SOFT_REFRESH_GAP_MS", 0, 0, 10000);
        return value;
    }

    inline DWORD EntityHardRescanGapMs()
    {
        static const DWORD value =
            ReadEnvDword("UN_DMA_ENTITY_HARD_RESCAN_GAP_MS", 0, 0, 10000);
        if (value == 0)
            return 0;
        const DWORD soft = EntitySoftRefreshGapMs();
        return soft > 0 && value < soft ? soft : value;
    }

    inline DWORD EntityScanMissGraceCount()
    {
        static const DWORD value =
            ReadEnvDword("UN_DMA_ENTITY_SCAN_MISS_GRACE_COUNT", 0, 0, 16);
        return value;
    }

    inline bool IsMatrixNonIdentity(const Matrix& matrix)
    {
        const float values[] = {
            matrix.m11, matrix.m12, matrix.m13, matrix.m14,
            matrix.m21, matrix.m22, matrix.m23, matrix.m24,
            matrix.m31, matrix.m32, matrix.m33, matrix.m34,
            matrix.m41, matrix.m42, matrix.m43, matrix.m44
        };
        const float identity[] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        bool differsFromIdentity = false;
        bool hasNonZeroValue = false;
        for (size_t index = 0; index < 16; ++index) {
            if (!std::isfinite(values[index]))
                return false;
            if (std::fabs(values[index]) > 0.0001f)
                hasNonZeroValue = true;
            if (std::fabs(values[index] - identity[index]) > 0.001f)
                differsFromIdentity = true;
        }
        return hasNonZeroValue && differsFromIdentity;
    }

    inline bool IsCameraViewMatrixPlausible(const Matrix& matrix)
    {
        if (!IsMatrixNonIdentity(matrix))
            return false;

        const float r11 = matrix.m11, r12 = matrix.m12, r13 = matrix.m13;
        const float r21 = matrix.m21, r22 = matrix.m22, r23 = matrix.m23;
        const float r31 = matrix.m31, r32 = matrix.m32, r33 = matrix.m33;
        const auto rowLengthSq = [](float x, float y, float z) { return x * x + y * y + z * z; };
        const auto dot = [](float ax, float ay, float az, float bx, float by, float bz) {
            return ax * bx + ay * by + az * bz;
        };
        if (!std::isfinite(matrix.m44) || std::fabs(matrix.m44 - 1.0f) > 0.35f ||
            rowLengthSq(r11, r12, r13) < 0.4225f || rowLengthSq(r11, r12, r13) > 1.8225f ||
            rowLengthSq(r21, r22, r23) < 0.4225f || rowLengthSq(r21, r22, r23) > 1.8225f ||
            rowLengthSq(r31, r32, r33) < 0.4225f || rowLengthSq(r31, r32, r33) > 1.8225f ||
            std::fabs(dot(r11, r12, r13, r21, r22, r23)) > 0.25f ||
            std::fabs(dot(r11, r12, r13, r31, r32, r33)) > 0.25f ||
            std::fabs(dot(r21, r22, r23, r31, r32, r33)) > 0.25f ||
            std::fabs(matrix.m41) > 10000.0f || std::fabs(matrix.m42) > 10000.0f ||
            std::fabs(matrix.m43) > 10000.0f)
            return false;

        const DirectX::XMFLOAT3 camera = matrix.get_location();
        const DirectX::XMFLOAT3 forward = matrix.get_rotation();
        const float cameraLengthSq = camera.x * camera.x + camera.y * camera.y + camera.z * camera.z;
        const float forwardLengthSq = forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;

        return std::isfinite(camera.x) && std::isfinite(camera.y) && std::isfinite(camera.z) &&
            std::isfinite(forward.x) && std::isfinite(forward.y) && std::isfinite(forward.z) &&
            cameraLengthSq > 0.000001f && cameraLengthSq < 1000000000000.0f &&
            forwardLengthSq > 0.0625f && forwardLengthSq < 4.0f;
    }

    struct ViewMatrixPublishState {
        bool hasPublished = false;
        Matrix lastPublished{};
        bool hasPublishedCamera = false;
        Matrix lastPublishedCamera{};
        Matrix pendingJump{};
        PresentationTiming::JumpConfirmation jumpConfirmation{};
    };

    inline float MaxMatrixElementAbs(const Matrix& matrix)
    {
        const float values[] = {
            matrix.m11, matrix.m12, matrix.m13, matrix.m14,
            matrix.m21, matrix.m22, matrix.m23, matrix.m24,
            matrix.m31, matrix.m32, matrix.m33, matrix.m34,
            matrix.m41, matrix.m42, matrix.m43, matrix.m44
        };

        float result = 0.0f;
        for (const float value : values) {
            const float absValue = std::fabs(value);
            if (absValue > result)
                result = absValue;
        }
        return result;
    }

    inline float MaxMatrixElementDelta(const Matrix& lhs, const Matrix& rhs)
    {
        const float left[] = {
            lhs.m11, lhs.m12, lhs.m13, lhs.m14,
            lhs.m21, lhs.m22, lhs.m23, lhs.m24,
            lhs.m31, lhs.m32, lhs.m33, lhs.m34,
            lhs.m41, lhs.m42, lhs.m43, lhs.m44
        };
        const float right[] = {
            rhs.m11, rhs.m12, rhs.m13, rhs.m14,
            rhs.m21, rhs.m22, rhs.m23, rhs.m24,
            rhs.m31, rhs.m32, rhs.m33, rhs.m34,
            rhs.m41, rhs.m42, rhs.m43, rhs.m44
        };

        float result = 0.0f;
        for (size_t index = 0; index < 16; ++index) {
            const float delta = std::fabs(left[index] - right[index]);
            if (delta > result)
                result = delta;
        }
        return result;
    }

    inline bool IsRenderViewProjectionPlausible(const Matrix& matrix)
    {
        if (!IsMatrixNonIdentity(matrix))
            return false;

        const float maxAbs = MaxMatrixElementAbs(matrix);
        if (!std::isfinite(maxAbs) || maxAbs > 10000000.0f)
            return false;

        const bool hasPerspectiveW =
            std::fabs(matrix.m14) > 0.00001f ||
            std::fabs(matrix.m24) > 0.00001f ||
            std::fabs(matrix.m34) > 0.00001f;
        const bool hasProjectionScale =
            std::fabs(matrix.m11) > 0.00001f ||
            std::fabs(matrix.m22) > 0.00001f;
        return hasPerspectiveW && hasProjectionScale;
    }

    inline bool IsViewProjectionLargeJump(const Matrix& previous, const Matrix& candidate)
    {
        const float delta = MaxMatrixElementDelta(previous, candidate);
        float scale = MaxMatrixElementAbs(previous);
        const float candidateScale = MaxMatrixElementAbs(candidate);
        if (candidateScale > scale)
            scale = candidateScale;
        if (scale < 1.0f)
            scale = 1.0f;
        const float threshold = std::clamp(scale * 0.75f, 8.0f, 2048.0f);
        return std::isfinite(delta) && delta > threshold;
    }

    inline bool IsDirectViewProjectionComposedMismatch(
        const Matrix& direct,
        const Matrix& composed,
        float& delta)
    {
        delta = MaxMatrixElementDelta(direct, composed);
        float scale = MaxMatrixElementAbs(direct);
        const float composedScale = MaxMatrixElementAbs(composed);
        if (composedScale > scale)
            scale = composedScale;
        if (scale < 1.0f)
            scale = 1.0f;
        const float threshold = std::clamp(scale * 0.01f, 0.05f, 12.0f);
        return std::isfinite(delta) && delta > threshold;
    }

    inline bool TryPublishViewMatrices(
        const Matrix& renderViewProjection,
        const Matrix& cameraViewMatrix,
        bool cameraViewValid,
        bool requireFreshCameraView,
        uint64_t expectedConnectionEpoch,
        ViewMatrixPublishState& state,
        Matrix& publishedCameraView,
        const char*& rejectReason,
        uint64_t readStartedMs = 0, uint64_t readCompletedMs = 0)
    {
        rejectReason = nullptr;
        if (readCompletedMs == 0) readCompletedMs = GetTickCount64();
        if (readStartedMs == 0) readStartedMs = readCompletedMs;
        if (!PresentationTiming::ObservationWindowFresh(readStartedMs,readCompletedMs,GetTickCount64(),kViewMatrixSampleGraceMs)) {
            rejectReason = "camera read span expired";
            RuntimeTrace::Emit(RuntimeTrace::CameraReject, 1, readStartedMs, readCompletedMs);
            return false;
        }
        if (!RuntimeWorkersRunning() || expectedConnectionEpoch == 0 ||
            !ProcessConnection::IsConnected() ||
            ProcessConnection::ConnectionEpoch() != expectedConnectionEpoch) {
            rejectReason = "connection epoch changed";
            RuntimeTrace::Emit(RuntimeTrace::CameraReject,2,expectedConnectionEpoch);
            return false;
        }
        if (!IsRenderViewProjectionPlausible(renderViewProjection)) {
            state.jumpConfirmation.Reset();
            rejectReason = "render view-projection implausible";
            RuntimeTrace::Emit(RuntimeTrace::CameraReject,4,0,0,0,&renderViewProjection.m11,16);
            return false;
        }

        if (requireFreshCameraView && !cameraViewValid) {
            state.jumpConfirmation.Reset();
            rejectReason = "fresh camera view invalid";
            RuntimeTrace::Emit(RuntimeTrace::CameraReject,5,0,0,0,&cameraViewMatrix.m11,16);
            return false;
        }

        const float publishedDelta = state.hasPublished
            ? MaxMatrixElementDelta(state.lastPublished, renderViewProjection)
            : 0.0f;
        const bool largeJump = state.hasPublished &&
            IsViewProjectionLargeJump(state.lastPublished, renderViewProjection);
        if (largeJump) {
            const bool cameraTracksJump =
                cameraViewValid &&
                state.hasPublishedCamera &&
                IsViewProjectionLargeJump(state.lastPublishedCamera, cameraViewMatrix);
            const DWORD now = GetTickCount();
            const bool sameCandidate = state.jumpConfirmation.pending &&
                !IsViewProjectionLargeJump(state.pendingJump, renderViewProjection);
            const bool confirmed = cameraTracksJump ||
                state.jumpConfirmation.Observe(sameCandidate, now);
            state.pendingJump = renderViewProjection;
            if (!confirmed) {
                Diagnostics::RecordViewMatrixStability(false, true, publishedDelta);
                rejectReason = "render view-projection jump pending stability";
                RuntimeTrace::Emit(RuntimeTrace::CameraReject,6,readStartedMs,readCompletedMs,expectedConnectionEpoch,&renderViewProjection.m11,16);
                return false;
            }

            Diagnostics::RecordViewMatrixStability(true, false, publishedDelta);
        } else {
            state.jumpConfirmation.Reset();
        }

        {
            std::lock_guard<std::mutex> lock(g_viewMatrixMutex);
            if (!RuntimeWorkersRunning() || !ProcessConnection::IsConnected() ||
                ProcessConnection::ConnectionEpoch() != expectedConnectionEpoch) {
                rejectReason = "connection epoch changed before publish";
                RuntimeTrace::Emit(RuntimeTrace::CameraReject,7,expectedConnectionEpoch);
                return false;
            }
            viewMatrix = renderViewProjection;
            if (cameraViewValid)
                viewMatrix_xor = cameraViewMatrix;
            publishedCameraView = viewMatrix_xor;
            static uint64_t nextCameraSequence=0;
            const auto sequence = ++nextCameraSequence;
            viewMatrixSampleState = {cameraViewValid, readStartedMs, expectedConnectionEpoch,
                readStartedMs, readCompletedMs, sequence};
            RuntimeTrace::Emit(RuntimeTrace::CameraPublish, sequence, readStartedMs, readCompletedMs,
                expectedConnectionEpoch, &renderViewProjection.m11, 16);
        }
        Diagnostics::RecordViewMatrixPublish();

        state.hasPublished = true;
        state.lastPublished = renderViewProjection;
        if (cameraViewValid) {
            state.hasPublishedCamera = true;
            state.lastPublishedCamera = cameraViewMatrix;
        }
        state.jumpConfirmation.Reset();
        return true;
    }

    inline void RecordViewMatrixUnresolved(const char* reason, uint64_t value, DWORD& lastLogTick)
    {
        RuntimeTrace::Emit(RuntimeTrace::CameraReject,3,value);
        Diagnostics::SetViewMatrixStatus(false, false);
        if (!PipelineDebugEnabled())
            return;

        const DWORD now = GetTickCount();
        if (lastLogTick == 0 || now - lastLogTick >= 1000) {
            Diagnostics::Info("[PIPELINE] Stage 2 view matrix unresolved: %s value=0x%llX.",
                reason ? reason : "unknown",
                static_cast<unsigned long long>(value));
            lastLogTick = now;
        }
    }

    inline void RecordViewMatrixResolved(
        uint64_t renderViewProjectionPtr,
        uint64_t cameraViewPtr,
        bool valid,
        bool& hasLastStatus,
        bool& lastValid,
        DWORD& lastLogTick)
    {
        Diagnostics::SetViewMatrixStatus(true, valid);
        if (!PipelineDebugEnabled())
            return;

        const DWORD now = GetTickCount();
        const bool changed = !hasLastStatus || lastValid != valid;
        if (changed || lastLogTick == 0 || now - lastLogTick >= 1000) {
            Diagnostics::Info("[PIPELINE] Stage 2 view matrix %s renderVP=0x%llX cameraView=0x%llX.",
                valid ? "valid" : "zero/invalid",
                static_cast<unsigned long long>(renderViewProjectionPtr),
                static_cast<unsigned long long>(cameraViewPtr));
            hasLastStatus = true;
            lastValid = valid;
            lastLogTick = now;
        }
    }

    inline void RecordViewMatrixRejected(
        const char* reason,
        uint64_t renderViewProjectionPtr,
        uint64_t cameraViewPtr,
        bool& hasLastStatus,
        bool& lastValid,
        DWORD& lastLogTick)
    {
        Diagnostics::SetViewMatrixStatus(true, false);
        if (!PipelineDebugEnabled())
            return;

        const DWORD now = GetTickCount();
        const bool changed = !hasLastStatus || lastValid;
        if (changed || lastLogTick == 0 || now - lastLogTick >= 1000) {
            Diagnostics::Info("[PIPELINE] Stage 2 view matrix rejected: %s renderVP=0x%llX cameraView=0x%llX.",
                reason ? reason : "unknown",
                static_cast<unsigned long long>(renderViewProjectionPtr),
                static_cast<unsigned long long>(cameraViewPtr));
            hasLastStatus = true;
            lastValid = false;
            lastLogTick = now;
        }
    }

    inline bool ProjectRowMajorToScreen(
        const Matrix& matrix,
        const Vector3& world,
        const Vector2& window,
        float& x,
        float& y)
    {
        if (window.X <= 0.0f || window.Y <= 0.0f)
            return false;
        if (!std::isfinite(world.X) || !std::isfinite(world.Y) || !std::isfinite(world.Z))
            return false;

        const float clipX = matrix.m11 * world.X + matrix.m12 * world.Y + matrix.m13 * world.Z + matrix.m14;
        const float clipY = matrix.m21 * world.X + matrix.m22 * world.Y + matrix.m23 * world.Z + matrix.m24;
        const float clipW = matrix.m41 * world.X + matrix.m42 * world.Y + matrix.m43 * world.Z + matrix.m44;
        if (clipW < 0.001f || std::fabs(clipW) < 0.001f)
            return false;

        const float ndcX = clipX / clipW;
        const float ndcY = clipY / clipW;
        if (!std::isfinite(ndcX) || !std::isfinite(ndcY))
            return false;

        x = (ndcX + 1.0f) * 0.5f * window.X;
        y = (1.0f - ndcY) * 0.5f * window.Y;
        return x >= 0.0f && y >= 0.0f && x < window.X && y < window.Y;
    }

    inline Matrix TransposeMatrix(const Matrix& matrix)
    {
        Matrix transposed{};
        transposed.m11 = matrix.m11; transposed.m12 = matrix.m21; transposed.m13 = matrix.m31; transposed.m14 = matrix.m41;
        transposed.m21 = matrix.m12; transposed.m22 = matrix.m22; transposed.m23 = matrix.m32; transposed.m24 = matrix.m42;
        transposed.m31 = matrix.m13; transposed.m32 = matrix.m23; transposed.m33 = matrix.m33; transposed.m34 = matrix.m43;
        transposed.m41 = matrix.m14; transposed.m42 = matrix.m24; transposed.m43 = matrix.m34; transposed.m44 = matrix.m44;
        return transposed;
    }

    struct DynamicWorldMatrixSelection {
        bool valid = false;
        uint64_t offset = 0;
        uint64_t address = 0;
        size_t hits = 0;
        size_t saneHeights = 0;
        float avgHeight = 0.0f;
        float score = 0.0f;
        Matrix rowMajor{};
    };

    inline bool ScoreWorld151177RowMajorMatrix(
        const Matrix& matrix,
        uint64_t offset,
        uint64_t decoded,
        const std::vector<c_entity>& entities,
        const c_entity& local,
        const Vector2& window,
        DynamicWorldMatrixSelection& score)
    {
        if (!IsMatrixNonIdentity(matrix))
            return false;

        size_t hits = 0;
        size_t saneHeights = 0;
        float heightSum = 0.0f;
        for (const c_entity& entity : entities) {
            if (!entity.team_known || !entity.Alive || !entity.Team)
                continue;
            if (!std::isfinite(entity.pos.X) || !std::isfinite(entity.head_pos.X))
                continue;
            if (std::isfinite(local.pos.X) && entity.pos.DistTo(local.pos) > 150.0f)
                continue;

            float headX = 0.0f, headY = 0.0f;
            float rootX = 0.0f, rootY = 0.0f;
            float chestX = 0.0f, chestY = 0.0f;
            if (!ProjectRowMajorToScreen(matrix, entity.head_pos, window, headX, headY) ||
                !ProjectRowMajorToScreen(matrix, entity.pos, window, rootX, rootY) ||
                !ProjectRowMajorToScreen(matrix, entity.chest_pos, window, chestX, chestY)) {
                continue;
            }

            const float height = std::fabs(rootY - headY);
            if (!std::isfinite(height) || height <= 0.0f)
                continue;
            ++hits;
            if (height >= 8.0f && height <= 180.0f)
                ++saneHeights;
            heightSum += height;
        }

        if (hits == 0)
            return false;

        score.valid = true;
        score.offset = offset;
        score.address = decoded + offset;
        score.hits = hits;
        score.saneHeights = saneHeights;
        score.avgHeight = heightSum / static_cast<float>(hits);
        score.score =
            static_cast<float>(hits) * 1000.0f +
            static_cast<float>(saneHeights) * 100.0f +
            std::clamp(score.avgHeight, 0.0f, 120.0f);
        score.rowMajor = matrix;
        return true;
    }

    inline bool TrySelectWorld151177DynamicViewProjection(
        uint64_t decoded,
        Matrix& selectedViewProjection,
        uint64_t& selectedPtr)
    {
        static DWORD lastSelectTick = 0;
        static uint64_t lastDecoded = 0;
        static DynamicWorldMatrixSelection lastSelection{};
        static Matrix lastPublished{};

        const DWORD now = GetTickCount();
        auto reuseLastSelection = [&]() -> bool {
            if (!lastSelection.valid || decoded != lastDecoded)
                return false;
            selectedViewProjection = lastPublished;
            selectedPtr = lastSelection.address;
            lastSelectTick = now;
            return true;
        };

        if (lastSelection.valid && decoded == lastDecoded && now - lastSelectTick < 16)
            return reuseLastSelection();

        if (!SDK || !SDK->IsInitialized() || decoded == 0)
            return reuseLastSelection();

        constexpr uint64_t kSweepStart = 0x210;
        constexpr uint64_t kSweepEnd = 0x1430;
        constexpr uint64_t kSweepStride = 0x50;
        constexpr size_t kSweepBytes =
            static_cast<size_t>((kSweepEnd - kSweepStart) + sizeof(Matrix));

        std::array<std::byte, kSweepBytes> bytes{};
        if (!SDK->read_range(decoded + kSweepStart, bytes.data(), bytes.size()))
            return reuseLastSelection();

        const c_entity local = TargetingDetail::SnapshotLocalEntity();
        const std::vector<c_entity> entities = TargetingDetail::SnapshotEntities();
        if (entities.empty())
            return reuseLastSelection();

        const float width = WX > 0.0f
            ? WX
            : (Config::manualScreenWidth > 0 ? static_cast<float>(Config::manualScreenWidth) : 1920.0f);
        const float height = WY > 0.0f
            ? WY
            : (Config::manualScreenHeight > 0 ? static_cast<float>(Config::manualScreenHeight) : 1080.0f);
        const Vector2 window(width, height);

        DynamicWorldMatrixSelection best{};
        DynamicWorldMatrixSelection previousNow{};
        for (uint64_t offset = kSweepStart; offset <= kSweepEnd; offset += kSweepStride) {
            Matrix matrix{};
            std::memcpy(&matrix, bytes.data() + static_cast<size_t>(offset - kSweepStart), sizeof(Matrix));
            DynamicWorldMatrixSelection candidate{};
            if (!ScoreWorld151177RowMajorMatrix(matrix, offset, decoded, entities, local, window, candidate))
                continue;
            if (!best.valid || candidate.score > best.score)
                best = candidate;
            if (lastSelection.valid && decoded == lastDecoded && offset == lastSelection.offset)
                previousNow = candidate;
        }

        if (!best.valid)
            return reuseLastSelection();

        if (previousNow.valid && previousNow.score + 250.0f >= best.score)
            best = previousNow;

        lastSelection = best;
        lastDecoded = decoded;
        lastSelectTick = now;
        lastPublished = TransposeMatrix(best.rowMajor);
        selectedViewProjection = lastPublished;
        selectedPtr = best.address;
        return true;
    }
} // namespace OW

inline std::mutex g_mutex;

inline bool ClearEntityScene(uint64_t expectedScene,uint64_t expectedConnection) {
    std::lock_guard<std::mutex> producerLock(OW::RuntimeOutputProducerTransitionMutex());
    {
        std::scoped_lock lock(g_mutex,OW::raw_scan_mutex,OW::g_presentEntityMutex,
            OW::g_presentRenderEntityMutex,OW::entityBodyValidityMutex,OW::entityRegistryMonitor.mutex);
        if(OW::EntitySceneGeneration()!=expectedScene || OW::ProcessConnection::ConnectionEpoch()!=expectedConnection ||
            !OW::empty_scene_watch.ready || (OW::DynamicEntityRegistryEnabled() &&
                (!OW::entityRegistryMonitor.state.available ||
                 OW::empty_scene_watch.registry!=OW::entityRegistryMonitor.state.snapshot.generation))) return false;
        const auto nextScene=OW::entitySceneGeneration.fetch_add(1,std::memory_order_acq_rel)+1;
        OW::entities.clear();OW::hp_dy_entities.clear();OW::local_entity={};
        OW::PublishFrame(std::make_shared<OW::ConsumptionFrame>());
        OW::present_entities.clear();OW::present_hp_dy_entities.clear();
        OW::present_render_entities.clear();OW::present_render_hp_dy_entities.clear();
        OW::ClearEntityBodyAuthoritiesLocked();OW::entityBodyDiscoveries.clear();OW::blockedEntityBodies.clear();
        OW::entityBodyAbsenceRevisions.clear();
        OW::ow_entities.clear();OW::ow_entities_scan.clear();
        OW::ow_entities_registry={};OW::ow_entities_scan_registry={};OW::abletotread=0;
        OW::latest_raw_scan_snapshot={};OW::raw_discovery_stamps.clear();
        OW::entity_discoveries={};OW::observed_entity_topology={};OW::observed_topology_epoch=0;
        ++OW::observed_topology_revision;
        OW::empty_scene_watch.MarkCleared(nextScene);++OW::entity_scene_clear_count;
        OW::entity_fast_scan_until_tick=0;
        OW::TargetingDetail::SetPublishedEntityCount(0);Diagnostics::SetEntityCount(0);
    }
    OW::TargetingDetail::ResetTargetLockRuntime();
    OW::InvalidateRuntimeOutputContinuity();
    {
        std::lock_guard<std::mutex> lock(OW::TargetingDetail::velocity_history_mutex);
        OW::TargetingDetail::velocity_history.clear();
    }
    OW::CancelActiveSkill();
    OW::RuntimeOutputScheduler().CancelAllAndJoin(OW::OutputActionCancelReason::RuntimeChanged);
    Diagnostics::Info("Entity scene cleared: reason=confirmed_empty_13000ms scene=%llu.",OW::EntitySceneGeneration());
    return true;
}

// =========================================================================
// Entity scan thread (lightweight, just calls get_ow_entities)
// =========================================================================

// This worker uses the existing memory session. A long full audit cannot delay
// membership observation or positive discovery of a newly allocated player.
// =========================================================================
// Entity processing thread (decrypts components, builds c_entity list)
// =========================================================================

// =========================================================================
// View matrix reader thread
// =========================================================================

// =========================================================================
// ESP rendering helpers (require ImGui and Render:: namespace)
// =========================================================================
