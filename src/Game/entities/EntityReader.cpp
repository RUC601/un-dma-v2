#include "Game/EntityMaintenance/HotFieldReads.hpp"
#include "Game/ParticipantIdentity.hpp"
#include "Game/camera/CameraWorker.hpp"
#include "../../runtime/Acquisition.hpp"
#include "EntityWorkers.hpp"
#include "Game/Overwatch.hpp"
#include "Game/map/MapReader.hpp"
#include "Game/SkillCooldownTimers.hpp"
#include "Alerts/Settings.hpp"
#include "Renderer/VisualSettings.hpp"

void entity_thread() {
    const auto entityAdmissionNow = [] { return GetTickCount(); };
    Diagnostics::ScopedDmaCallsite tag(Diagnostics::DmaCallsite::EntityDecrypt);
    const bool scanLatestWinsEnabled=V2::Acquisition::kLatestWins;
    const bool coldTopologyScanEnabled=V2::Acquisition::kColdTopology;
    Diagnostics::Info("UNDMA V2: unified acquisition, compact hot fields, resumable discovery, UN feature settings retained.");
    const auto entityProcessPeriod = V2::Acquisition::Period();
    auto nextProcessTick = std::chrono::steady_clock::now();
    Diagnostics::Info("UNDMA V2 acquisition target=%d Hz period_ms=%.7f.",
        V2::Acquisition::kTargetHz, std::chrono::duration<double,std::milli>(entityProcessPeriod).count());
    bool lastCriticalBatchFusionActive = OW::CriticalBatchFusionActive();
    uint64_t lastConnectionEpoch = OW::ProcessConnection::ConnectionEpoch();
    uint64_t processRegistryGeneration=0;
    uint64_t processSceneGeneration=OW::EntitySceneGeneration();
    OW::EntityRosterPolicy::DiscoveryBook cycleDiscoveries;
    std::vector<std::pair<uint64_t,uint64_t>> cycleMemberPairs;
    auto lastRuntimeProfile = OW::offset::ActiveProfile();
    Vector3 lastpos{};
    size_t lastLoggedRawCount = static_cast<size_t>(-1);
    size_t lastLoggedValidatedCount = static_cast<size_t>(-1);
    size_t previousProcessedValidCount = 0;
    DWORD lastProcessLogTick = 0;
    uint64_t entityCycleCount = 0;
    auto entityCycleRateTick = std::chrono::steady_clock::now();
    double entityCycleHz = 0.0;

#include "Game/EntityMaintenance/CacheState.inl"

    auto readDwordEnv = [](const char* name, DWORD fallback, DWORD minValue, DWORD maxValue) {
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
    };

    auto readBoolEnv = [](const char* name) {
        char buffer[16] = {};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return false;
        return buffer[0] != '0' &&
            buffer[0] != 'n' &&
            buffer[0] != 'N' &&
            buffer[0] != 'f' &&
            buffer[0] != 'F';
    };

    const bool entityHotPagePrefetchEnabled =
        readBoolEnv("UNLEASHED_DMA_PREFETCH_HOT_PAGES");
    const DWORD entityHotPagePrefetchMaxPages =
        readDwordEnv("UNLEASHED_DMA_PREFETCH_MAX_PAGES", 128, 8, 512);
    const DWORD teamNameRefreshBudgetPerCycle =
        readDwordEnv("UN_DMA_TEAM_NAME_REFRESH_BUDGET", 2, 1, 64);
    const DWORD skillRefreshBudgetPerCycle =
        readDwordEnv("UN_DMA_SKILL_REFRESH_BUDGET", 2, 1, 64);
    const DWORD skeletonRefreshBudgetPerCycle =
        readDwordEnv("UN_DMA_SKELETON_REFRESH_BUDGET", 4, 1, 64);
    DWORD lastPrefetchLogTick = 0;

    auto prefetchKnownEntityHotPages =
        [&](const std::vector<std::pair<uint64_t, uint64_t>>& rawEntities,
            DWORD now) {
            if (!entityHotPagePrefetchEnabled)
                return;

            constexpr uint64_t kPageMask = ~0xFFFull;
            std::vector<uint64_t> pages{};
            pages.reserve(entityHotPagePrefetchMaxPages);

            auto addPage = [&](uint64_t address) {
                if (!address || pages.size() >= entityHotPagePrefetchMaxPages)
                    return;

                const uint64_t page = address & kPageMask;
                if (!page)
                    return;
                if (std::find(pages.begin(), pages.end(), page) == pages.end())
                    pages.push_back(page);
            };

            auto addRangePages = [&](uint64_t address, size_t size) {
                if (!address || size == 0)
                    return;
                addPage(address);
                addPage(address + size - 1);
            };

            const auto& activeOffsets = offset::Active();
            for (const auto& [componentParent, linkParent] : rawEntities) {
                addRangePages(componentParent + OW::kEntityHeaderSnapshotOffset,
                    OW::kEntityHeaderSnapshotSize);
                if (linkParent != componentParent) {
                    addRangePages(linkParent + OW::kEntityHeaderSnapshotOffset,
                        OW::kEntityHeaderSnapshotSize);
                }

                const auto cacheIt = componentBaseCache.find(componentParent);
                if (cacheIt == componentBaseCache.end() ||
                    cacheIt->second->linkParent != linkParent) {
                    continue;
                }

                const ComponentBaseCache& cache = *cacheIt->second;
                addRangePages(cache.velocity, sizeof(OW::velocity_compo_t));
                addRangePages(cache.health, sizeof(OW::health_compo_t));
                addRangePages(cache.hero, sizeof(OW::hero_compo_t));
                addPage(cache.visibility + activeOffsets.VisibilityValueOffset);
                addPage(cache.team + OW::offset::Team_FlagsOffset);
                addPage(cache.rotation + OW::offset::RotationBase_Sub1);
                addPage(cache.transform + 0x3D0);
                addPage(cache.angle);
                addPage(cache.enemyAngle);
                addPage(cache.skill + 0x40);

                const OW::c_entity::SkeletonBoneCache& skeleton = cache.skeletonCache;
                if (skeleton.valid && skeleton.bonesBase && skeleton.maxMappedIndex >= 0) {
                    constexpr uint64_t kBoneValueOffset = 0x20;
                    constexpr uint64_t kBoneStride = 0x30;
                    const size_t boneBlockBytes =
                        static_cast<size_t>(kBoneStride) *
                            static_cast<size_t>(skeleton.maxMappedIndex) +
                        sizeof(XMFLOAT3);
                    addRangePages(skeleton.bonesBase + kBoneValueOffset, boneBlockBytes);
                }

                if (pages.size() >= entityHotPagePrefetchMaxPages)
                    break;
            }

            if (pages.empty())
                return;

            Diagnostics::ScopedDmaCallsite::Push(Diagnostics::DmaCallsite::EntityPrefetch);
            const bool ok = mem.PrefetchPages(pages);
            Diagnostics::ScopedDmaCallsite::Pop();

            if (lastPrefetchLogTick == 0 || now - lastPrefetchLogTick >= 5000) {
                Diagnostics::Info("[DMA-PREFETCH] hot_pages=%zu ok=%d max_pages=%lu note=direct_reads_use_nocache.",
                    pages.size(),
                    ok ? 1 : 0,
                    static_cast<unsigned long>(entityHotPagePrefetchMaxPages));
                lastPrefetchLogTick = now;
            }
        };

#include "Game/EntityMaintenance/RosterState.inl"

    auto resetRosterCycleFlags = [&]() {
        for (auto& rosterPair : entityRoster) {
            rosterPair.second->seenThisCycle = false;
            rosterPair.second->scanSeenThisCycle = false;
            rosterPair.second->hotReadRetainedThisCycle = false;
        }
    };

    auto applyCachedUltimateToEntity =
        [&](OW::c_entity& entity, const RosterEntry& cache) {
            const bool valid = cache.ultimateValid && std::isfinite(cache.ultimate);
            entity.display_ultimate_tick = valid ? cache.displayUltimateTick : 0;
            entity.ultimate = valid ? cache.ultimate : 0;
        };

#include "Game/EntityMaintenance/RecordAccess.inl"

#include "Game/EntityMaintenance/RosterMaintenance.inl"

    auto recordEntityCycle = [&]() {
        ++entityCycleCount;
        if ((entityCycleCount % 60ull) == 0ull) {
            const auto now = std::chrono::steady_clock::now();
            const std::chrono::duration<double> elapsed = now - entityCycleRateTick;
            entityCycleHz = elapsed.count() > 0.0 ? (60.0 / elapsed.count()) : 0.0;
            entityCycleRateTick = now;
            if (OW::PipelineDebugEnabled()) {
                Diagnostics::Info("[PIPELINE] entity_thread cycle %llu at ~%.1f Hz.",
                    static_cast<unsigned long long>(entityCycleCount),
                    entityCycleHz);
            }
        }
        Diagnostics::RecordEntityProcessCycle(entityCycleHz);
    };

#include "Game/EntityMaintenance/DmaFieldState.inl"

    auto resetDmaFieldWindow = [&](DWORD now) {
        dmaFieldWindow = EntityDmaFieldWindow{};
        dmaFieldWindow.startTick = now;
    };
    auto maybeLogDmaFieldWindow = [&](DWORD now) {
        if (dmaFieldWindow.startTick == 0) {
            dmaFieldWindow.startTick = now;
            return;
        }

        const DWORD elapsed = now - dmaFieldWindow.startTick;
        if (elapsed < 5000 || dmaFieldWindow.cycles == 0)
            return;

        Diagnostics::Info("[DMA-FIELD] window_ms=%lu cycles=%zu raw=%zu validated=%zu published=%zu hot_scatter=%zu fail=%zu.",
            static_cast<unsigned long>(elapsed),
            dmaFieldWindow.cycles,
            dmaFieldWindow.raw,
            dmaFieldWindow.validated,
            dmaFieldWindow.published,
            dmaFieldWindow.hotScatterExecute,
            dmaFieldWindow.hotScatterExecuteFail);
        Diagnostics::Info("[DMA-FIELD] component cache_hit=%zu cache_miss=%zu header=%zu fail=%zu link_header=%zu fail=%zu.",
            dmaFieldWindow.componentCacheHit,
            dmaFieldWindow.componentCacheMiss,
            dmaFieldWindow.componentHeaderRead,
            dmaFieldWindow.componentHeaderReadFail,
            dmaFieldWindow.linkHeaderRead,
            dmaFieldWindow.linkHeaderReadFail);
        Diagnostics::Info("[DMA-FIELD] velocity req=%zu scatter_hit=%zu partial=%zu fallback=%zu fail=%zu health req=%zu scatter_hit=%zu partial=%zu fallback=%zu read_fail=%zu layout_fail=%zu missing=%zu.",
            dmaFieldWindow.velocityRequested,
            dmaFieldWindow.velocityScatterHit,
            dmaFieldWindow.velocityScatterPartial,
            dmaFieldWindow.velocityFallback,
            dmaFieldWindow.velocityReadFail,
            dmaFieldWindow.healthRequested,
            dmaFieldWindow.healthScatterHit,
            dmaFieldWindow.healthScatterPartial,
            dmaFieldWindow.healthFallback,
            dmaFieldWindow.healthReadFail,
            dmaFieldWindow.healthLayoutFail,
            dmaFieldWindow.healthBaseMissing);
        Diagnostics::Info("[DMA-FIELD] hero req=%zu scatter_hit=%zu partial=%zu fallback=%zu read_fail=%zu missing=%zu fallback_fail=%zu visibility req=%zu scatter_hit=%zu partial=%zu fallback=%zu read_fail=%zu missing=%zu anomaly=%zu.",
            dmaFieldWindow.heroRequested,
            dmaFieldWindow.heroScatterHit,
            dmaFieldWindow.heroScatterPartial,
            dmaFieldWindow.heroFallback,
            dmaFieldWindow.heroReadFail,
            dmaFieldWindow.heroBaseMissing,
            dmaFieldWindow.heroFallbackFail,
            dmaFieldWindow.visibilityRequested,
            dmaFieldWindow.visibilityScatterHit,
            dmaFieldWindow.visibilityScatterPartial,
            dmaFieldWindow.visibilityFallback,
            dmaFieldWindow.visibilityReadFail,
            dmaFieldWindow.visibilityBaseMissing,
            dmaFieldWindow.visibilityAnomaly);
        Diagnostics::Info("[DMA-FIELD] entity_fail link_base=%zu name_unknown=%zu team_refresh=%zu skill_refresh=%zu bones candidates=%zu any=%zu head=%zu core_fallback=%zu.",
            dmaFieldWindow.linkBaseFail,
            dmaFieldWindow.nameUnknown,
            dmaFieldWindow.teamRefresh,
            dmaFieldWindow.skillRefresh,
            dmaFieldWindow.boneCandidates,
            dmaFieldWindow.skeletonAnyValid,
            dmaFieldWindow.skeletonHeadValid,
            dmaFieldWindow.coreAnchorFallback);
        resetDmaFieldWindow(now);
    };

    struct ScopedPipelinePhaseTimer {
        double& targetMs;
        std::chrono::steady_clock::time_point startedAt;

        explicit ScopedPipelinePhaseTimer(double& target)
            : targetMs(target),
              startedAt(std::chrono::steady_clock::now())
        {
        }

        ~ScopedPipelinePhaseTimer()
        {
            const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - startedAt).count();
            targetMs += static_cast<double>(elapsedUs) / 1000.0;
        }
    };

    auto finalizePipelineProcessStats = [](
        Diagnostics::EntityPipelineProcessStats& stats,
        const Diagnostics::DmaReadStats& dmaStart,
        const std::chrono::steady_clock::time_point& cycleStartedAt) {
        const Diagnostics::DmaReadStats dmaEnd = Diagnostics::SnapshotDmaReadStats();
        stats.entityCycleMs = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - cycleStartedAt).count()) / 1000.0;
        const double entityLoopKnownMs =
            stats.phase.entityLoopSetupMs +
            stats.phase.entityHeaderSpecialMs +
            stats.phase.entityCacheApplyMs +
            stats.phase.entityHotFieldsMs +
            stats.phase.entityRotationPositionMs +
            stats.phase.baseCacheMs +
            stats.phase.baseDecryptMs +
            stats.phase.healthMs +
            stats.phase.heroMs +
            stats.phase.visibilityMs +
            stats.phase.skeletonMs +
            stats.phase.skillMs +
            stats.phase.teamNameMs +
            stats.phase.localSelectMs;
        stats.phase.entityLoopGapMs =
            stats.phase.entityLoopWallMs > entityLoopKnownMs
                ? stats.phase.entityLoopWallMs - entityLoopKnownMs
                : 0.0;
        const double measuredPhaseMs =
            stats.phase.beginFrameMs +
            stats.phase.consumeScanMs +
            stats.phase.previousSnapshotCopyMs +
            stats.phase.prefetchMs +
            stats.phase.previousIndexMs +
            stats.phase.hotScatterPrepareMs +
            stats.phase.hotScatterExecuteMs +
            stats.phase.baseCacheMs +
            stats.phase.baseDecryptMs +
            stats.phase.healthMs +
            stats.phase.heroMs +
            stats.phase.visibilityMs +
            stats.phase.skeletonMs +
            stats.phase.skillMs +
            stats.phase.teamNameMs +
            stats.phase.localSelectMs +
            stats.phase.publishMs +
            stats.phase.recordSyncMs +
            stats.phase.entityLoopSetupMs +
            stats.phase.entityHeaderSpecialMs +
            stats.phase.entityCacheApplyMs +
            stats.phase.entityHotFieldsMs +
            stats.phase.entityRotationPositionMs +
            stats.phase.entityLoopGapMs;
        stats.phase.cycleGapMs =
            stats.entityCycleMs > measuredPhaseMs
                ? stats.entityCycleMs - measuredPhaseMs
                : 0.0;
        stats.dmaReadsDelta = dmaEnd.total >= dmaStart.total
            ? dmaEnd.total - dmaStart.total
            : 0;
        stats.dmaFailDelta = dmaEnd.failed >= dmaStart.failed
            ? dmaEnd.failed - dmaStart.failed
            : 0;
        Diagnostics::SetEntityPipelineProcessStats(stats);
    };

#include "Game/EntityMaintenance/RescanRequest.inl"

        struct HotFieldBatchItem {
            uint64_t componentParent = 0;
            uint64_t linkParent = 0;
            EntityHotFieldReads reads{};
            OW::Vector3 rotation{}, viewDirection{};
            DWORD viewDirectionBytes=0;
            DWORD rotationBytes = 0;
            bool rotationRequested = false;
            uint32_t observedMatchId=0;
            DWORD matchIdBytes=0;
            DWORD readStartedTick=0,readCompletedTick=0;
            struct BoneRange { int first=0,last=0; size_t offset=0, size=0; DWORD bytes=0; };
            std::array<BoneRange,19> boneRanges{};
            std::array<uint8_t,19*0x60> boneData{};
            std::array<bool,18> boneRequested{};
            size_t boneRangeCount=0;
            bool botChestRequested=false;
            std::vector<uint8_t> shapeBoneData;
            DWORD shapeBoneBytes=0;
            OW::EntityBodyObservation bodyObservation{};
            bool prepared=false, applied=false;
            uint64_t sampleTimeUs=0;

            bool RotationRead() const {
                return rotationRequested &&
                    rotationBytes == sizeof(OW::Vector3) &&
                    std::isfinite(rotation.X) &&
                    std::isfinite(rotation.Y) &&
                    std::isfinite(rotation.Z);
            }
        };
    struct ScatterOwner {
        VMMDLL_SCATTER_HANDLE value = mem.CreateScatterHandle();
        ~ScatterOwner() { if(value) mem.CloseScatterHandle(value); }
    } hotScatterOwner;
    const auto hotFieldScatter = hotScatterOwner.value;
    std::vector<HotFieldBatchItem> hotFieldBatchItems;
    std::unordered_map<uint64_t,size_t> hotFieldBatchIndex;
    std::unordered_map<uint64_t,uint64_t> successfulSamples;
    auto measurementStart=std::chrono::steady_clock::now();
    size_t measuredCycles=0; double hotTotalMs=0, hotMaxMs=0;

    while (OW::RuntimeWorkersRunning()) {
        if (!OW::ProcessConnection::IsConnected()) {
            nextProcessTick = std::chrono::steady_clock::now();
            OW::RuntimeWorkerSleep(100);
            continue;
        }

        const bool criticalBatchFusionActive = OW::CriticalBatchFusionActive();
        const uint64_t connectionEpoch = OW::ProcessConnection::ConnectionEpoch();
        const uint64_t currentSceneGeneration=OW::EntitySceneGeneration();
        const auto runtimeProfile = OW::offset::ActiveProfile();
        if (processSceneGeneration!=currentSceneGeneration || criticalBatchFusionActive != lastCriticalBatchFusionActive ||
            connectionEpoch != lastConnectionEpoch ||
            runtimeProfile != lastRuntimeProfile) {
            componentBaseCache.clear();
            dynamicEntityCache.clear();
            entityRoster.clear();
            retiredActors.clear();
            latestCompleteActorMembers.clear();completeActorMembershipKnown=false;
            discoveryAttemptTicks.clear();
            cycleDiscoveries={};cycleMemberPairs.clear();processSceneGeneration=currentSceneGeneration;
            lastpos = {};
            previousProcessedValidCount = 0;
            lastTopologyRescanRequestTick = 0;
            lastRespawnTopologyRescanRequestTick = 0;
            lastLoggedRawCount = static_cast<size_t>(-1);
            lastLoggedValidatedCount = static_cast<size_t>(-1);
            Diagnostics::Info(
                "Entity owner reset: cbf_active_cn_ne=%d profile=%s connection_epoch=%llu.",
                criticalBatchFusionActive ? 1 : 0,
                OW::offset::ActiveProfileName(),
                static_cast<unsigned long long>(connectionEpoch));
            lastCriticalBatchFusionActive = criticalBatchFusionActive;
            lastConnectionEpoch = connectionEpoch;
            lastRuntimeProfile = runtimeProfile;
        }

        V2::Acquisition::Wait(nextProcessTick, entityProcessPeriod);

        SampleAcquisitionCamera();
        V2::Maps::Sample();
        const auto pipelineCycleStartedAt = std::chrono::steady_clock::now();
        const Diagnostics::DmaReadStats pipelineDmaStart = Diagnostics::SnapshotDmaReadStats();
        Diagnostics::EntityPipelineProcessStats pipelineStats{};
        Diagnostics::EntityLifecycleStats& lifecycleStats = pipelineStats.lifecycle;

        {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.beginFrameMs);
            SDK->BeginFrame(Diagnostics::SdkFrameSource::Process);
        }

        std::vector<std::pair<uint64_t, uint64_t>> consumedLatestRawEntities;
        std::vector<uint64_t> consumedActualMembers;
        uint8_t consumedScanSource=0;
        bool consumedLatestRawScan = false;
        bool consumedRawScanThisCycle = false;
        bool consumedCompleteScan = false;
        const auto processRegistryState=OW::RefreshEntityRegistry(false);
        auto processRegistry=processRegistryState.snapshot;
#include "Game/EntityMaintenance/RegistryGeneration.inl"
        bool clearScene=false;
        {
            std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
            if(OW::entity_discoveries.connection==connectionEpoch && OW::entity_discoveries.registry==processRegistry.generation &&
                OW::entity_discoveries.scene==processSceneGeneration) cycleDiscoveries=OW::entity_discoveries;
            else cycleDiscoveries={};
            clearScene=OW::empty_scene_watch.ready && OW::empty_scene_watch.connection==connectionEpoch &&
                OW::empty_scene_watch.registry==processRegistry.generation && OW::empty_scene_watch.scene==processSceneGeneration;
            lifecycleStats.sceneGeneration=processSceneGeneration;
            lifecycleStats.sceneClearCount=OW::entity_scene_clear_count;
            lifecycleStats.seatExpiredCount=OW::entity_seat_expired_count;
            lifecycleStats.emptyScenePending=OW::empty_scene_watch.pending;
            lifecycleStats.emptySceneAgeMs=OW::empty_scene_watch.pending ? DWORD(GetTickCount()-OW::empty_scene_watch.since) : 0;
            lifecycleStats.staleSceneRejectedCount=OW::rejectedEntitySceneResults.load();
        }
        if(clearScene && ClearEntityScene(processSceneGeneration,connectionEpoch)) continue;
        {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.consumeScanMs);
#include "Game/EntityMaintenance/ConsumeRawScan.inl"
        }

        std::vector<std::pair<uint64_t, uint64_t>> raw_entities;
        std::vector<OW::c_entity> previous_entities;
        {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.previousSnapshotCopyMs);
            std::lock_guard<std::mutex> lock(g_mutex);
            if (scanLatestWinsEnabled && consumedLatestRawScan) {
                // Producers already merge recent partial discoveries. Merging
                // again here would resurrect expired historical pairs forever.
                OW::ow_entities = std::move(consumedLatestRawEntities);
                OW::ow_entities_registry=processRegistry;
            }
            if(OW::AcceptEntityRegistry(OW::ow_entities_registry))raw_entities = OW::ow_entities;
            previous_entities = OW::SnapshotFrame()->entities;
        }
        const DWORD processLoopTick = GetTickCount();
        if(!scanLatestWinsEnabled && consumedCompleteScan) {
            latestCompleteActorMembers.clear();completeActorMembershipKnown=true;
            cycleMemberPairs=raw_entities;
            for(const auto& pair:raw_entities)if(pair.first)latestCompleteActorMembers.insert(pair.first);
        }
        std::unordered_set<uint64_t> rawScanSeenComponents{};
        std::unordered_set<uint64_t> scheduledRawComponents{};
        rawScanSeenComponents.reserve(raw_entities.size() * 2 + 16);
        rawScanSeenComponents.insert(consumedActualMembers.begin(),consumedActualMembers.end());
        scheduledRawComponents.reserve(raw_entities.size() * 2 + 16);
        for (const auto& rawPair : raw_entities) {
            if (rawPair.first != 0) {
                scheduledRawComponents.insert(rawPair.first);
            }
        }
        std::unordered_set<uint64_t> scanMissHotReadComponents{};
        std::unordered_set<uint64_t> scanMissHotReadSucceededComponents{};
        resetRosterCycleFlags();
        // Maintain deadlines before any potentially slow read, including empty/unavailable scans.
        std::vector<OW::c_entity> maintainedEntities;
        publishRosterSnapshot(processLoopTick,0,maintainedEntities,&lifecycleStats);
#include "Game/EntityMaintenance/RegistryUnavailable.inl"
        const bool maintenanceChanged=maintainedEntities.size()!=previous_entities.size() ||
            std::any_of(maintainedEntities.begin(),maintainedEntities.end(),[&](const auto& value) {
                const auto old=std::find_if(previous_entities.begin(),previous_entities.end(),[&](const auto& previous){return previous.address==value.address;});
                return old!=previous_entities.end() && old->body_valid && !value.body_valid;
            });
        if(maintenanceChanged) {
            size_t count=0;
            OW::TargetingDetail::TryPublishEntitySnapshots(std::move(maintainedEntities),{},connectionEpoch,count,&processRegistry,processSceneGeneration);
            Diagnostics::SetEntityPipelineProcessStats(pipelineStats);
        }
        std::erase_if(raw_entities,[&](const auto& pair) {
            if(cycleDiscoveries.Find(pair.first,pair.second,processLoopTick))return false;
            return componentBaseCache.contains(pair.first) ||
                std::any_of(previous_entities.begin(),previous_entities.end(),[&](const auto& value){return value.address==pair.first;});
        });
        if(completeActorMembershipKnown)for(auto& [address,retired]:retiredActors)
            if(!latestCompleteActorMembers.contains(address))retired.absent=true;


        const DWORD softRefreshGapMs = OW::EntitySoftRefreshGapMs();
        const DWORD hardRescanGapMs = OW::EntityHardRescanGapMs();
        const DWORD scanMissGraceCount = OW::EntityScanMissGraceCount();
        if (consumedRawScanThisCycle &&
            hardRescanGapMs > 0 &&
            scanMissGraceCount > 0 &&
            !entityRoster.empty()) {
            scanMissHotReadComponents.reserve(entityRoster.size());
            scanMissHotReadSucceededComponents.reserve(entityRoster.size());
            for (auto& rosterPair : entityRoster) {
                RosterEntry& rosterEntry = *rosterPair.second;
                const OW::c_entity& rosterEntity = rosterEntry.entity;
                if (!rosterEntity.address || !rosterEntity.LinkParent)
                    continue;
                if (scheduledRawComponents.find(rosterEntity.address) != scheduledRawComponents.end())
                    continue;
                if (rosterEntry.lastScanSeenTick == 0) {
                    rosterEntry.lastScanSeenTick = rosterEntity.last_scan_seen_tick_ms != 0
                        ? rosterEntity.last_scan_seen_tick_ms
                        : rosterEntity.last_seen_tick_ms;
                }
                if (rosterEntry.lastHotReadTick == 0) {
                    rosterEntry.lastHotReadTick = rosterEntity.last_hot_read_tick_ms != 0
                        ? rosterEntity.last_hot_read_tick_ms
                        : rosterEntity.last_seen_tick_ms;
                }

                const DWORD scanGapMs = rosterEntry.lastScanSeenTick != 0
                    ? processLoopTick - rosterEntry.lastScanSeenTick
                    : hardRescanGapMs + 1;
                const DWORD estimatedScanMissCount = scanGapMs == 0
                    ? 1
                    : (scanGapMs + OW::kEntityScanIntervalMs - 1) / OW::kEntityScanIntervalMs;
                const bool aliveHotReadable =
                    rosterEntity.roster_state == OW::EntityRosterState::Fresh &&
                    rosterEntity.Alive &&
                    rosterEntity.PlayerHealth > 0.0f &&
                    hasRenderableAnchor(rosterEntity);
                if (!aliveHotReadable)
                    continue;

                if (scanGapMs <= hardRescanGapMs &&
                    estimatedScanMissCount <= scanMissGraceCount) {
                    if (scanGapMs >= softRefreshGapMs)
                        ++lifecycleStats.entityRecordScanMissSoftGapCount;
                    rosterEntry.consecutiveScanMissCount = estimatedScanMissCount;
                    rosterEntry.hotReadRetainedThisCycle = true;
                    raw_entities.emplace_back(rosterEntity.address, rosterEntity.LinkParent);
                    scheduledRawComponents.insert(rosterEntity.address);
                    scanMissHotReadComponents.insert(rosterEntity.address);
                    ++lifecycleStats.entityRecordScanMissGraceAppendCount;
                } else {
                    if (scanGapMs > hardRescanGapMs)
                        ++lifecycleStats.entityRecordScanMissHardGapCount;
                    ++lifecycleStats.entityRecordScanMissGraceDropCount;
                }
            }
        }

        // No entities available
        if (raw_entities.empty()) {
            Diagnostics::EntityProcessStats stats{};
            Diagnostics::LocalEntityStats localStats{};
            std::vector<OW::c_entity> published_entities{};
            Diagnostics::RosterStats rosterStats =
                publishRosterSnapshot(GetTickCount(), 0, published_entities, &lifecycleStats);
            previousProcessedValidCount = 0;
            if (componentBaseCache.size() > 512)
            {

                componentBaseCache.clear();
            }
            if (dynamicEntityCache.size() > 512) {
                lifecycleStats.dynamicCacheExpiredCount += dynamicEntityCache.size();

                dynamicEntityCache.clear();
            }
            const bool requestFastRescan =
                rosterStats.fresh > 0 || rosterStats.missing > 0 || rosterStats.dead > 0;
            if (!OW::ProcessConnection::IsConnected() ||
                OW::ProcessConnection::ConnectionEpoch() != connectionEpoch ||
                OW::offset::ActiveProfile() != runtimeProfile) {
                continue;
            }
            size_t published_count = 0;
            {
                ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.publishMs);
                if (!OW::TargetingDetail::TryPublishEntitySnapshots(
                        std::move(published_entities),
                        std::vector<OW::hpanddy>{},
                        connectionEpoch,
                        published_count,&processRegistry,processSceneGeneration)) {
                    continue;
                }
            }
            pipelineStats.rawCount = 0;
            pipelineStats.validatedCount = 0;
            pipelineStats.publishedCount = published_count;
            if (requestFastRescan) {
                const DWORD requestTick = GetTickCount();
                if (coldTopologyScanEnabled) {
                    requestTopologyRescan(requestTick);
                } else {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    OW::entity_fast_scan_until_tick =
                        requestTick + OW::kEntityFastRescanWindowMs;
                }
            }

            Diagnostics::SetEntityProcessStats(stats);
            Diagnostics::SetLocalEntityStats(localStats);
            Diagnostics::SetRosterStats(rosterStats);
            if (OW::PipelineDebugEnabled()) {
                const DWORD now = GetTickCount();
                const bool changed = lastLoggedRawCount != 0 || lastLoggedValidatedCount != 0;
                if (changed || now - lastProcessLogTick >= 1000) {
                    Diagnostics::Info("[PIPELINE] Stage 4 entity processing raw=0 validated=0.");
                    Diagnostics::Info("[PIPELINE] Stage 4 roster fresh=%zu dead=%zu missing=%zu expired=%zu hero_change=%zu.",
                        rosterStats.fresh,
                        rosterStats.dead,
                        rosterStats.missing,
                        rosterStats.expired,
                        rosterStats.heroChanged);
                    lastLoggedRawCount = 0;
                    lastLoggedValidatedCount = 0;
                    lastProcessLogTick = now;
                }
            }
            recordEntityCycle();
            finalizePipelineProcessStats(pipelineStats, pipelineDmaStart, pipelineCycleStartedAt);
            StepAcquisitionDiscovery();
            continue;
        }
        if (componentBaseCache.size() > 512) {

            componentBaseCache.clear();
        }
        if (dynamicEntityCache.size() > 512) {

            dynamicEntityCache.clear();
        }

        {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.prefetchMs);
            prefetchKnownEntityHotPages(raw_entities, processLoopTick);
        }

        std::vector<OW::c_entity> tmp_entities{};
        std::vector<OW::hpanddy> hpdy_entities{};
        OW::c_entity lastentity{};
        Diagnostics::EntityProcessStats processStats{};
        processStats.raw = raw_entities.size();
        pipelineStats.rawCount = raw_entities.size();
        Diagnostics::LocalEntityStats localStats{};
        size_t teamNameRefreshBudget =
            static_cast<size_t>(teamNameRefreshBudgetPerCycle);
        size_t skillRefreshBudget =
            static_cast<size_t>(skillRefreshBudgetPerCycle);
        size_t skeletonRefreshBudget =
            static_cast<size_t>(skeletonRefreshBudgetPerCycle);
        const size_t skeletonBudgetForCycle=skeletonRefreshBudget;
        size_t entityLoopStart = raw_entities.empty() ? 0 : maintenanceCursor % raw_entities.size();
        for(size_t offset=0;offset<raw_entities.size();++offset) {
            const size_t index=(entityLoopStart+offset)%raw_entities.size();
            const auto [address,link]=raw_entities[index];
            const auto cache=componentBaseCache.find(address);
            const bool newActor=(cache==componentBaseCache.end() || cache->second->linkParent!=link) &&
                !dynamicEntityCache.contains(address);
            const auto retry=discoveryAttemptTicks.find(address);
            if(newActor && (retry==discoveryAttemptTicks.end() || processLoopTick-retry->second>=250)) {
                entityLoopStart=index;discoveryAttemptTicks[address]=processLoopTick;break;
            }
        }
        std::unordered_map<BaseDecryptCycleKey, size_t, BaseDecryptCycleKeyHash>
            baseDecryptCycleKeyCounts{};
        baseDecryptCycleKeyCounts.reserve(raw_entities.size() * 16 + 16);
        auto consumeCycleBudget = [](size_t& budget) {
            if (budget == 0)
                return false;
            --budget;
            return true;
        };
        size_t heroChangedThisCycle = 0;
        bool sampledBoneCandidateHasAngle = false;
        const bool detailedProcessLog = OW::PipelineDebugEnabled() &&
            (lastProcessLogTick == 0 || processLoopTick - lastProcessLogTick >= 1000);
        const OW::Vector3 cameraLocation = OW::SnapshotCameraPosition();
        OW::c_entity localCycleSnapshot = OW::SnapshotLocalEntity();
        const auto toCentimeters = [](float value) -> int {
            return std::isfinite(value) ? static_cast<int>(value * 100.0f) : 0;
        };
        localStats.cameraXCm = std::isfinite(cameraLocation.X) ? static_cast<int>(cameraLocation.X * 100.0f) : 0;
        localStats.cameraYCm = std::isfinite(cameraLocation.Y) ? static_cast<int>(cameraLocation.Y * 100.0f) : 0;
        localStats.cameraZCm = std::isfinite(cameraLocation.Z) ? static_cast<int>(cameraLocation.Z * 100.0f) : 0;

        constexpr uint64_t kScatterPageMask = ~0xFFFull;
        std::unordered_set<uint64_t> hotScatterPages{};
        hotScatterPages.reserve(raw_entities.size() * 4);
#include "Game/EntityMaintenance/HotRead.inl"

#include "Game/EntityMaintenance/FailureSample.inl"
#include "Game/EntityMaintenance/HealthCacheRecovery.inl"

        auto recordNameUnknownSample = [&](const OW::c_entity& unknownEntity,
                                           uint64_t componentParent,
                                           uint64_t linkParent,
                                           const std::string& resolvedName) {
            if (processStats.sampleNameUnknownComponentParent)
                return;
            const auto scanHeroIdCandidate = [](uint64_t heroBase, uint64_t& candidate, int& offset) {
                candidate = 0;
                offset = -1;
                if (!heroBase)
                    return;
                std::array<uint64_t, 0x100> qwords{};
                if (!SDK->read_range(heroBase, qwords.data(), qwords.size() * sizeof(uint64_t)))
                    return;
                for (size_t qwordIndex = 0; qwordIndex < qwords.size(); ++qwordIndex) {
                    const uint64_t value = qwords[qwordIndex];
                    if (OW::GameData::HasHeroIdPrefix(value)) {
                        candidate = value;
                        offset = static_cast<int>(qwordIndex * sizeof(uint64_t));
                        return;
                    }
                }
            };
            auto readHeroId = [](uint64_t heroBase) -> uint64_t {
                if (!heroBase)
                    return 0;
                OW::hero_compo_t heroCompo{};
                if (!SDK->read_range(heroBase, &heroCompo, sizeof(heroCompo)))
                    return 0;
                return heroCompo.heroid;
            };
            uint64_t linkHeroCandidate = 0;
            int linkHeroCandidateOffset = -1;
            scanHeroIdCandidate(unknownEntity.HeroBase, linkHeroCandidate, linkHeroCandidateOffset);
            const uint64_t componentHeroBase =
                OW::DecryptComponent(componentParent, OW::offset::Active().typeHeroId);
            const uint64_t componentHeroId = readHeroId(componentHeroBase);
            uint64_t componentHeroCandidate = 0;
            int componentHeroCandidateOffset = -1;
            scanHeroIdCandidate(componentHeroBase, componentHeroCandidate, componentHeroCandidateOffset);

            processStats.sampleNameUnknownComponentParent = componentParent;
            processStats.sampleNameUnknownLinkParent = linkParent;
            processStats.sampleNameUnknownComponentMatchId = unknownEntity.match_id;
            processStats.sampleNameUnknownLinkMatchId = linkParent
                ? SDK->RPM<uint32_t>(linkParent + offset::Entity_MatchId)
                : 0;
            processStats.sampleNameUnknownHeroBase = unknownEntity.HeroBase;
            processStats.sampleNameUnknownHeroId = unknownEntity.HeroID;
            processStats.sampleNameUnknownHeroIdCandidate = linkHeroCandidate;
            processStats.sampleNameUnknownHeroIdCandidateOffset = linkHeroCandidateOffset;
            processStats.sampleNameUnknownComponentHeroBase = componentHeroBase;
            processStats.sampleNameUnknownComponentHeroId = componentHeroId;
            processStats.sampleNameUnknownComponentHeroIdCandidate = componentHeroCandidate;
            processStats.sampleNameUnknownComponentHeroIdCandidateOffset = componentHeroCandidateOffset;
            processStats.sampleNameUnknownLinkBase = unknownEntity.LinkBase;
            processStats.sampleNameUnknownSkillBase = unknownEntity.SkillBase;
            processStats.sampleNameUnknownTeamBase = unknownEntity.TeamBase;
            processStats.sampleNameUnknownBoneBase = unknownEntity.BoneBase;
            processStats.sampleNameUnknownKind = resolvedName.rfind("Hero_", 0) == 0 ? 2 : 1;
        };

        // Also log entity MatchIds for cross-reference on first detailed cycle
        static bool matchIdLogEnabled = true;

        std::unordered_map<uint64_t, const OW::c_entity*> previousEntityByAddress;
        std::unordered_map<uint64_t, const OW::c_entity*> previousEntityByRosterKey;
        {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.previousIndexMs);
            previousEntityByAddress.reserve(previous_entities.size());
            previousEntityByRosterKey.reserve(previous_entities.size());
            for (const OW::c_entity& previous : previous_entities) {
                if (previous.address)
                    previousEntityByAddress.emplace(previous.address, &previous);
                if (previous.roster_key)
                    previousEntityByRosterKey.emplace(previous.roster_key, &previous);
            }
        }

        hotFieldBatchItems.clear(); hotFieldBatchIndex.clear();
        hotFieldBatchItems.reserve(raw_entities.size());
        hotFieldBatchIndex.reserve(raw_entities.size());
        bool hotFramePublished=false;
        OW::Config::ScopedConfigRead configRead;
        OW::SkeletonBoneMask neededBones=OW::kSkeletonBoneHead|OW::kSkeletonBoneNeck|OW::kSkeletonBoneChest;
        if(OW::Config::View_autobone())neededBones|=OW::Config::View_aimbotBoneMask();
        if(OW::Config::autobone2)neededBones|=OW::Config::aimbotBoneMask2;
        neededBones|=OW::Config::View_triggerbotBoneMask()|OW::Config::triggerbotBoneMask2;
        OW::Config::HeroSkillSettings meleeSettings;
        if (OW::Config::TryGetHeroSkillSettings(localCycleSnapshot.HeroID, "auto-melee", meleeSettings) &&
            meleeSettings.enabled)
            neededBones |= OW::kSkeletonBoneAllMask;
        if(OW::Config::IsFlick2ndBehavior(OW::Config::View_aimBehavior()) || OW::Config::Flick2)
            neededBones|=OW::kSkeletonBoneShoulders|OW::kSkeletonBonePelvis;
        const auto& heroSlots=OW::Config::View_heroAimPresets();
        if(const auto slots=heroSlots.find(localCycleSnapshot.HeroID);slots!=heroSlots.end())
            for(const auto& slot:slots->second)if(slot.present && slot.enabled) {
                if(slot.preset.autoBone)neededBones|=slot.preset.aimBoneMask;
                if(OW::Config::IsFlick2ndBehavior(slot.preset.aimBehavior))neededBones|=OW::kSkeletonBoneShoulders|OW::kSkeletonBonePelvis;
            }

        const auto& activeOffsetsForHotBatch = offset::Active();

        {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.hotScatterPrepareMs);
            for (const auto& [componentParent, linkParent] : raw_entities) {
                if (!OW::RuntimeWorkersRunning() || !OW::ProcessConnection::IsConnected())
                    break;
                if (!hotFieldScatter || !componentParent || !linkParent)
                    continue;
                if (hotFieldBatchIndex.find(componentParent) != hotFieldBatchIndex.end())
                    continue;

                const auto dynamicCacheIt = dynamicEntityCache.find(componentParent);
                if (dynamicCacheIt != dynamicEntityCache.end() &&
                    dynamicCacheIt->second.valid &&
                    dynamicCacheIt->second.linkParent == linkParent) {
                    continue;
                }

                const auto cacheIt = componentBaseCache.find(componentParent);
                if (cacheIt == componentBaseCache.end())
                    continue;
                const ComponentBaseCache& cached = *cacheIt->second;
                if (cached.linkParent != linkParent ||
                    cached.baseUpdateTick == 0 ||
                    !cached.health) {
                    continue;
                }

                const DWORD refreshInterval = cached.alive
                    ? OW::kEntityLiveComponentRefreshMs
                    : OW::kEntityDeadComponentRefreshMs;
                if (processLoopTick - cached.baseUpdateTick >= refreshInterval) {
                    RuntimeTrace::Emit(RuntimeTrace::EntityReject,componentParent,cached.baseUpdateTick,processLoopTick,14);
                    continue;
                }

                const bool refreshHealth =
                    !cached.healthValid ||
                    processLoopTick - cached.healthUpdateTick >= OW::kEntityHealthIntervalMs;
                const bool refreshHero =
                    cached.hero &&
                    (!cached.heroValid ||
                     cached.heroUpdateTick == 0 ||
                     processLoopTick - cached.heroUpdateTick >= OW::kEntityHeroIntervalMs);
                const bool scatterVisibility =
                    activeOffsetsForHotBatch.VisibilityValueOffset != 0 &&
                    cached.visibility != 0 && cached.visibilitySample.Due(processLoopTick);
                const uint64_t visibilityValueAddress = scatterVisibility
                    ? cached.visibility + activeOffsetsForHotBatch.VisibilityValueOffset
                    : 0;

                const size_t itemIndex = hotFieldBatchItems.size();
                HotFieldBatchItem& item = hotFieldBatchItems.emplace_back();
                item.componentParent = componentParent;
                item.linkParent = linkParent;
                if(!mem.AddScatterReadRequest(hotFieldScatter,componentParent+offset::Entity_MatchId,
                    &item.observedMatchId,sizeof(item.observedMatchId),&item.matchIdBytes)) {
                    continue;
                }
                if (!prepareHotFields(
                        hotFieldScatter,
                        item.reads,
                        cached.velocity,
                        cached.velocity != 0,
                        cached.health,
                        refreshHealth,
                        cached.hero,
                        refreshHero,
                        visibilityValueAddress,
                        scatterVisibility)) {
                    continue;
                }
                if (cached.rotationLeaf) {
                    ++pipelineStats.hotScatterRequestedCount;
                    item.rotationRequested = mem.AddScatterReadRequest(
                        hotFieldScatter,
                        cached.rotationLeaf + offset::RotationBase_Sub2,
                        &item.rotation,
                        sizeof(item.rotation),
                        &item.rotationBytes);
                    recordHotScatterPrepare(
                        cached.rotationLeaf + offset::RotationBase_Sub2,
                        sizeof(item.rotation),
                        item.rotationRequested);
                }
                if(cached.angle)mem.AddScatterReadRequest(hotFieldScatter,cached.angle+OW::kPlayerControllerViewDirectionOffset,
                    &item.viewDirection,sizeof(item.viewDirection),&item.viewDirectionBytes);
                const auto& skeleton=cached.skeletonCache;
                if(skeleton.valid && skeleton.bonesBase) {
                    std::array<int,19> indices{}; size_t count=0;
                    for(size_t slot=0;slot<18;++slot) {
                        if(skeleton.mappedIndices[slot]<0 || (!OW::Config::draw_skel && slot>=3 &&
                            !OW::SkeletonBoneMaskIncludesRenderSlot(neededBones,slot)))continue;
                        item.boneRequested[slot]=true;indices[count++]=skeleton.mappedIndices[slot];
                    }
                    if(skeleton.hasBotChest && skeleton.botChestMappedIndex>=0) {
                        item.botChestRequested=true;indices[count++]=skeleton.botChestMappedIndex;
                    }
                    std::sort(indices.begin(),indices.begin()+count);
                    size_t packedOffset=0;
                    for(size_t begin=0;begin<count;) {
                        size_t end=begin+1;
                        while(end<count && indices[end]<=indices[end-1]+2)++end;
                        auto& range=item.boneRanges[item.boneRangeCount++];
                        range.first=indices[begin];range.last=indices[end-1];range.offset=packedOffset;
                        range.size=size_t(range.last-range.first)*0x30+sizeof(XMFLOAT3);
                        mem.AddScatterReadRequest(hotFieldScatter,skeleton.bonesBase+0x20+uint64_t(range.first)*0x30,
                            item.boneData.data()+packedOffset,range.size,&range.bytes);
                        packedOffset+=range.size;begin=end;
                    }
                }
                if(OW::Config::draw_hitbox && OW::ModelShapes::Supported(cached.heroId) &&
                   skeleton.valid && skeleton.shapeModelIndex>=0 && skeleton.shapeMaxIndex>=0 && skeleton.shapeMaxIndex<skeleton.boneCount) {
                    item.shapeBoneData.resize(size_t(skeleton.shapeMaxIndex+1)*0x30);
                    mem.AddScatterReadRequest(hotFieldScatter,skeleton.bonesBase,item.shapeBoneData.data(),
                        item.shapeBoneData.size(),&item.shapeBoneBytes);
                }
                item.prepared=true;
                hotFieldBatchIndex.emplace(componentParent, itemIndex);
            }
            pipelineStats.hotScatterBatchItems = hotFieldBatchItems.size();
            for (const HotFieldBatchItem& item : hotFieldBatchItems)
                pipelineStats.hotScatterBatchRequests +=
                    countHotFieldRequests(item.reads) +
                    static_cast<size_t>(item.rotationRequested ? 1 : 0);
        }

#include "Game/EntityMaintenance/PreviousSample.inl"
#include "Game/EntityMaintenance/ObservedDeath.inl"

        auto publishHotActors=[&]() {
        size_t hotUpdated=0;
        for(auto& item:hotFieldBatchItems) {
            const auto rejectHot=[&](uint64_t reason) {
                RuntimeTrace::Emit(RuntimeTrace::EntityReject,item.componentParent,item.readStartedTick,item.readCompletedTick,reason);
            };
            if(!item.prepared || !item.reads.scatterOk){rejectHot(3);continue;}
            if(!OW::PresentationTiming::ObservationWindowFresh32(item.readStartedTick,item.readCompletedTick,GetTickCount())) {
                RuntimeTrace::Emit(RuntimeTrace::EntityReject,item.componentParent,item.readStartedTick,item.readCompletedTick,1);continue;
            }
            auto cachedIt=componentBaseCache.find(item.componentParent);
            if(cachedIt==componentBaseCache.end() || cachedIt->second->linkParent!=item.linkParent){rejectHot(5);continue;}
            auto& cached=*cachedIt->second;
            if(item.matchIdBytes!=sizeof(item.observedMatchId)){rejectHot(6);continue;}
            if(cached.matchIdValid && cached.matchId!=item.observedMatchId) {
                OW::RevokeEntityBodyObservation(cached.entity.address,cached.entity.actor_generation,cached.entity.body_authority_revision);
                cached.entity.body_valid=false;
                cached.entity.team_known=false;
                componentBaseCache.erase(cachedIt);rejectHot(7);continue;
            }
            auto rosterIt=std::find_if(entityRoster.begin(),entityRoster.end(),[&](const auto& p) {
                return p.second->entity.address==item.componentParent && p.second->entity.LinkParent==item.linkParent;
            });
            if(rosterIt==entityRoster.end()){rejectHot(8);continue;}
            auto& entry=*rosterIt->second;
            OW::c_entity next=entry.entity;
            cached.ApplyBases(next);
            const auto* discovery=cycleDiscoveries.Find(next.address,next.LinkParent,GetTickCount());
            if(!discovery || item.observedMatchId!=next.match_id ||
                discovery->match!=next.match_id || discovery->hero!=next.HeroID ||
                next.body_absence_revision!=OW::EntityBodyAbsenceRevision(next.address) ||
                (completeActorMembershipKnown && std::find(cycleMemberPairs.begin(),cycleMemberPairs.end(),
                    std::make_pair(next.address,next.LinkParent))==cycleMemberPairs.end())) {
                entry.entity.body_valid=false;rejectHot(14);continue;
            }
            if(item.reads.DeathRead() && item.reads.health.death==1) {
                item.applied=commitObservedDeath(entry,next,*discovery,item.bodyObservation,item.readStartedTick);
                if(item.applied)++hotUpdated;
                continue;
            }
#include "Game/EntityMaintenance/HotHealthApply.inl"
            if(!item.reads.VelocityRead()){rejectHot(4);continue;}
            // Hero changes and respawns go through full identity reconciliation below.
            if((next.Alive && !entry.entity.Alive) || (item.reads.HeroRead() &&
                item.reads.hero.heroid!=0 && item.reads.hero.heroid!=next.HeroID)){rejectHot(11);continue;}
            if(item.reads.HeroRead()) {
                cached.heroId=next.HeroID;cached.heroValid=true;cached.heroUpdateTick=processLoopTick;
            }
            const auto& velocity=item.reads.velocity;
            const OW::Vector3 position(velocity.location.x,velocity.location.y-1.f,velocity.location.z);
            if(!OW::c_entity::IsPlausibleSpatialAnchor(position)){rejectHot(12);continue;}
            const auto delta=position-next.pos;
            next.pos=position;next.velocity={velocity.velocity.x,velocity.velocity.y,velocity.velocity.z};
            next.head_pos+=delta;next.neck_pos+=delta;next.chest_pos+=delta;
            if(OW::PresentationTiming::Fresh(next.skeleton_sample_tick_ms,item.readCompletedTick)) {
                for(size_t j=0;j<next.skeleton_bones.size();++j)if(next.skeleton_bone_valid[j])next.skeleton_bones[j]+=delta;
                if(next.cached_bot_chest_bone_valid)next.cached_bot_chest_bone+=delta;
            } else {
                next.skeleton_bone_valid.fill(false);next.cached_bot_chest_bone_valid=false;
                // Keep accepted root-relative core bounds when detailed joint samples expire.
                if(!OW::c_entity::IsPlausibleSpatialAnchor(next.head_pos) || next.head_pos.DistTo(position)>4.f)
                    next.head_pos=position+OW::Vector3(0,1.65f,0);
                if(!OW::c_entity::IsPlausibleSpatialAnchor(next.neck_pos) || next.neck_pos.DistTo(position)>4.f)
                    next.neck_pos=position+OW::Vector3(0,1.35f,0);
                if(!OW::c_entity::IsPlausibleSpatialAnchor(next.chest_pos) || next.chest_pos.DistTo(position)>4.f)
                    next.chest_pos=position+OW::Vector3(0,1.05f,0);
            }
            std::fill(next.model_shapes.valid.begin(),next.model_shapes.valid.end(),false);
            if(OW::Config::draw_hitbox && OW::ModelShapes::Supported(next.HeroID) &&
               !item.shapeBoneData.empty() && item.shapeBoneBytes==item.shapeBoneData.size() &&
               item.reads.velocity.bonedata==cached.skeletonCache.velocityBoneData &&
               cached.skeletonCache.heroId==next.HeroID) {
                next.model_shapes.modelIndex=cached.skeletonCache.shapeModelIndex;
                next.model_shapes.poses.resize(cached.skeletonCache.shapeMapped.size());
                next.model_shapes.valid.resize(cached.skeletonCache.shapeMapped.size());
                for(size_t s=0;s<next.model_shapes.poses.size();++s) {
                    int mapped=cached.skeletonCache.shapeMapped[s];
                    if(mapped<0 || size_t(mapped+1)*0x30>item.shapeBoneData.size())continue;
                    auto& pose=next.model_shapes.poses[s];
                    std::memcpy(&pose,item.shapeBoneData.data()+size_t(mapped)*0x30,sizeof(pose));
                    next.model_shapes.valid[s]=pose.Valid();
                }
                next.model_shapes.tick=item.readStartedTick;
                next.model_shapes.boneData=item.reads.velocity.bonedata;
                static DWORD lastShapeLog=0;
                if(GetTickCount()-lastShapeLog>1000) {
                    lastShapeLog=GetTickCount();
                    const auto& pose=next.model_shapes.poses[0];
                    Diagnostics::Info("Model shapes candidate hero=%llX valid=%d/%zu bone0=%d q=(%.4f,%.4f,%.4f,%.4f) scale=(%.4f,%.4f,%.4f) t=(%.4f,%.4f,%.4f)",
                        next.HeroID,int(std::count(next.model_shapes.valid.begin(),next.model_shapes.valid.end(),true)),
                        next.model_shapes.poses.size(),cached.skeletonCache.shapeMapped[0],pose.q[0],pose.q[1],pose.q[2],pose.q[3],
                        pose.scale[0],pose.scale[1],pose.scale[2],pose.translation[0],pose.translation[1],pose.translation[2]);
                }
            }
            cached.cachedModelShapes=next.model_shapes;
            if(item.RotationRead())next.Rot=item.rotation;
            if(item.viewDirectionBytes==sizeof(item.viewDirection))next.view_direction=item.viewDirection;
            if(item.boneRangeCount && (item.reads.velocity.bonedata==cached.skeletonCache.velocityBoneData)) {
                const auto rotation=DirectX::XMMatrixRotationY(next.Rot.X);
                bool any=false;
                auto boneWorld=[&](int mapped,OW::Vector3& world) {
                    for(size_t rangeIndex=0;rangeIndex<item.boneRangeCount;++rangeIndex) {
                        const auto& range=item.boneRanges[rangeIndex];
                        if(mapped<range.first || mapped>range.last || range.bytes!=range.size)continue;
                        XMFLOAT3 local{},transformed{};
                        std::memcpy(&local,item.boneData.data()+range.offset+size_t(mapped-range.first)*0x30,sizeof(local));
                        if(!OW::c_entity::IsFiniteBoneValue(local))return false;
                        DirectX::XMStoreFloat3(&transformed,DirectX::XMVector3Transform(DirectX::XMLoadFloat3(&local),rotation));
                        const float yBias=next.HeroID==OW::eHero::HERO_WRECKINGBALL?-0.7f:0.f;
                        world=OW::Vector3(transformed.x,transformed.y+yBias,transformed.z)+next.pos;
                        return OW::c_entity::IsFiniteVectorValue(world) && world!=OW::Vector3{};
                    }
                    return false;
                };
                for(size_t slot=0;slot<18;++slot)if(item.boneRequested[slot]) {
                    next.skeleton_bone_valid[slot]=boneWorld(cached.skeletonCache.mappedIndices[slot],next.skeleton_bones[slot]);
                    any|=next.skeleton_bone_valid[slot];
                }
                if(item.botChestRequested)next.cached_bot_chest_bone_valid=boneWorld(cached.skeletonCache.botChestMappedIndex,next.cached_bot_chest_bone);
                if(any) {
                    next.skeleton_sample_tick_ms=item.readStartedTick;
                    if(next.skeleton_bone_valid[0])next.head_pos=next.skeleton_bones[0];
                    if(next.skeleton_bone_valid[1])next.neck_pos=next.skeleton_bones[1];
                    if(next.skeleton_bone_valid[2])next.chest_pos=next.skeleton_bones[2];
                    cached.cachedSkeletonBones=next.skeleton_bones;cached.cachedSkeletonBoneValid=next.skeleton_bone_valid;
                    cached.cachedSkeletonPos=next.pos;cached.cachedSkeletonValid=true;
                    cached.cachedBotChestBone=next.cached_bot_chest_bone;cached.cachedBotChestBoneValid=next.cached_bot_chest_bone_valid;
                    cached.skeletonUpdateTick=item.readStartedTick;
                }
            } else if(item.boneRangeCount)cached.skeletonCache.valid=false;
            if (item.reads.visibilityRequested)
                cached.visibilitySample.Commit(OW::DecodeVisibilityRaw(item.reads.visibilityRaw),
                    item.reads.VisibilityRead(), item.readStartedTick, item.readCompletedTick);
            next.Vis = cached.visibility ? cached.visibilitySample.Visible(GetTickCount()) :
                (offset::UsesProtectedEntityLayout() && !next.HeroBase && OW::GameData::IsTrainingBotHeroId(next.HeroID));
            cached.vis = next.Vis;
            next.read_started_tick_ms=item.readStartedTick;next.read_completed_tick_ms=item.readCompletedTick;
            next.sample_time_us=item.sampleTimeUs;
            next.anchor_sample_tick_ms=item.readStartedTick;next.last_seen_tick_ms=item.readStartedTick;
            next.connection_epoch=connectionEpoch;
            next.body_observation_fence=item.bodyObservation.fence;
            next.body_absence_revision=item.bodyObservation.absence;
            next.scene_generation=processSceneGeneration;
            next.last_discovered_tick_ms=discovery->tick;
            next.body_valid=next.Alive;
            next.roster_state=next.Alive?OW::EntityRosterState::Fresh:OW::EntityRosterState::Dead;
            next.missing_since_tick_ms=0;
            if(!next.Alive && entry.entity.Alive) {
                entry.respawnWatchStartedTick=item.readCompletedTick;entry.respawnFirstScanRequested=false;
                entry.respawnLastScanRequestTick=0;sanitizeStaleRosterEntity(next);
            }
            if(next.actor_generation==0)next.actor_generation=++actorGenerationSequence;
            next.body_authority_revision=OW::AdmitEntityBodyObservation(*discovery,next.actor_generation,processSceneGeneration,
                processRegistry,item.bodyObservation,next.Alive,item.reads.healthRequested && item.reads.HealthRead(),GetTickCount());
            if(!next.body_authority_revision){rejectHot(15);continue;}
            bool resetHotHistory=false;
            if(next.roster_state==OW::EntityRosterState::Fresh && shouldSkipTeleportObservation(next,entry,item.readCompletedTick)) {
                if(entry.skippedJumpObservations++<kObservationMaxSkippedJumpFrames){rejectHot(13);continue;}
                resetHotHistory=true;
            } else entry.skippedJumpObservations=0;
            attachPreviousRenderSample(next);
            if(resetHotHistory){resetRenderHistory(next,item.readCompletedTick);entry.skippedJumpObservations=0;}
            next.team_known=cached.confirmedTeam.ConfirmIdentity(next.LinkParent,next.match_id);
            next.team_key=next.team_known?cached.confirmedTeam.key:0;
            entry.entity=next;entry.seenThisCycle=true;entry.lastHotReadTick=item.readCompletedTick;
            if(rawScanSeenComponents.contains(next.address)) {
                entry.lastScanSeenTick=discovery->tick;entry.entity.actual_scan_generation=OW::last_consumed_raw_scan_generation;
                entry.entity.actual_scan_source=consumedScanSource;
            }
            if(next.address==localCycleSnapshot.address) {
                if(OW::TryCommitLocalEntity(next,item.readCompletedTick))localCycleSnapshot=next;
            }
            item.applied=true;
            ++hotUpdated;
        }
        if(hotUpdated) {
            std::vector<OW::c_entity> hotSnapshot;
            ++entityPublishSequence;publishRosterSnapshot(GetTickCount(),0,hotSnapshot,&lifecycleStats);
            // No source read is permitted while copying the dynamic snapshot.
            std::vector<OW::hpanddy> dynamicSnapshot;
            for(const auto& [address,dynamic]:dynamicEntityCache) {
                if(!dynamic.valid || GetTickCount()-dynamic.updateTick>1000 || !scheduledRawComponents.contains(address))continue;
                OW::hpanddy value{};value.entityid=dynamic.entityId;value.MeshBase=dynamic.meshBase;
                value.POS=dynamic.pos;value.scene_generation=processSceneGeneration;dynamicSnapshot.push_back(value);
            }
            size_t hotPublishedCount=0;
            hotFramePublished=OW::TargetingDetail::TryPublishEntitySnapshots(std::move(hotSnapshot),std::move(dynamicSnapshot),connectionEpoch,hotPublishedCount,&processRegistry,processSceneGeneration);
        }
        };
        if (!hotFieldBatchItems.empty()) {
            for(auto& item:hotFieldBatchItems)
                item.bodyObservation=OW::CaptureEntityBodyObservation(item.componentParent);
            const auto hotStarted=std::chrono::steady_clock::now();
            const DWORD started=GetTickCount();
            bool ok=false;
            {
                ScopedPipelinePhaseTimer timer(pipelineStats.phase.hotScatterExecuteMs);
                Diagnostics::ScopedDmaCallsite tag(Diagnostics::DmaCallsite::EntityHotScatter);
                ok=mem.ExecuteReadScatter(hotFieldScatter);
            }
            const DWORD completed=GetTickCount();
            ++pipelineStats.hotScatterExecuteCount; ++dmaFieldWindow.hotScatterExecute;
            if(!ok){++pipelineStats.hotScatterExecuteFailCount; ++dmaFieldWindow.hotScatterExecuteFail;}
            for(auto& item:hotFieldBatchItems) {
                item.readStartedTick=started; item.readCompletedTick=completed;
                item.sampleTimeUs=RuntimeTrace::NowUs();
                item.reads.scatterExecuted=false; item.reads.scatterOk=ok && item.prepared;
                recordHotScatterReadResults(item.reads);
                if(item.reads.VelocityRead()) ++successfulSamples[item.componentParent];
            }
            if(ok) publishHotActors();
            const double hotMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-hotStarted).count();
            hotTotalMs+=hotMs; hotMaxMs=(std::max)(hotMaxMs,hotMs); ++measuredCycles;
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-measurementStart).count();
            if(elapsed>=5) {
                Diagnostics::Info("[UNDMA-V2] target_hz=%d cycles=%zu actual_cycle_hz=%.2f entities=%zu hot_mean_ms=%.3f hot_max_ms=%.3f",
                    V2::Acquisition::kTargetHz,measuredCycles,measuredCycles/elapsed,hotFieldBatchItems.size(),hotTotalMs/measuredCycles,hotMaxMs);
                for(const auto& [address,count]:successfulSamples)
                    Diagnostics::Info("[UNDMA-V2-SAMPLE] entity=0x%llX samples=%llu hz=%.2f",address,count,count/elapsed);
                successfulSamples.clear(); measuredCycles=0; hotTotalMs=0; hotMaxMs=0;
                measurementStart=std::chrono::steady_clock::now();
            }
        }
        StepAcquisitionDiscovery();
#include "Game/EntityMaintenance/LiveBinding.inl"
        const uint64_t maintenanceStartedUs=RuntimeTrace::NowUs();
        size_t maintenanceVisited=0;

        {
        ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.entityLoopWallMs);
        // Rotate budget priority while publishing by stable roster key. A fixed
        // first-four traversal leaves every later cached skeleton frozen.
        for (size_t order = 0; OW::RuntimeWorkersRunning() &&
                           OW::ProcessConnection::IsConnected() && order < raw_entities.size(); ++order) {
            if(order>0 && RuntimeTrace::NowUs()-maintenanceStartedUs>=1000)break;
            const size_t i = (entityLoopStart + order) % raw_entities.size();
            ++maintenanceVisited;
            maintenanceCursor=(i+1)%raw_entities.size();
            OW::c_entity entity{};
            const bool progressLog = detailedProcessLog && (i < 3 || (i % 16) == 0);
            uint64_t ComponentParent = 0;
            uint64_t LinkParent = 0;
            bool dynamicCacheExpiredByLinkChange = false;
            {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.entityLoopSetupMs);
            if (progressLog) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu/%zu start component=0x%llX link=0x%llX.",
                    i,
                    raw_entities.size(),
                    static_cast<unsigned long long>(raw_entities[i].first),
                    static_cast<unsigned long long>(raw_entities[i].second));
            }
            if (!raw_entities[i].first || !raw_entities[i].second) {
                processStats.nullPair++;
                Diagnostics::RecordInvalidEntity();
                continue;
            }

            ComponentParent = raw_entities[i].first;
            LinkParent = raw_entities[i].second;
            entity.address = ComponentParent;
            entity.LinkParent = LinkParent;
            auto dynamicCacheIt = dynamicEntityCache.find(ComponentParent);
            if (dynamicCacheIt != dynamicEntityCache.end() &&
                dynamicCacheIt->second.linkParent != LinkParent) {
                ++lifecycleStats.dynamicCacheExpiredCount;
                dynamicCacheExpiredByLinkChange = true;

                dynamicEntityCache.erase(dynamicCacheIt);
                dynamicCacheIt = dynamicEntityCache.end();
            }
            if (dynamicCacheIt != dynamicEntityCache.end() && dynamicCacheIt->second.valid) {
                uint64_t identityPointer=0,currentId=0;
                if(!SDK->read_range(ComponentParent+0x30,&identityPointer,sizeof(identityPointer)))continue;
                identityPointer&=0xFFFFFFFFFFFFFFC0;
                if(!identityPointer||!SDK->read_range(identityPointer+0x10,&currentId,sizeof(currentId)))continue;
                if(currentId!=dynamicCacheIt->second.entityId) {
                    dynamicEntityCache.erase(dynamicCacheIt);continue;
                }
                ++lifecycleStats.dynamicCacheReusedCount;
                DynamicEntityCache& cachedDynamic = dynamicCacheIt->second;
                if (cachedDynamic.meshBase &&
                    (cachedDynamic.lastAttemptTick == 0 ||
                     processLoopTick - cachedDynamic.lastAttemptTick >= OW::kEntityProcessIntervalMs)) {
                    cachedDynamic.lastAttemptTick=processLoopTick;
                    OW::velocity_compo_t hpdyVelocity{};
                    if(SDK->read_range(cachedDynamic.meshBase+0x200,&hpdyVelocity.location,sizeof(hpdyVelocity.location)) &&
                        std::isfinite(hpdyVelocity.location.x)&&std::isfinite(hpdyVelocity.location.y)&&std::isfinite(hpdyVelocity.location.z) &&
                        (hpdyVelocity.location.x!=0||hpdyVelocity.location.y!=0||hpdyVelocity.location.z!=0)) {
                        cachedDynamic.pos=hpdyVelocity.location;cachedDynamic.updateTick=processLoopTick;
                    }
                }

                OW::hpanddy hpdyentity{};
                hpdyentity.scene_generation=processSceneGeneration;
                hpdyentity.object_parent=ComponentParent;hpdyentity.position_tick_ms=cachedDynamic.updateTick;
                if(!hpdyentity.position_tick_ms||DWORD(processLoopTick-hpdyentity.position_tick_ms)>200)continue;
                hpdyentity.entityid = cachedDynamic.entityId;
                hpdyentity.MeshBase = cachedDynamic.meshBase;
                hpdyentity.POS = cachedDynamic.pos;
                hpdy_entities.push_back(hpdyentity);

                processStats.dynamic++;
                continue;
            }
            }

#include "Game/EntityMaintenance/CacheAdmission.inl"

            if (componentCacheHit) {
                dmaFieldWindow.componentCacheHit++;
                pipelineStats.baseCacheHitCount++;
                lifecycleStats.componentCacheHitCount++;
            } else {
                dmaFieldWindow.componentCacheMiss++;
                pipelineStats.baseCacheMissCount++;
                lifecycleStats.componentCacheMissCount++;
            }

            OW::EntityHeaderSnapshot componentHeader{};
            OW::EntityHeaderSnapshot linkHeader{};
            const OW::EntityHeaderSnapshot* componentSnapshot = nullptr;
            const OW::EntityHeaderSnapshot* linkSnapshot = nullptr;

            {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.entityHeaderSpecialMs);
            Diagnostics::ScopedDmaCallsite dmaTag(
                Diagnostics::DmaCallsite::EntityHeaderSpecial);
            if (!componentCacheHit) {
                dmaFieldWindow.componentHeaderRead++;
                {
                    ScopedPipelinePhaseTimer headerTimer(pipelineStats.phase.entityHeaderComponentMs);
                    if (!componentHeader.Read(ComponentParent))
                        dmaFieldWindow.componentHeaderReadFail++;
                }
                componentSnapshot = componentHeader.valid ? &componentHeader : nullptr;
                linkSnapshot = componentSnapshot;
                if (LinkParent != ComponentParent) {
                    dmaFieldWindow.linkHeaderRead++;
                    {
                        ScopedPipelinePhaseTimer headerTimer(pipelineStats.phase.entityHeaderLinkMs);
                        if (!linkHeader.Read(LinkParent))
                            dmaFieldWindow.linkHeaderReadFail++;
                    }
                    linkSnapshot = linkHeader.valid ? &linkHeader : nullptr;
                }
                if (progressLog) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu headers component_valid=%d link_valid=%d cache_hit=0.",
                        i,
                        componentHeader.valid ? 1 : 0,
                        linkSnapshot ? 1 : 0);
                }
            } else if (progressLog) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu headers cache_hit=1.", i);
            }

            // Check for special entity IDs (HP packs, Bob, etc.)
            if (!componentCacheHit) {
                ScopedPipelinePhaseTimer specialTimer(pipelineStats.phase.entitySpecialProbeMs);
                uint64_t ptrValue = 0;
                if (!componentHeader.ReadParentOffset(0x30, ptrValue))
                    if(!SDK->read_range(ComponentParent+0x30,&ptrValue,sizeof(ptrValue)))continue;
                uint64_t Ptr = ptrValue & 0xFFFFFFFFFFFFFFC0;
                if (Ptr && Ptr < 0xFFFFFFFFFFFFFFEF) {
                    uint64_t EntityID=0;
                    if(!SDK->read_range(Ptr+0x10,&EntityID,sizeof(EntityID)))continue;
                    if(V2::VisualSettings::ObjectIndex(EntityID)>=0) {
                        OW::hpanddy hpdyentity{};
                        hpdyentity.scene_generation=processSceneGeneration;
                        hpdyentity.entityid = EntityID;
                        hpdyentity.MeshBase = OW::DecryptComponent(
                            ComponentParent, OW::TYPE_VELOCITY, componentSnapshot);
                        hpdyentity.object_parent=ComponentParent;
                        XMFLOAT3 position{};
                        if(!hpdyentity.MeshBase||!SDK->read_range(hpdyentity.MeshBase+0x200,&position,sizeof(position)) ||
                            !std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z)||
                            (position.x==0&&position.y==0&&position.z==0))continue;
                        hpdyentity.POS=position;hpdyentity.position_tick_ms=processLoopTick;
                        DynamicEntityCache dynamicCache{};
                        dynamicCache.linkParent = LinkParent;
                        dynamicCache.valid = true;
                        dynamicCache.entityId = EntityID;
                        dynamicCache.meshBase = hpdyentity.MeshBase;
                        dynamicCache.pos = hpdyentity.POS;
                        dynamicCache.updateTick = dynamicCache.lastAttemptTick = processLoopTick;
                        const bool dynamicCacheReplacing =
                            dynamicCacheExpiredByLinkChange ||
                            dynamicEntityCache.find(ComponentParent) != dynamicEntityCache.end();
                        if (dynamicCacheReplacing)
                            ++lifecycleStats.dynamicCacheReplacedCount;
                        else
                            ++lifecycleStats.dynamicCacheCreatedCount;
                        dynamicEntityCache.insert_or_assign(ComponentParent, dynamicCache);

                        hpdy_entities.push_back(hpdyentity);
                        processStats.dynamic++;
                        continue;
                    }
                }
            }
            }

            // Renew the whole set together; never combine two component allocations.
            if (!componentCacheHit) {
                ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.baseDecryptMs);
                Diagnostics::ScopedDmaCallsite dmaTag(
                    Diagnostics::DmaCallsite::EntityBaseDecrypt);
                constexpr double kBaseDecryptSlowCallThresholdMs = 50.0;
                auto decryptBase = [&](uint64_t parent,
                                       uint32_t type,
                                       const OW::EntityHeaderSnapshot* snapshot,
                                       bool fallback = false) {
                    ++pipelineStats.baseDecryptAttemptCount;
                    BaseDecryptCycleKey decryptKey{ parent, type };
                    auto keyIt = baseDecryptCycleKeyCounts.find(decryptKey);
                    if (keyIt == baseDecryptCycleKeyCounts.end()) {
                        keyIt = baseDecryptCycleKeyCounts.emplace(decryptKey, 0).first;
                        ++pipelineStats.baseDecryptUniqueKeyCount;
                    } else {
                        ++pipelineStats.baseDecryptDuplicateKeyCount;
                    }
                    const size_t keyCount = ++keyIt->second;
                    if (keyCount > pipelineStats.baseDecryptMaxDuplicateKeyCount) {
                        pipelineStats.baseDecryptMaxDuplicateKeyCount = keyCount;
                        pipelineStats.baseDecryptMaxDuplicateKeyType = type;
                        pipelineStats.baseDecryptMaxDuplicateKeyParent = parent;
                    }
                    if (fallback)
                        ++pipelineStats.baseDecryptFallbackAttemptCount;
                    const auto decryptStartedAt = std::chrono::steady_clock::now();
                    const uint64_t value = OW::DecryptComponent(parent, type, snapshot, true);
                    const double decryptElapsedMs = static_cast<double>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - decryptStartedAt).count()) / 1000.0;
                    if (decryptElapsedMs >= kBaseDecryptSlowCallThresholdMs)
                        ++pipelineStats.baseDecryptSlowCallCount;
                    if (decryptElapsedMs > pipelineStats.baseDecryptMaxCallMs) {
                        pipelineStats.baseDecryptMaxCallMs = decryptElapsedMs;
                        pipelineStats.baseDecryptMaxCallType = type;
                        pipelineStats.baseDecryptMaxCallParent = parent;
                        pipelineStats.baseDecryptMaxCallSuccess = value != 0;
                    }
                    if (value) {
                        ++pipelineStats.baseDecryptSuccessCount;
                        if (fallback)
                            ++pipelineStats.baseDecryptFallbackSuccessCount;
                    } else {
                        ++pipelineStats.baseDecryptFailCount;
                        if (fallback)
                            ++pipelineStats.baseDecryptFallbackFailCount;
                    }
                    return value;
                };
#include "Game/EntityMaintenance/CacheRebuild.inl"

            }

            {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.entityCacheApplyMs);
            if (cacheIt->second->matchIdValid) {
                entity.match_id = cacheIt->second->matchId;
            } else {
                {
                    ScopedPipelinePhaseTimer matchIdTimer(pipelineStats.phase.entityCacheMatchIdMs);
                    const bool matchRead=componentSnapshot ? componentSnapshot->ReadParentOffset(offset::Entity_MatchId,entity.match_id) :
                        SDK->read_range(entity.address+offset::Entity_MatchId,&entity.match_id,sizeof(entity.match_id));
                    if(!matchRead || !entity.match_id) {
                        cacheIt->second->baseUpdateTick=0;
                        requestTopologyRescan(processLoopTick);
                        continue;
                    }
                }
                cacheIt->second->matchId = entity.match_id;
                cacheIt->second->matchIdValid = true;
            }
            RosterEntry& identityCache = *cacheIt->second;
            uint32_t controllerUid=0;
            const bool controllerRead=linkSnapshot ? linkSnapshot->ReadParentOffset(offset::Entity_MatchId,controllerUid) :
                SDK->read_range(LinkParent+offset::Entity_MatchId,&controllerUid,sizeof(controllerUid));
            if(!controllerRead || !controllerUid)continue;
            if(identityCache.participantController!=LinkParent || identityCache.participantControllerUid!=controllerUid) {
                if(identityCache.participantController && identityCache.entity.address) {
                    OW::RevokeEntityBodyObservation(identityCache.entity.address,identityCache.entity.actor_generation,
                        identityCache.entity.body_authority_revision);
                    identityCache.entity.body_valid=false;identityCache.healthValid=false;
                }
                identityCache.participantKey=0;identityCache.participantLabel.clear();
                identityCache.participantIdentityValid=false;identityCache.participantIdentityUpdateTick=0;
                identityCache.participantController=LinkParent;identityCache.participantControllerUid=controllerUid;
            }
            if (!identityCache.participantIdentityValid && identityCache.stat &&
                (identityCache.participantIdentityUpdateTick == 0 ||
                 processLoopTick - identityCache.participantIdentityUpdateTick >= 1000)) {
                identityCache.participantIdentityUpdateTick = processLoopTick;
                std::string participantId;
                if(OW::ReadParticipantLabel(identityCache.stat,
                    [&](uint64_t at,void* out,size_t size){return SDK->read_range(at,out,size);},participantId)) {
                    identityCache.participantKey=OW::EntityRosterPolicy::ParticipantRosterKey(participantId);
                    identityCache.participantLabel=std::move(participantId);
                    identityCache.participantIdentityValid=true;
                }
            }
            entity.statcombase = identityCache.stat;
            entity.participant_key = identityCache.participantKey;
            entity.roster_key = entity.participant_key != 0
                ? entity.participant_key
                : makeRosterKey(entity.match_id, LinkParent, ComponentParent);
            // Participant collisions are resolved after the hot reads below; defer
            // their record write so a late dead actor cannot overwrite the winner.
            if (entity.participant_key == 0) {
                ScopedPipelinePhaseTimer recordTimer(pipelineStats.phase.entityCacheRecordUpdateMs);

            }

            cacheIt->second->ApplyBases(entity);
            if (progressLog) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu decrypt health=0x%llX link=0x%llX velocity=0x%llX hero=0x%llX bone=0x%llX angle=0x%llX.",
                    i,
                    static_cast<unsigned long long>(entity.HealthBase),
                    static_cast<unsigned long long>(entity.LinkBase),
                    static_cast<unsigned long long>(entity.VelocityBase),
                    static_cast<unsigned long long>(entity.HeroBase),
                    static_cast<unsigned long long>(entity.BoneBase),
                    static_cast<unsigned long long>(entity.AngleBase));
            }
            if (!entity.LinkBase) {
                cacheIt->second->baseUpdateTick = 0;
                processStats.linkBaseFail++;
                requestTopologyRescan(processLoopTick);
            }

            // Skip duplicates
            if (entity == lastentity) {
                processStats.duplicate++;
                continue;
            }
            lastentity = entity;
            }

            RosterEntry& componentCache = *cacheIt->second;
            const auto hotAppliedItem=hotFieldBatchIndex.find(ComponentParent);
            const bool hotAlreadyApplied=componentCacheHit && hotAppliedItem!=hotFieldBatchIndex.end() && hotFieldBatchItems[hotAppliedItem->second].applied &&
                entity.roster_key==cacheIt->second->entity.roster_key && entity.participant_key==cacheIt->second->entity.participant_key;
            if(hotAlreadyApplied)entity=cacheIt->second->entity;
            bool refreshHealth = false;
            bool refreshHero = false;
            bool scatterVisibility = false;
            uint64_t visibilityValueAddress = 0;
            EntityHotFieldReads hotReads{};
            OW::velocity_fields_t velo_compo{};
            bool velocityRead = false;
            if(hotAlreadyApplied) {
                velo_compo=hotFieldBatchItems[hotAppliedItem->second].reads.velocity;
                velocityRead=true;
            }
            if(!hotAlreadyApplied) {
            {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.entityHotFieldsMs);
            Diagnostics::ScopedDmaCallsite dmaTag(
                Diagnostics::DmaCallsite::EntityHotFields);
#include "Game/EntityMaintenance/HealthDue.inl"
            refreshHero =
                entity.HeroBase &&
                (!componentCache.heroValid ||
                 componentCache.heroUpdateTick == 0 ||
                 processLoopTick - componentCache.heroUpdateTick >= OW::kEntityHeroIntervalMs);
            const auto& activeOffsets = offset::Active();
            scatterVisibility =
                activeOffsets.VisibilityValueOffset != 0 &&
                entity.VisBase != 0 && componentCache.visibilitySample.Due(processLoopTick);
            visibilityValueAddress = scatterVisibility
                ? entity.VisBase + activeOffsets.VisibilityValueOffset
                : 0;
                bool hotReadsFromBatch = false;
                if (componentCacheHit) {
                    const auto hotBatchIt = hotFieldBatchIndex.find(ComponentParent);
                    if (hotBatchIt != hotFieldBatchIndex.end()) {
                        HotFieldBatchItem& item = hotFieldBatchItems[hotBatchIt->second];
                        if (item.linkParent == LinkParent) {
                            hotReads = item.reads;
                            hotReadsFromBatch = true;
                            entity.body_observation_fence=item.bodyObservation.fence;
                            entity.body_absence_revision=item.bodyObservation.absence;
                            entity.read_started_tick_ms = item.readStartedTick;
                            entity.read_completed_tick_ms = item.readCompletedTick;
                        }
                    }
                }
                if (!hotReadsFromBatch) {
                    const auto observation=OW::CaptureEntityBodyObservation(ComponentParent);
                    entity.body_observation_fence=observation.fence;entity.body_absence_revision=observation.absence;
                    entity.read_started_tick_ms = GetTickCount();
                    hotReads = readHotFields(
                        hotFieldScatter,
                        entity.VelocityBase,
                        entity.VelocityBase != 0,
                        entity.HealthBase,
                        refreshHealth,
                        entity.HeroBase,
                        refreshHero,
                        visibilityValueAddress,
                        scatterVisibility);
                    entity.read_completed_tick_ms = GetTickCount();
                    entity.sample_time_us=RuntimeTrace::NowUs();
                }

#include "Game/EntityMaintenance/HotResults.inl"
            }

#include "Game/EntityMaintenance/HealthStep.inl"

            {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.entityRotationPositionMs);
            Diagnostics::ScopedDmaCallsite dmaTag(
                Diagnostics::DmaCallsite::EntityRotationPosition);
            entity.last_seen_tick_ms = processLoopTick;
            entity.missing_since_tick_ms = 0;
            entity.roster_state = entity.Alive
                ? OW::EntityRosterState::Fresh
                : OW::EntityRosterState::Dead;
            if (entity.roster_state == OW::EntityRosterState::Dead)
                sanitizeStaleRosterEntity(entity);

            // ---- Rotation ----
            bool rotationReadFromBatch = false;
            {
                const auto hotBatchIt = hotFieldBatchIndex.find(ComponentParent);
                if (componentCacheHit && hotBatchIt != hotFieldBatchIndex.end()) {
                    HotFieldBatchItem& item = hotFieldBatchItems[hotBatchIt->second];
                    if (item.reads.scatterOk &&
                        item.linkParent == LinkParent &&
                        item.RotationRead()) {
                        entity.Rot = item.rotation;
                        rotationReadFromBatch = true;
                    } else if (item.linkParent == LinkParent && item.rotationRequested) {
                        cacheIt->second->rotationLeaf = 0;
                    }
                }
            }
            if (!rotationReadFromBatch && entity.RotationBase) {
                uint64_t rotPtr = SDK->RPM<uint64_t>(entity.RotationBase + offset::RotationBase_Sub1);
                if (rotPtr) {
                    const Vector3 rotation =
                        SDK->RPM<Vector3>(rotPtr + offset::RotationBase_Sub2);
                    if (std::isfinite(rotation.X) &&
                        std::isfinite(rotation.Y) &&
                        std::isfinite(rotation.Z)) {
                        entity.Rot = rotation;
                        cacheIt->second->rotationLeaf = rotPtr;
                    } else if (criticalBatchFusionActive) {
                        cacheIt->second->rotationLeaf = 0;
                    }
                } else if (criticalBatchFusionActive) {
                    cacheIt->second->rotationLeaf = 0;
                }
            }

            // ---- Velocity / position / bones ----
            if (offset::UsesProtectedEntityLayout() && !velocityRead) {
                // A failed read cannot refresh coordinates from zero-padded
                // typed reads. The roster keeps the preceding sample's age.
                Diagnostics::RecordInvalidEntity();
                continue;
            }
            if (velocityRead) {
                entity.pos      = Vector3(velo_compo.location.x, velo_compo.location.y - 1.f, velo_compo.location.z);
                entity.velocity = Vector3(velo_compo.velocity.x, velo_compo.velocity.y, velo_compo.velocity.z);
                if(offset::UsesProtectedEntityLayout() && !OW::c_entity::IsPlausibleSpatialAnchor(entity.pos)) {
                    requestTopologyRescan(processLoopTick);
                    Diagnostics::RecordInvalidEntity();
                    continue;
                }
                entity.anchor_sample_tick_ms = entity.read_started_tick_ms;
                if(!OW::PresentationTiming::ObservationWindowFresh32(
                    entity.read_started_tick_ms,entity.read_completed_tick_ms,GetTickCount())) {
                    RuntimeTrace::Emit(RuntimeTrace::EntityReject,entity.address,entity.read_started_tick_ms,entity.read_completed_tick_ms,2);continue;
                }
            }
            if (!offset::UsesProtectedEntityLayout()) {
                XMFLOAT3 transformPosition{};
                if (cacheIt->second->transform &&
                    SDK->read_range(cacheIt->second->transform + 0x3D0, &transformPosition, sizeof(transformPosition))) {
                    const OW::Vector3 candidate(
                        transformPosition.x,
                        transformPosition.y,
                        transformPosition.z);
                    if (OW::c_entity::IsPlausibleSpatialAnchor(candidate))
                        entity.pos = candidate;
                }
                if (!OW::c_entity::IsPlausibleSpatialAnchor(entity.pos)) {
                    Diagnostics::RecordInvalidEntity();
                    continue;
                }
            }
            }

            // ---- Hero ID ----
            {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.heroMs);
            if (entity.HeroBase) {
                if (refreshHero) {
                    dmaFieldWindow.heroRequested++;
                    uint64_t heroId = hotReads.hero.heroid;
                    bool heroRead = hotReads.HeroRead();
                    if (heroRead) {
                        dmaFieldWindow.heroScatterHit++;
                        pipelineStats.hotScatterSuccessCount++;
                    } else if (hotReads.heroRequested && hotReads.scatterOk) {
                        dmaFieldWindow.heroScatterPartial++;
                        pipelineStats.hotScatterPartialCount++;
                    }
                    if (!heroRead) {
                        dmaFieldWindow.heroFallback++;
                        pipelineStats.hotScatterFallbackReadCount++;
                        heroRead =
                            SDK->read_range(entity.HeroBase + 0xD0, &heroId, sizeof(heroId));
                    }
                    if (heroRead) {
                        const uint64_t newHeroId = heroId;
                        if (offset::Active().componentTransform == offset::ComponentTransformMode::Cn155284 &&
                            !Cn155284::HeroId(newHeroId)) {
                            requestTopologyRescan(processLoopTick);

                            OW::RevokeEntityBodyObservation(cacheIt->second->entity.address,
                                cacheIt->second->entity.actor_generation,cacheIt->second->entity.body_authority_revision);
                            cacheIt->second->entity.body_valid=false;
                            componentCache.baseUpdateTick=0;
                            Diagnostics::RecordInvalidEntity();
                            continue;
                        }
                        if (newHeroId != 0) {
                            uint64_t previousHeroId = componentCache.heroValid
                                ? componentCache.heroId
                                : 0;
                            // A fresh component set already discarded the old rig and skills.
                            const bool heroChanged =
                                previousHeroId != 0 &&
                                previousHeroId != newHeroId;
                            if (heroChanged) {
                                ++lifecycleStats.componentCacheInvalidateHeroChangeCount;
                                requestTopologyRescan(processLoopTick);
                                componentCache.visibilitySample = {};
                                componentCache.nameTeamValid = false;
                                componentCache.nameTeamUpdateTick = 0;
                                componentCache.skillDeadlines = {};
                                componentCache.skillValid = false;
                                componentCache.skillUpdateTick = 0;
                                componentCache.localSkillValid = false;
                                componentCache.localSkillUpdateTick = 0;
                                componentCache.heroName = "Unknown";
                                componentCache.skill1act = false;
                                componentCache.skillActiveTick = {};
                                componentCache.skill2act = false;
                                componentCache.ultimate = 0.0f;
                                componentCache.ultimateValid = false;
                                componentCache.displayUltimateTick = 0;
                                componentCache.displayCooldowns = {};
                                componentCache.cooldownTimers = {};
                                componentCache.skillRefreshCursor = 0;
                                componentCache.skillcd1 = 0.0f;
                                componentCache.skillcd2 = 0.0f;
                                componentCache.skeletonCache = OW::c_entity::SkeletonBoneCache{};
                                ++heroChangedThisCycle;
                                // Reconcile the complete component set before using the new hero's body.
                                componentCache.baseUpdateTick = 0;
                                OW::RevokeEntityBodyObservation(entity.address, cacheIt->second->entity.actor_generation,
                                    cacheIt->second->entity.body_authority_revision);
                                cacheIt->second->entity.body_valid=false;
                                if (OW::PipelineDebugEnabled()) {
                                    Diagnostics::Info("[PIPELINE] Stage 4 hero_change roster=0x%llX component=0x%llX old=0x%llX new=0x%llX.",
                                        static_cast<unsigned long long>(entity.roster_key),
                                        static_cast<unsigned long long>(ComponentParent),
                                        static_cast<unsigned long long>(previousHeroId),
                                        static_cast<unsigned long long>(newHeroId));
                                }
                                continue;
                            }
                            componentCache.heroId = newHeroId;
                            componentCache.heroValid = true;
                            entity.HeroID = newHeroId;
                        } else if (componentCache.heroValid) {
                            entity.HeroID = componentCache.heroId;
                        }
                        componentCache.heroUpdateTick = processLoopTick;
                    } else if (componentCache.heroValid) {
                        dmaFieldWindow.heroReadFail++;
                        entity.HeroID = componentCache.heroId;
                    } else {
                        dmaFieldWindow.heroReadFail++;
                        requestTopologyRescan(processLoopTick);
                    }
                } else {
                    entity.HeroID = componentCache.heroId;
                }
            } else {
                dmaFieldWindow.heroBaseMissing++;
                processStats.heroBaseMissing++;
                // Fallback: identify by MaxHealth
                if (entity.MaxHealth == 225) {
                    XMFLOAT3 temppos = velocityRead
                        ? velo_compo.location
                        : SDK->RPM<XMFLOAT3>(entity.VelocityBase + 0x380 + 0x50);
                    entity.head_pos = Vector3(temppos.x, temppos.y + 1.f, temppos.z);
                    entity.HeroID = 0x16dd; // TOBTERT
                    entity.neck_pos = entity.head_pos;
                    entity.chest_pos = entity.head_pos;
                    entity.pos = entity.neck_pos;
                } else if (entity.MaxHealth == 30) {
                    XMFLOAT3 temppos = velocityRead
                        ? velo_compo.location
                        : SDK->RPM<XMFLOAT3>(entity.VelocityBase + 0x380 + 0x50);
                    entity.head_pos = Vector3(temppos.x, temppos.y, temppos.z);
                    entity.HeroID = 0x16ee; // SYMTERT
                    entity.neck_pos = entity.head_pos;
                    entity.chest_pos = entity.head_pos;
                    entity.pos = entity.neck_pos;
                } else if (entity.MaxHealth == 1000) {
                    entity.HeroID = 0x16bb; // Bob
                } else if (offset::UsesProtectedEntityLayout() &&
                           std::fabs(entity.PlayerHealthMax - 200.0f) <= 5.0f) {
                    XMFLOAT3 temppos = velocityRead
                        ? velo_compo.location
                        : SDK->RPM<XMFLOAT3>(entity.VelocityBase + 0x380 + 0x50);
                    entity.HeroID = OW::eHero::HERO_TRAININGBOT4;
                    const float baseY = temppos.y - OW::offset::Velocity_LocationYBias;
                    entity.pos = Vector3(temppos.x, baseY, temppos.z);
                    entity.head_pos = Vector3(temppos.x, baseY + 1.45f, temppos.z);
                    entity.neck_pos = Vector3(temppos.x, baseY + 1.10f, temppos.z);
                    entity.chest_pos = Vector3(temppos.x, baseY + 0.75f, temppos.z);
                    entity.cached_bot_chest_bone = entity.chest_pos;
                    entity.cached_bot_chest_bone_valid = true;
                } else if (offset::UsesProtectedEntityLayout() &&
                           std::fabs(entity.PlayerHealthMax - 500.0f) <= 10.0f) {
                    XMFLOAT3 temppos = velocityRead
                        ? velo_compo.location
                        : SDK->RPM<XMFLOAT3>(entity.VelocityBase + 0x380 + 0x50);
                    entity.HeroID = OW::eHero::HERO_TRAININGBOT5;
                    const float baseY = temppos.y - OW::offset::Velocity_LocationYBias;
                    entity.pos = Vector3(temppos.x, baseY, temppos.z);
                    entity.head_pos = Vector3(temppos.x, baseY + 2.00f, temppos.z);
                    entity.neck_pos = Vector3(temppos.x, baseY + 1.55f, temppos.z);
                    entity.chest_pos = Vector3(temppos.x, baseY + 1.05f, temppos.z);
                    entity.cached_bot_chest_bone = entity.chest_pos;
                    entity.cached_bot_chest_bone_valid = true;
                } else {
                    dmaFieldWindow.heroFallbackFail++;
                    processStats.heroFallbackFail++;
                    requestTopologyRescan(processLoopTick);
                    Diagnostics::RecordInvalidEntity();
                    continue;
                }
            }
            }
            if (progressLog) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu hero=0x%llX health=%.1f pos=(%.2f,%.2f,%.2f).",
                    i,
                    static_cast<unsigned long long>(entity.HeroID),
                    entity.PlayerHealth,
                    entity.pos.X,
                    entity.pos.Y,
                    entity.pos.Z);
            }

            }
            const bool syntheticCnTrainingBot =
                offset::UsesProtectedEntityLayout() &&
                !entity.HeroBase &&
                OW::GameData::IsTrainingBotHeroId(entity.HeroID);

            {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.skeletonMs);
            if (entity.VelocityBase &&
                entity.HeroID != 0x16dd &&
                entity.HeroID != 0x16ee &&
                !syntheticCnTrainingBot) {
                processStats.boneCandidates++;
                if (entity.BoneBase)
                    processStats.boneBaseNonZero++;

                ComponentBaseCache& skeletonSlowCache = *cacheIt->second;
                constexpr double kSkeletonSlowCallThresholdMs = 50.0;
                auto recordSkeletonCall = [&](double elapsedMs,
                                              uint32_t op,
                                              uint64_t observedVelocityBoneData,
                                              bool cacheHit,
                                              bool success) {
                    if (elapsedMs >= kSkeletonSlowCallThresholdMs)
                        ++pipelineStats.skeletonSlowCallCount;
                    if (elapsedMs > pipelineStats.skeletonMaxCallMs) {
                        pipelineStats.skeletonMaxCallMs = elapsedMs;
                        pipelineStats.skeletonMaxCallOp = op;
                        pipelineStats.skeletonMaxCallHeroId = entity.HeroID;
                        pipelineStats.skeletonMaxCallEntity = entity.address;
                        pipelineStats.skeletonMaxCallBoneBase = entity.BoneBase;
                        pipelineStats.skeletonMaxCallVelocityBase = entity.VelocityBase;
                        pipelineStats.skeletonMaxCallVelocityBoneData = observedVelocityBoneData;
                        pipelineStats.skeletonMaxCallCacheHit = cacheHit;
                        pipelineStats.skeletonMaxCallCacheValid =
                            skeletonSlowCache.skeletonCache.valid;
                        pipelineStats.skeletonMaxCallFallback =
                            !skeletonSlowCache.skeletonCache.valid ||
                            skeletonSlowCache.skeletonCache.maxMappedIndex < 0;
                        pipelineStats.skeletonMaxCallMaxMappedIndex =
                            skeletonSlowCache.skeletonCache.maxMappedIndex;
                        pipelineStats.skeletonMaxCallSuccess = success;
                    }
                };

                uint64_t velocityBoneData = 0;
                if (velocityRead) {
                    velocityBoneData = velo_compo.bonedata;
                } else {
                    const auto velocityReadStartedAt = std::chrono::steady_clock::now();
                    velocityBoneData = SDK->RPM<uint64_t>(entity.VelocityBase + 0x8B0);
                    const double velocityReadMs = static_cast<double>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - velocityReadStartedAt).count()) /
                        1000.0;
                    pipelineStats.phase.skeletonVelocityReadMs += velocityReadMs;
                    recordSkeletonCall(velocityReadMs, 1, velocityBoneData, false, velocityBoneData != 0);
                }

                if (detailedProcessLog) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu bone_start hero=0x%llX velocity=0x%llX bone=0x%llX vbd=0x%llX bones_base=0x%llX.",
                        i,
                        static_cast<unsigned long long>(entity.HeroID),
                        static_cast<unsigned long long>(entity.VelocityBase),
                        static_cast<unsigned long long>(entity.BoneBase),
                        static_cast<unsigned long long>(velocityBoneData),
                        0ull);
                }
                const bool forceLocalSkeletonRefresh = entity.AngleBase != 0;
                const auto hotSkeletonItem=hotFieldBatchIndex.find(ComponentParent);
                const bool hotSkeletonCurrent=hotSkeletonItem!=hotFieldBatchIndex.end() && skeletonSlowCache.cachedSkeletonValid &&
                    skeletonSlowCache.skeletonUpdateTick==hotFieldBatchItems[hotSkeletonItem->second].readStartedTick;
                const bool refreshSkeleton = !hotSkeletonCurrent &&
                    (forceLocalSkeletonRefresh ||
                    (OW::PresentationTiming::BudgetEligible(i,raw_entities.size(),entityLoopStart,skeletonBudgetForCycle) &&
                     consumeCycleBudget(skeletonRefreshBudget)));
                if (!refreshSkeleton) {
                    if(skeletonSlowCache.cachedSkeletonValid &&
                        OW::PresentationTiming::Fresh(skeletonSlowCache.skeletonUpdateTick,GetTickCount())) {
                    entity.skeleton_sample_tick_ms=skeletonSlowCache.skeletonUpdateTick;
                    entity.model_shapes = skeletonSlowCache.cachedModelShapes;
                    if(entity.model_shapes.boneData!=velocityBoneData)entity.model_shapes={};
                    entity.skeleton_bones = skeletonSlowCache.cachedSkeletonBones;
                    entity.skeleton_bone_valid = skeletonSlowCache.cachedSkeletonBoneValid;
                    entity.cached_bot_chest_bone = skeletonSlowCache.cachedBotChestBone;
                    entity.cached_bot_chest_bone_valid =
                        skeletonSlowCache.cachedBotChestBoneValid;
                    if (entity.pos != OW::Vector3(0, 0, 0) &&
                        skeletonSlowCache.cachedSkeletonPos != OW::Vector3(0, 0, 0)) {
                        const OW::Vector3 skeletonDelta =
                            entity.pos - skeletonSlowCache.cachedSkeletonPos;
                        for (size_t boneIndex = 0;
                             boneIndex < entity.skeleton_bones.size();
                             ++boneIndex) {
                            if (entity.skeleton_bone_valid[boneIndex])
                                entity.skeleton_bones[boneIndex] += skeletonDelta;
                        }
                        if (entity.cached_bot_chest_bone_valid)
                            entity.cached_bot_chest_bone += skeletonDelta;
                    }
                    } else {
                        entity.skeleton_bone_valid.fill(false);
                        entity.cached_bot_chest_bone_valid=false;
                    }
                } else {
                    const bool skeletonCacheHit =
                        skeletonSlowCache.skeletonCache.valid &&
                        skeletonSlowCache.skeletonCache.heroId == entity.HeroID &&
                        (!velocityBoneData ||
                         skeletonSlowCache.skeletonCache.velocityBoneData == velocityBoneData);
                    if (skeletonCacheHit)
                        pipelineStats.skeletonCacheHitCount++;
                    else
                        pipelineStats.skeletonCacheMissCount++;
                    const DWORD skeletonReadStartedTick=GetTickCount();
                    const auto cacheSkeletonStartedAt = std::chrono::steady_clock::now();
                    entity.CacheSkeletonBones(
                        skeletonSlowCache.skeletonCache,
                        velocityBoneData,
                        !OW::offset::UsesProtectedEntityLayout());
                    const double cacheSkeletonMs = static_cast<double>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - cacheSkeletonStartedAt).count()) /
                        1000.0;
                    pipelineStats.phase.skeletonCacheCallMs += cacheSkeletonMs;
                    const bool refreshedAnyBoneValid = std::any_of(
                        entity.skeleton_bone_valid.begin(),
                        entity.skeleton_bone_valid.end(),
                        [](bool valid) { return valid; });
                    recordSkeletonCall(
                        cacheSkeletonMs,
                        2,
                        velocityBoneData,
                        skeletonCacheHit,
                        refreshedAnyBoneValid);
                    if (skeletonSlowCache.skeletonCache.valid &&
                        skeletonSlowCache.skeletonCache.maxMappedIndex >= 0) {
                        constexpr size_t kObservedBoneStride = 0x30;
                        pipelineStats.skeletonBlockReadBytes +=
                            kObservedBoneStride *
                            static_cast<size_t>(
                                skeletonSlowCache.skeletonCache.maxMappedIndex) +
                            sizeof(XMFLOAT3);
                    } else if (!OW::offset::UsesProtectedEntityLayout()) {
                        pipelineStats.skeletonFallbackGetBonePosCount++;
                        pipelineStats.skeletonExactBoneReadCount += 3;
                    }

                    entity.model_shapes = skeletonSlowCache.cachedModelShapes;
                    if(entity.model_shapes.boneData!=velocityBoneData)entity.model_shapes={};
                    skeletonSlowCache.cachedSkeletonBones = entity.skeleton_bones;
                    skeletonSlowCache.cachedSkeletonBoneValid =
                        entity.skeleton_bone_valid;
                    skeletonSlowCache.cachedSkeletonPos = entity.pos;
                    skeletonSlowCache.cachedBotChestBone =
                        entity.cached_bot_chest_bone;
                    skeletonSlowCache.cachedBotChestBoneValid =
                        entity.cached_bot_chest_bone_valid;
                    skeletonSlowCache.cachedSkeletonValid = refreshedAnyBoneValid;
                    entity.skeleton_sample_tick_ms=skeletonReadStartedTick;
                    skeletonSlowCache.skeletonUpdateTick = entity.skeleton_sample_tick_ms;
                }

                if (entity.debugHeadBoneData)
                    processStats.velocityBoneDataNonZero++;
                if (entity.debugHeadBonePtr)
                    processStats.boneDataPtrNonZero++;
                if (entity.debugHeadBonesBase)
                    processStats.bonesBaseNonZero++;
                if (entity.debugHeadBoneIdTable)
                    processStats.velocityBoneIdTableNonZero++;
                const bool boneCountValid =
                    entity.debugHeadBoneCount > 0 &&
                    entity.debugHeadBoneCount <= OW::c_entity::kMaxBoneIdCount;
                if (boneCountValid)
                    processStats.velocityBoneCountValid++;
                if (entity.debugHeadLookupResolved)
                    processStats.velocityBoneIdTableReadable++;
                if (entity.debugHeadIdFound)
                    processStats.velocityBoneHeadIdFound++;

                if (processStats.sampleBoneAddress == 0 ||
                    (entity.AngleBase && !sampledBoneCandidateHasAngle)) {
                    processStats.sampleBoneAddress = entity.address;
                    processStats.sampleVelocityBase = entity.VelocityBase;
                    processStats.sampleBoneBase = entity.BoneBase;
                    processStats.sampleVelocityBoneData = entity.debugHeadBoneData;
                    processStats.sampleBoneDataPtr = entity.debugHeadBonePtr;
                    processStats.sampleBonesBase = entity.debugHeadBonesBase;
                    processStats.sampleBoneIdTable = entity.debugHeadBoneIdTable;
                    processStats.sampleBoneCount = static_cast<int>(entity.debugHeadBoneCount);
                    processStats.sampleBoneIdTableReadable = entity.debugHeadLookupResolved ? 1 : 0;
                    processStats.sampleBoneHeadIndex = entity.debugHeadMappedIndex;
                    sampledBoneCandidateHasAngle = entity.AngleBase != 0;
                }
                if (detailedProcessLog) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu bone_done head_res=%d id=%d local=%d world=%d idx=%d head=(%.2f,%.2f,%.2f).",
                        i,
                        entity.debugHeadLookupResolved ? 1 : 0,
                        entity.debugHeadIdFound ? 1 : 0,
                        entity.debugHeadLocalNonZero ? 1 : 0,
                        entity.debugHeadWorldNonZero ? 1 : 0,
                        entity.debugHeadMappedIndex,
                        entity.debugHeadWorld.X,
                        entity.debugHeadWorld.Y,
                        entity.debugHeadWorld.Z);
                }
                if (detailedProcessLog) {
                    processStats.headProbeCandidates++;
                    if (entity.debugHeadLookupResolved)
                        processStats.headProbeResolved++;
                    if (entity.debugHeadIdFound)
                        processStats.headProbeIdFound++;
                    if (entity.debugHeadLocalFinite)
                        processStats.headProbeLocalFinite++;
                    if (entity.debugHeadLocalNonZero)
                        processStats.headProbeLocalNonZero++;
                    if (entity.debugHeadWorldNonZero)
                        processStats.headProbeWorldNonZero++;
                    if (entity.debugHeadLookupException)
                        processStats.headProbeExceptions++;

                    const float posDist = cameraLocation.DistTo(entity.pos);
                    const bool nearCameraByPosition = std::isfinite(posDist) && posDist <= 3.0f;
                    if (nearCameraByPosition) {
                        processStats.headProbeNearCandidates++;
                        if (entity.debugHeadWorldNonZero)
                            processStats.headProbeNearWorldNonZero++;
                    } else {
                        processStats.headProbeFarCandidates++;
                        if (entity.debugHeadWorldNonZero)
                            processStats.headProbeFarWorldNonZero++;
                    }

                    if (entity.debugHeadWorldNonZero && processStats.sampleHeadGoodAddress == 0) {
                        const float headDist = cameraLocation.DistTo(entity.debugHeadWorld);
                        processStats.sampleHeadGoodAddress = entity.address;
                        processStats.sampleHeadGoodHeroId = entity.HeroID;
                        processStats.sampleHeadGoodMappedIndex = entity.debugHeadMappedIndex;
                        processStats.sampleHeadGoodLocalXCm = toCentimeters(entity.debugHeadLocal.x);
                        processStats.sampleHeadGoodLocalYCm = toCentimeters(entity.debugHeadLocal.y);
                        processStats.sampleHeadGoodLocalZCm = toCentimeters(entity.debugHeadLocal.z);
                        processStats.sampleHeadGoodWorldXCm = toCentimeters(entity.debugHeadWorld.X);
                        processStats.sampleHeadGoodWorldYCm = toCentimeters(entity.debugHeadWorld.Y);
                        processStats.sampleHeadGoodWorldZCm = toCentimeters(entity.debugHeadWorld.Z);
                        processStats.sampleHeadGoodDistanceCm = std::isfinite(headDist)
                            ? static_cast<int>(headDist * 100.0f)
                            : -1;
                    }

                    if ((!entity.debugHeadWorldNonZero || !entity.debugHeadIdFound ||
                         !entity.debugHeadLocalFinite || !entity.debugHeadLocalNonZero) &&
                        processStats.sampleHeadBadAddress == 0) {
                        processStats.sampleHeadBadAddress = entity.address;
                        processStats.sampleHeadBadHeroId = entity.HeroID;
                        processStats.sampleHeadBadBoneData = entity.debugHeadBoneData;
                        processStats.sampleHeadBadBonesBase = entity.debugHeadBonesBase;
                        processStats.sampleHeadBadBonePtr = entity.debugHeadBonePtr;
                        processStats.sampleHeadBadBoneIdTable = entity.debugHeadBoneIdTable;
                        processStats.sampleHeadBadMappedIndex = entity.debugHeadMappedIndex;
                        processStats.sampleHeadBadBoneCount = static_cast<int>(entity.debugHeadBoneCount);
                        processStats.sampleHeadBadLocalXCm = toCentimeters(entity.debugHeadLocal.x);
                        processStats.sampleHeadBadLocalYCm = toCentimeters(entity.debugHeadLocal.y);
                        processStats.sampleHeadBadLocalZCm = toCentimeters(entity.debugHeadLocal.z);
                    }
                }
                const bool anyBoneValid = std::any_of(
                    entity.skeleton_bone_valid.begin(),
                    entity.skeleton_bone_valid.end(),
                    [](bool valid) { return valid; });
                if (anyBoneValid)
                    processStats.skeletonAnyValid++;
                if (entity.skeleton_bone_valid[0]) entity.head_pos = entity.skeleton_bones[0];
                if (entity.skeleton_bone_valid[1]) entity.neck_pos = entity.skeleton_bones[1];
                if (entity.skeleton_bone_valid[2]) entity.chest_pos = entity.skeleton_bones[2];
                if (entity.skeleton_bone_valid[0])
                    processStats.skeletonHeadValid++;
            }
            // Failed detailed bones keep only accepted root-relative core
            // anchors for the same actor; never revive expired joint samples.
            if(const auto previous=previousEntityByAddress.find(entity.address);previous!=previousEntityByAddress.end() &&
                OW::PresentationTiming::SameLifetime(previous->second->PresentationIdentity(),entity.PresentationIdentity())) {
                const auto& old=*previous->second;
                const auto delta=entity.pos-old.pos;
                if(!entity.skeleton_bone_valid[0] && isUsableVector(old.head_pos) && old.head_pos.DistTo(old.pos)<4.f)entity.head_pos=old.head_pos+delta;
                if(!entity.skeleton_bone_valid[1] && isUsableVector(old.neck_pos) && old.neck_pos.DistTo(old.pos)<4.f)entity.neck_pos=old.neck_pos+delta;
                if(!entity.skeleton_bone_valid[2] && isUsableVector(old.chest_pos) && old.chest_pos.DistTo(old.pos)<4.f)entity.chest_pos=old.chest_pos+delta;
            }
            if (entity.FillMissingCoreAnchorsFromPosition())
                dmaFieldWindow.coreAnchorFallback++;

            if (entity.HeroID == OW::eHero::HERO_WRECKINGBALL) {
                entity.head_pos.Y += 0.02f;
            }
            }

            RosterEntry& slowCache = *cacheIt->second;
            const auto teamNamePhaseStartedAt = std::chrono::steady_clock::now();
            constexpr double kTeamNameSlowCallThresholdMs = 50.0;
            auto recordTeamNameCall = [&](double elapsedMs, uint32_t op, bool success) {
                if (elapsedMs >= kTeamNameSlowCallThresholdMs)
                    ++pipelineStats.teamNameSlowCallCount;
                if (elapsedMs > pipelineStats.teamNameMaxCallMs) {
                    pipelineStats.teamNameMaxCallMs = elapsedMs;
                    pipelineStats.teamNameMaxCallOp = op;
                    pipelineStats.teamNameMaxCallHeroId = entity.HeroID;
                    pipelineStats.teamNameMaxCallLinkBase = entity.LinkBase;
                    pipelineStats.teamNameMaxCallTeamBase = entity.TeamBase;
                    pipelineStats.teamNameMaxCallSuccess = success;
                }
            };
            const DWORD slowNow = GetTickCount();
            bool refreshNameTeam =
                !slowCache.nameTeamValid ||
                slowNow - slowCache.nameTeamUpdateTick >= OW::kEntityTeamNameIntervalMs;
            if (refreshNameTeam && !consumeCycleBudget(teamNameRefreshBudget))
                refreshNameTeam = false;
            const DWORD skillInterval =
                V2::Alerts::ObserveSkillHero(entity.HeroID) || V2::Alerts::ObserveUltimateHero(entity.HeroID)
                    ? 50u : OW::Config::ult ? 100u : OW::kEntitySkillStatusIntervalMs;
            size_t skillField = slowCache.skillRefreshCursor % 3;
            bool refreshSkillStatus = false;
            for (size_t offset = 0; offset < 3; ++offset) {
                const size_t candidate = (slowCache.skillRefreshCursor + offset) % 3;
                if (slowCache.skillDeadlines[candidate].Due(slowNow, skillInterval)) {
                    skillField = candidate;
                    refreshSkillStatus = true;
                    break;
                }
            }
            if (refreshSkillStatus && !consumeCycleBudget(skillRefreshBudget))
                refreshSkillStatus = false;
            const bool refreshLocalSkillStatus =
                !slowCache.localSkillValid ||
                slowNow - slowCache.localSkillUpdateTick >= OW::kEntityLocalSkillIntervalMs;
            std::string name;
            if (refreshNameTeam) {
                const auto heroLookupStartedAt = std::chrono::steady_clock::now();
                name = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
                const double heroLookupMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - heroLookupStartedAt).count()) / 1000.0;
                pipelineStats.phase.teamNameHeroLookupMs += heroLookupMs;
                recordTeamNameCall(heroLookupMs, 1, !name.empty());
            } else {
                name = slowCache.heroName;
            }
            if (name.empty())
                name = "Unknown";

            if (entity.HeroID == OW::eHero::HERO_DVA && name != "Hana") {
                entity.imort = false;
                entity.head_pos.Y -= 0.1f;
                entity.chest_pos = entity.neck_pos;
                entity.chest_pos.Y -= 0.3f;
            }

            bool isStandardBot = OW::GameData::IsTrainingBotHeroId(entity.HeroID);
            if (isStandardBot) {
                const auto botAdjustStartedAt = std::chrono::steady_clock::now();
                if (entity.cached_bot_chest_bone_valid)
                    entity.chest_pos = entity.cached_bot_chest_bone;
                else
                    entity.chest_pos = entity.GetBonePos(83);
                const double botAdjustMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - botAdjustStartedAt).count()) / 1000.0;
                pipelineStats.phase.teamNameBotAdjustMs += botAdjustMs;
                recordTeamNameCall(botAdjustMs, 2, true);
            }
            if (detailedProcessLog && processStats.boneCandidates > 0) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu post_bone_adjust.", i);
            }

            // ---- BattleTag (optional) ----
            if (OW::Config::drawbattletag) {
                const auto battleTagStartedAt = std::chrono::steady_clock::now();
                if (slowCache.participantIdentityValid && entity != localCycleSnapshot)
                    entity.battletag = slowCache.participantLabel;
                const double battleTagMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - battleTagStartedAt).count()) / 1000.0;
                pipelineStats.phase.teamNameBattleTagMs += battleTagMs;
                recordTeamNameCall(battleTagMs, 3, entity.statcombase != 0);
            }
            if (detailedProcessLog && processStats.boneCandidates > 0) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu post_battletag.", i);
            }

            // ---- Team ----
            if (!componentCache.teamUpdateTick ||
                slowNow-componentCache.teamUpdateTick>=OW::kEntityDeadComponentRefreshMs) {
                dmaFieldWindow.teamRefresh++;
                const auto teamReadStartedAt = std::chrono::steady_clock::now();
                const auto key=entity.GetTeamComparisonKey();
                const auto* teamIdentity=cycleDiscoveries.Find(entity.address,entity.LinkParent,slowNow);
                const bool paired=teamIdentity && teamIdentity->match==entity.match_id && teamIdentity->hero==entity.HeroID;
                const bool confirmed=componentCache.confirmedTeam.Observe(key,
                    paired?entity.LinkParent:0,paired?entity.match_id:0);
                componentCache.teamUpdateTick=slowNow;
                if(!confirmed)requestTopologyRescan(slowNow);
                const double teamReadMs = static_cast<double>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - teamReadStartedAt).count()) / 1000.0;
                pipelineStats.phase.teamNameTeamReadMs += teamReadMs;
                recordTeamNameCall(teamReadMs, 4, confirmed);
            }
            entity.team_known=componentCache.confirmedTeam.known;
            entity.team_key=entity.team_known?componentCache.confirmedTeam.key:0;
            entity.Team=entity.team_known && localCycleSnapshot.team_known &&
                (entity.team_key==(1u<<0x1B) || entity.team_key!=localCycleSnapshot.team_key);
            if(OW::GameData::IsFriendlyTrainingBotHeroId(entity.HeroID))entity.Team=false;
            slowCache.isEnemy=entity.Team;
            pipelineStats.phase.teamNameMs += static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - teamNamePhaseStartedAt).count()) / 1000.0;

            // ---- Visibility: independent deadline, exact reads, bounded sample age ----
            // Both read paths use the active profile's visibility decoder.
            if(!hotAlreadyApplied) {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.visibilityMs);
            if (entity.VisBase && componentCache.visibilitySample.Due(processLoopTick)) {
                DWORD visibilityStarted = scatterVisibility && hotReads.visibilityRequested
                    ? entity.read_started_tick_ms : GetTickCount();
                uint64_t rawVis = 0;
                bool rawVisRead = false;
                if (scatterVisibility) {
                    dmaFieldWindow.visibilityRequested++;
                    rawVis = hotReads.visibilityRaw;
                    rawVisRead = hotReads.VisibilityRead();
                    if (rawVisRead) {
                        dmaFieldWindow.visibilityScatterHit++;
                        pipelineStats.hotScatterSuccessCount++;
                        pipelineStats.visibilityScatterHitCount++;
                    } else {
                        if (hotReads.visibilityRequested && hotReads.scatterOk) {
                            dmaFieldWindow.visibilityScatterPartial++;
                            pipelineStats.hotScatterPartialCount++;
                        }
                        dmaFieldWindow.visibilityFallback++;
                        pipelineStats.hotScatterFallbackReadCount++;
                        pipelineStats.visibilityFallbackCount++;
                        visibilityStarted = GetTickCount();
                        rawVisRead = SDK->read_range(
                            visibilityValueAddress,
                            &rawVis,
                            sizeof(rawVis));
                        if (!rawVisRead)
                            dmaFieldWindow.visibilityReadFail++;
                    }
                } else {
                    rawVis = OW::DecryptVis(entity.VisBase, &rawVisRead);
                }

                if (!rawVisRead)
                    rawVis = 0;
                else if (scatterVisibility)
                    rawVis = OW::DecodeVisibilityRaw(rawVis);
                componentCache.visibilitySample.Commit(rawVis, rawVisRead, visibilityStarted, GetTickCount());
                entity.Vis = componentCache.visibilitySample.Visible(GetTickCount());
                slowCache.vis = entity.Vis;
                if (rawVis != 0 && rawVis != 1) {
                    dmaFieldWindow.visibilityAnomaly++;
                    Diagnostics::Aim("visibility.anomaly raw=%llu addr=0x%llX hero=0x%llX visbase=0x%llX",
                        static_cast<unsigned long long>(rawVis),
                        static_cast<unsigned long long>(entity.address),
                        static_cast<unsigned long long>(entity.HeroID),
                        static_cast<unsigned long long>(entity.VisBase));
                }
            } else if (syntheticCnTrainingBot) {
                entity.Vis = true;
                slowCache.vis = true;
            } else if (entity.VisBase) {
                entity.Vis = componentCache.visibilitySample.Visible(GetTickCount());
                slowCache.vis = entity.Vis;
            } else {
                entity.Vis = slowCache.vis = false;
                componentCache.visibilitySample = {};
                dmaFieldWindow.visibilityBaseMissing++;
            }
            }

            const auto cooldownRules = OW::CooldownTimers::ForHero(entity.HeroID);
            const std::array<uint64_t,3> ultimateIdentity{entity.HeroID,connectionEpoch,processSceneGeneration};
            if (slowCache.ultimateIdentity != ultimateIdentity) {
                slowCache.ultimateIdentity = ultimateIdentity;
                slowCache.ultimateValid = false;
                slowCache.displayUltimateTick = 0;
                slowCache.ultimate = 0;
            }
            // A rebuilt temporary entity has not yet inherited the admitted body generation.
            const std::array<uint64_t,4> cooldownIdentity{entity.HeroID, slowCache.entity.actor_generation,
                connectionEpoch, processSceneGeneration};
            if (slowCache.cooldownIdentity != cooldownIdentity ||
                entity.roster_state == OW::EntityRosterState::Dead) {
                slowCache.cooldownIdentity = cooldownIdentity;
                slowCache.cooldownTimers = {};
                slowCache.displayCooldowns = {};
                slowCache.skillActiveTick = {};
            }
            auto observeSkill = [&](size_t activeSlot) {
                bool readable = false;
                const bool active = OW::IsSkillActive(entity.SkillBase + 0x40, 0,
                    activeSlot == 0 ? 0x28E3 : 0x28E9, &readable);
                if (readable) (activeSlot == 0 ? slowCache.skill1act : slowCache.skill2act) = active;
                slowCache.skillActiveTick[activeSlot] = readable ? slowNow : 0;
                const double now = OW::OverlayStatus::MonotonicSeconds();
                for (size_t slot = 0; slot < cooldownRules.size(); ++slot)
                    if (cooldownRules[slot].activeSlot == activeSlot)
                        slowCache.cooldownTimers[slot].Observe(active, readable, cooldownRules[slot], now, slowNow);
            };
            auto publishCooldowns = [&] {
                const double now = OW::OverlayStatus::MonotonicSeconds();
                for (size_t slot = 0; slot < cooldownRules.size(); ++slot)
                    slowCache.displayCooldowns[slot] = entity.SkillBase
                        ? slowCache.cooldownTimers[slot].Sample(now, slowNow, cooldownRules[slot])
                        : OW::OverlayStatus::CooldownSample{};
                entity.display_cooldowns = slowCache.displayCooldowns;
                slowCache.skillcd1 = slowCache.skillcd2 = 0;
                for (size_t slot = 0; slot < cooldownRules.size(); ++slot)
                    if (entity.display_cooldowns[slot].valid)
                        (cooldownRules[slot].activeSlot == 0 ? slowCache.skillcd1 : slowCache.skillcd2) =
                            entity.display_cooldowns[slot].Remaining(now);
            };
            // ---- Skills ----
            {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.skillMs);
            if (!refreshSkillStatus) {
                pipelineStats.skillSkippedNotDueCount++;
            } else if (entity.SkillBase) {
                pipelineStats.skillDueCount++;
                dmaFieldWindow.skillRefresh++;
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu skill_start skill_base=0x%llX.",
                        i,
                        static_cast<unsigned long long>(entity.SkillBase));
                }
                switch (skillField) {
                case 0:
                    observeSkill(0);
                    break;
                case 1:
                    observeSkill(1);
                    break;
                case 2:
                {
                    float ultimate = 0.0f;
                    if (entity.Alive && OW::TryReadUlt(entity.SkillBase + 0x40, 0, 0x1e32, ultimate) &&
                        std::isfinite(ultimate)) {
                        slowCache.ultimate = ultimate;
                        slowCache.ultimateValid = true;
                        slowCache.displayUltimateTick = slowNow;
                    } else {
                        // Keep the last successful sample within its freshness window.
                    }
                    break;
                }
                }
                slowCache.skillDeadlines[skillField].Schedule(GetTickCount(), skillInterval);
                slowCache.skillRefreshCursor = (skillField + 1) % 3;
                pipelineStats.skillReadCount++;
                if (slowCache.skillRefreshCursor == 0) {
                    slowCache.skillUpdateTick = slowNow;
                    slowCache.skillValid = true;
                }
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu skill_done.", i);
                }
            }
            entity.skill1act = slowCache.skill1act;
            entity.skill2act = slowCache.skill2act;
            entity.skill_active_tick_ms = entity.SkillBase ? slowCache.skillActiveTick : std::array<uint32_t, 2>{};
            applyCachedUltimateToEntity(entity, slowCache);
            publishCooldowns();
            entity.skillcd1 = slowCache.skillcd1;
            entity.skillcd2 = slowCache.skillcd2;
            entity.display_cooldowns = entity.SkillBase ? slowCache.displayCooldowns
                : std::array<OW::OverlayStatus::CooldownSample, 2>{};
            }

            // Sombra stealth affects the effective visibility state, so keep it
            // on the per-process path with DecryptVis instead of the slow cache.
            entity.sombraStealthKnown=false;entity.sombraStealth=false;
            if (entity.HeroID == OW::eHero::HERO_SOMBRA && entity.Team && entity.SkillBase) {
                __m128 query{};uint16_t ids[15]={0,0x7C5};query.m128_u64[1]=entity.SkillBase+0x40+0xD0;
                const auto skill=OW::FnSkillStruct(&query,ids);uint8_t active=0;
                entity.sombraStealthKnown=skill&&SDK->read_range(skill+0x48,&active,sizeof(active))&&active<=1;
                entity.sombraStealth=entity.sombraStealthKnown&&active==1;
                entity.Vis = (entity.Vis && !entity.sombraStealth);
                slowCache.vis = entity.Vis;
            }

            // ---- Player controller / local entity detection ----
            // AngleBase is produced from the PlayerController component and is
            // the primary local-player anchor. Camera proximity remains a
            // secondary diagnostic fallback.
            {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.localSelectMs);
            if (entity.AngleBase && hasRenderableAnchor(entity)) {
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_start angle=0x%llX.",
                        i,
                        static_cast<unsigned long long>(entity.AngleBase));
                }
                localStats.angleCandidates++;
                const bool headIsZero = entity.head_pos == Vector3(0, 0, 0);
                const bool positionIsNonZero = entity.pos != Vector3(0, 0, 0);
                if (headIsZero)
                    localStats.zeroHeadCandidates++;
                if (positionIsNonZero)
                    localStats.nonZeroPositionCandidates++;

                float dist = cameraLocation.DistTo(entity.head_pos);
                const int distanceCm = std::isfinite(dist)
                    ? static_cast<int>(dist * 100.0f + 0.5f)
                    : -1;

                const std::string& localHeroName = name;
                if (localHeroName != "Unknown")
                    localStats.namedCandidates++;

                // Log MatchId cross-reference on first cycle (diagnostic only)
                if (matchIdLogEnabled) {
                    const uint32_t eMatchId = entity.match_id;
                    if (eMatchId != 0) {
                        Diagnostics::Info("[MATCHID] entity=0x%llX MatchId=0x%08X(%u) hero=%s angle=0x%llX dist=%.1fm.",
                            static_cast<unsigned long long>(entity.address),
                            static_cast<unsigned int>(eMatchId),
                            static_cast<unsigned int>(eMatchId),
                            localHeroName.c_str(),
                            static_cast<unsigned long long>(entity.AngleBase),
                            static_cast<double>(dist));
                    }
                }
                // Track proximity stats
                if (dist <= 1.f)
                    localStats.nearCameraCandidates++;
                if (distanceCm >= 0 &&
                    (localStats.bestDistanceCm < 0 || distanceCm < localStats.bestDistanceCm)) {
                    localStats.bestDistanceCm = distanceCm;
                    localStats.bestAddress = entity.address;
                    localStats.bestHeroId = entity.HeroID;
                    localStats.bestAngleBase = entity.AngleBase;
                    localStats.bestHealth = static_cast<int>(entity.PlayerHealth + 0.5f);
                    localStats.bestHeadXCm = std::isfinite(entity.head_pos.X) ? static_cast<int>(entity.head_pos.X * 100.0f) : 0;
                    localStats.bestHeadYCm = std::isfinite(entity.head_pos.Y) ? static_cast<int>(entity.head_pos.Y * 100.0f) : 0;
                    localStats.bestHeadZCm = std::isfinite(entity.head_pos.Z) ? static_cast<int>(entity.head_pos.Z * 100.0f) : 0;
                    localStats.bestPosXCm = std::isfinite(entity.pos.X) ? static_cast<int>(entity.pos.X * 100.0f) : 0;
                    localStats.bestPosYCm = std::isfinite(entity.pos.Y) ? static_cast<int>(entity.pos.Y * 100.0f) : 0;
                    localStats.bestPosZCm = std::isfinite(entity.pos.Z) ? static_cast<int>(entity.pos.Z * 100.0f) : 0;
                }

                // Selection priority:
                // 1. First AngleBase candidate (only local has PlayerController)
                // 2. Proximity fallback (within 1m + known name)
                const bool isNearCamera = dist <= 1.f;
                const bool hasKnownName = localHeroName != "Unknown";
                const bool shouldSelectLocal =
                    localStats.selected == 0 ||
                    (isNearCamera && hasKnownName);

                if (shouldSelectLocal) {
                    if (refreshLocalSkillStatus && entity.SkillBase) {
                        pipelineStats.skillDueCount++;
                        dmaFieldWindow.skillRefresh++;
                        switch (slowCache.localSkillRefreshCursor % 4) {
                        case 0:
                            slowCache.reloading = OW::IsSkillActivate1(entity.SkillBase + 0x40, 0, 0x4BF);
                            break;
                        case 1: observeSkill(0); break;
                        case 2: observeSkill(1); break;
                        default:
                        {
                            float ultimate = 0.0f;
                            if (entity.Alive && OW::TryReadUlt(entity.SkillBase + 0x40, 0, 0x1e32, ultimate) &&
                                std::isfinite(ultimate)) {
                                slowCache.ultimate = ultimate;
                                slowCache.ultimateValid = true;
                                slowCache.displayUltimateTick = slowNow;
                            } else {
                                // Keep the last successful sample within its freshness window.
                            }
                            slowCache.skillValid = true;
                            slowCache.skillUpdateTick = slowNow;
                            break;
                        }
                        }
                        ++slowCache.localSkillRefreshCursor;
                        pipelineStats.skillReadCount++;
                        slowCache.localSkillUpdateTick = slowNow;
                        slowCache.localSkillValid = true;
                    }
                    entity.skill1act = slowCache.skill1act;
                    entity.skill2act = slowCache.skill2act;
                    publishCooldowns();
                    applyCachedUltimateToEntity(entity, slowCache);
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select skillcd1_start skill_base=0x%llX hero=%s.",
                            i,
                            static_cast<unsigned long long>(entity.SkillBase),
                            localHeroName.c_str());
                    }
                    entity.skillcd1 = slowCache.skillcd1;
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select skillcd1_done value=%.3f.",
                            i,
                            entity.skillcd1);
                    }
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select skillcd2_start.", i);
                    }
                    entity.skillcd2 = slowCache.skillcd2;
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select skillcd2_done value=%.3f.",
                            i,
                            entity.skillcd2);
                    }
                    localStats.selected++;
                    localStats.selectedAddress = entity.address;
                    localStats.selectedHeroId = entity.HeroID;
                    localStats.selectedAngleBase = entity.AngleBase;
                    localStats.selectedHealth = static_cast<int>(entity.PlayerHealth + 0.5f);
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select reload_start.", i);
                    }
                    OW::Config::reloading = slowCache.reloading;
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select reload_done.", i);
                    }
                    entity.Team = false;
                    slowCache.isEnemy = false;
                }
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_done selected=%zu.",
                        i, localStats.selected);
                }
            }
            }

            // Add to list if valid
            if (refreshNameTeam) {
                slowCache.heroName = name;
                slowCache.nameTeamUpdateTick = slowNow;
                slowCache.nameTeamValid = true;
            }
            if (detailedProcessLog && processStats.boneCandidates > 0) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu name_start.", i);
            }
            if (detailedProcessLog && processStats.boneCandidates > 0) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu name_done name=%s.", i, name.c_str());
            }
            if (name.rfind("Hero_", 0) == 0) {
                processStats.nameUnknown++;
                recordNameUnknownSample(entity, ComponentParent, LinkParent, name);
            }
            const bool hasRenderablePosition =
                !OW::offset::UsesProtectedEntityLayout() || hasRenderableAnchor(entity);
            const bool scanSeenThisCycle =
                rawScanSeenComponents.find(ComponentParent) != rawScanSeenComponents.end();
            const bool scanMissHotRead =
                scanMissHotReadComponents.find(ComponentParent) != scanMissHotReadComponents.end();
#include "Game/EntityMaintenance/ObservationCommit.inl"

        }

        }

        for (const uint64_t componentParent : scanMissHotReadComponents) {
            if (scanMissHotReadSucceededComponents.find(componentParent) ==
                scanMissHotReadSucceededComponents.end()) {
                ++lifecycleStats.entityRecordScanMissHotReadFailCount;
            }
        }

        bool firstRespawnScanDue = false;
        bool retryRespawnScanDue = false;
        for (const auto& rosterPair : entityRoster) {
            const RosterEntry& entry = *rosterPair.second;
            const auto due = OW::EntityRosterPolicy::ResolveRespawnRescanDue(
                entry.entity.roster_state != OW::EntityRosterState::Fresh,
                entry.respawnWatchStartedTick,
                entry.respawnFirstScanRequested,
                entry.respawnLastScanRequestTick,
                processLoopTick,
                OW::kEntityRespawnRescanDelayMs,
                OW::kEntityRespawnRescanRetryMs,
                OW::kEntityRespawnWatchMs);
            if (due == OW::EntityRosterPolicy::RespawnRescanDue::First) {
                firstRespawnScanDue = true;
            } else if (due == OW::EntityRosterPolicy::RespawnRescanDue::Retry) {
                retryRespawnScanDue = true;
            }
        }
        const bool respawnRequestIntervalReady =
            lastRespawnTopologyRescanRequestTick == 0 ||
            processLoopTick - lastRespawnTopologyRescanRequestTick >=
                OW::kEntityRespawnRescanRetryMs;
        if ((firstRespawnScanDue || retryRespawnScanDue) &&
            respawnRequestIntervalReady) {
            const bool requested = requestTopologyRescan(
                processLoopTick,
                true);
            if (requested) {
                lastRespawnTopologyRescanRequestTick = processLoopTick;
                for (auto& rosterPair : entityRoster) {
                    RosterEntry& entry = *rosterPair.second;
                    if (entry.entity.roster_state == OW::EntityRosterState::Fresh ||
                        entry.respawnWatchStartedTick == 0) {
                        continue;
                    }
                    const DWORD deadAge =
                        processLoopTick - entry.respawnWatchStartedTick;
                    if (deadAge < OW::kEntityRespawnRescanDelayMs) {
                        continue;
                    }
                    entry.respawnFirstScanRequested = true;
                    entry.respawnLastScanRequestTick = processLoopTick;
                }
            }
        }


        if (!OW::RuntimeWorkersRunning() || !OW::ProcessConnection::IsConnected() ||
            OW::ProcessConnection::ConnectionEpoch() != connectionEpoch ||
            OW::offset::ActiveProfile() != runtimeProfile) {
            continue;
        }

#include "Game/EntityMaintenance/Outlines.inl"

        // Swap processed entities
        maintenanceCursor=OW::PresentationTiming::AdvanceMaintenanceCursor(entityLoopStart,raw_entities.size(),maintenanceVisited,skeletonBudgetForCycle);
        RuntimeTrace::Emit(RuntimeTrace::SlowWork,RuntimeTrace::NowUs()-maintenanceStartedUs,tmp_entities.size());
        if(missingTransitionNeedsScan) {
            requestTopologyRescan(GetTickCount());missingTransitionNeedsScan=false;
        }
        const size_t valid_count = std::count_if(entityRoster.begin(),entityRoster.end(),[&](const auto& item) {
            return item.second->seenThisCycle && item.second->entity.roster_state==OW::EntityRosterState::Fresh;
        });
        hpdy_entities.clear();
        for(const auto& [address,dynamic]:dynamicEntityCache) {
            if(!dynamic.valid || GetTickCount()-dynamic.updateTick>1000 ||
                !scheduledRawComponents.contains(address))continue;
            OW::hpanddy value{};value.entityid=dynamic.entityId;value.MeshBase=dynamic.meshBase;value.POS=dynamic.pos;
            value.scene_generation=processSceneGeneration;
            hpdy_entities.push_back(value);
        }
        const size_t dynamic_count = hpdy_entities.size();
        processStats.validated = valid_count;
        processStats.dynamic = dynamic_count;
        std::vector<OW::c_entity> published_entities{};
        Diagnostics::RosterStats rosterStats{};
        size_t published_count = 0;
        {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.publishMs);
            ++entityPublishSequence;
            rosterStats =
                publishRosterSnapshot(
                    GetTickCount(),
                    heroChangedThisCycle,
                    published_entities,
                    &lifecycleStats);
            published_count = published_entities.size();
            if (!hotFramePublished && !OW::TargetingDetail::TryPublishEntitySnapshots(
                    std::move(published_entities),
                    std::move(hpdy_entities),
                    connectionEpoch,
                    published_count,&processRegistry,processSceneGeneration)) {
                continue;
            }
        }

        pipelineStats.rawCount = raw_entities.size();
        pipelineStats.validatedCount = valid_count;
        pipelineStats.publishedCount = published_count;
        dmaFieldWindow.cycles++;
        dmaFieldWindow.raw += raw_entities.size();
        dmaFieldWindow.validated += valid_count;
        dmaFieldWindow.published += published_count;
        dmaFieldWindow.linkBaseFail += processStats.linkBaseFail;
        dmaFieldWindow.nameUnknown += processStats.nameUnknown;
        dmaFieldWindow.boneCandidates += processStats.boneCandidates;
        dmaFieldWindow.skeletonAnyValid += processStats.skeletonAnyValid;
        dmaFieldWindow.skeletonHeadValid += processStats.skeletonHeadValid;
        const bool suspectStaleScan =
            (valid_count == 0 && !raw_entities.empty()) ||
            (previousProcessedValidCount > 0 && valid_count < previousProcessedValidCount);
        if (suspectStaleScan) {
            if (coldTopologyScanEnabled) {
                requestTopologyRescan(processLoopTick);
            } else {
                std::lock_guard<std::mutex> lock(g_mutex);
                OW::entity_fast_scan_until_tick = GetTickCount() + OW::kEntityFastRescanWindowMs;
            }
        }
        previousProcessedValidCount = valid_count;
        Diagnostics::SetEntityProcessStats(processStats);
        if(localStats.selected==0 && localCycleSnapshot.address && localCycleSnapshot.AngleBase &&
            OW::PresentationTiming::Fresh(localCycleSnapshot.anchor_sample_tick_ms,GetTickCount())) {
            localStats.selected=1;localStats.selectedAddress=localCycleSnapshot.address;
            localStats.selectedHeroId=localCycleSnapshot.HeroID;
            localStats.selectedAngleBase=localCycleSnapshot.AngleBase;
            localStats.selectedHealth=static_cast<int>(localCycleSnapshot.PlayerHealth+0.5f);
        }
        Diagnostics::SetLocalEntityStats(localStats);
        Diagnostics::SetRosterStats(rosterStats);
        maybeLogDmaFieldWindow(processLoopTick);
        if (matchIdLogEnabled)
            matchIdLogEnabled = false;
        finalizePipelineProcessStats(pipelineStats, pipelineDmaStart, pipelineCycleStartedAt);
        Diagnostics::Trace("Entity process cycle: valid=%zu hp_dynamic=%zu raw=%zu.",
            valid_count, dynamic_count, raw_entities.size());
        if (OW::PipelineDebugEnabled()) {
            const DWORD now = GetTickCount();
            const bool changed =
                lastLoggedRawCount != raw_entities.size() ||
                lastLoggedValidatedCount != valid_count;
            if (changed || now - lastProcessLogTick >= 1000) {
                Diagnostics::Info("[PIPELINE] Stage 4 entity processing raw=%zu validated=%zu hp_dynamic=%zu.",
                    raw_entities.size(), valid_count, dynamic_count);
                Diagnostics::Info("[PIPELINE] Stage 4 timing scan_ms=%lu process_ms=%lu health_ms=%lu team_name_ms=%lu skill_status_ms=%lu local_skill_ms=%lu visibility=scatter_cn_ne_with_fallback.",
                    static_cast<unsigned long>(OW::kEntityScanIntervalMs),
                    static_cast<unsigned long>(1000.0 / V2::Acquisition::kTargetHz),
                    static_cast<unsigned long>(OW::kEntityHealthIntervalMs),
                    static_cast<unsigned long>(OW::kEntityTeamNameIntervalMs),
                    static_cast<unsigned long>(OW::kEntitySkillStatusIntervalMs),
                    static_cast<unsigned long>(OW::kEntityLocalSkillIntervalMs));
                Diagnostics::Info("[PIPELINE] Stage 4 roster fresh=%zu dead=%zu missing=%zu expired=%zu hero_change=%zu published=%zu.",
                    rosterStats.fresh,
                    rosterStats.dead,
                    rosterStats.missing,
                    rosterStats.expired,
                    rosterStats.heroChanged,
                    published_count);
                if (suspectStaleScan) {
                    Diagnostics::Info("[PIPELINE] Stage 4 suspected stale scan/cache; fast rescan for %lu ms.",
                        static_cast<unsigned long>(OW::kEntityFastRescanWindowMs));
                }
                Diagnostics::Info("[PIPELINE] Stage 4 detail null=%zu duplicate=%zu health_base_fail=%zu link_base_fail=%zu hero_missing=%zu hero_fallback_fail=%zu name_unknown=%zu.",
                    processStats.nullPair,
                    processStats.duplicate,
                    processStats.healthBaseFail,
                    processStats.linkBaseFail,
                    processStats.heroBaseMissing,
                    processStats.heroFallbackFail,
                    processStats.nameUnknown);
                Diagnostics::Info("[PIPELINE] Stage 4 bone chain candidates=%zu bone_base=%zu vel_bonedata=%zu bone_ptr=%zu bones_base=%zu id_table=%zu count_ok=%zu table_read=%zu head_id=%zu skel_any=%zu skel_head=%zu sample addr=0x%llX velocity=0x%llX bone=0x%llX vel_bonedata=0x%llX bone_ptr=0x%llX bones_base=0x%llX id_table=0x%llX count=%d read=%d head_idx=%d.",
                    processStats.boneCandidates,
                    processStats.boneBaseNonZero,
                    processStats.velocityBoneDataNonZero,
                    processStats.boneDataPtrNonZero,
                    processStats.bonesBaseNonZero,
                    processStats.velocityBoneIdTableNonZero,
                    processStats.velocityBoneCountValid,
                    processStats.velocityBoneIdTableReadable,
                    processStats.velocityBoneHeadIdFound,
                    processStats.skeletonAnyValid,
                    processStats.skeletonHeadValid,
                    static_cast<unsigned long long>(processStats.sampleBoneAddress),
                    static_cast<unsigned long long>(processStats.sampleVelocityBase),
                    static_cast<unsigned long long>(processStats.sampleBoneBase),
                    static_cast<unsigned long long>(processStats.sampleVelocityBoneData),
                    static_cast<unsigned long long>(processStats.sampleBoneDataPtr),
                    static_cast<unsigned long long>(processStats.sampleBonesBase),
                    static_cast<unsigned long long>(processStats.sampleBoneIdTable),
                    processStats.sampleBoneCount,
                    processStats.sampleBoneIdTableReadable,
                    processStats.sampleBoneHeadIndex);
                Diagnostics::Info("[PIPELINE] Stage 4 head probe candidates=%zu resolved=%zu id_found=%zu local_finite=%zu local_nz=%zu world_nz=%zu exceptions=%zu near_pos=%zu/%zu far_pos=%zu/%zu.",
                    processStats.headProbeCandidates,
                    processStats.headProbeResolved,
                    processStats.headProbeIdFound,
                    processStats.headProbeLocalFinite,
                    processStats.headProbeLocalNonZero,
                    processStats.headProbeWorldNonZero,
                    processStats.headProbeExceptions,
                    processStats.headProbeNearWorldNonZero,
                    processStats.headProbeNearCandidates,
                    processStats.headProbeFarWorldNonZero,
                    processStats.headProbeFarCandidates);
                Diagnostics::Info("[PIPELINE] Stage 4 head sample good addr=0x%llX hero=0x%llX idx=%d local_cm=(%d,%d,%d) world_cm=(%d,%d,%d) dist_cm=%d.",
                    static_cast<unsigned long long>(processStats.sampleHeadGoodAddress),
                    static_cast<unsigned long long>(processStats.sampleHeadGoodHeroId),
                    processStats.sampleHeadGoodMappedIndex,
                    processStats.sampleHeadGoodLocalXCm,
                    processStats.sampleHeadGoodLocalYCm,
                    processStats.sampleHeadGoodLocalZCm,
                    processStats.sampleHeadGoodWorldXCm,
                    processStats.sampleHeadGoodWorldYCm,
                    processStats.sampleHeadGoodWorldZCm,
                    processStats.sampleHeadGoodDistanceCm);
                Diagnostics::Info("[PIPELINE] Stage 4 head sample bad addr=0x%llX hero=0x%llX idx=%d count=%d bonedata=0x%llX bones_base=0x%llX bone_ptr=0x%llX id_table=0x%llX local_cm=(%d,%d,%d).",
                    static_cast<unsigned long long>(processStats.sampleHeadBadAddress),
                    static_cast<unsigned long long>(processStats.sampleHeadBadHeroId),
                    processStats.sampleHeadBadMappedIndex,
                    processStats.sampleHeadBadBoneCount,
                    static_cast<unsigned long long>(processStats.sampleHeadBadBoneData),
                    static_cast<unsigned long long>(processStats.sampleHeadBadBonesBase),
                    static_cast<unsigned long long>(processStats.sampleHeadBadBonePtr),
                    static_cast<unsigned long long>(processStats.sampleHeadBadBoneIdTable),
                    processStats.sampleHeadBadLocalXCm,
                    processStats.sampleHeadBadLocalYCm,
                    processStats.sampleHeadBadLocalZCm);
                Diagnostics::Info("[PIPELINE] Stage 4 local angle_candidates=%zu near_camera=%zu named=%zu selected=%zu best_dist_cm=%d health=%d hero=0x%llX angle=0x%llX.",
                    localStats.angleCandidates,
                    localStats.nearCameraCandidates,
                    localStats.namedCandidates,
                    localStats.selected,
                    localStats.bestDistanceCm,
                    localStats.selectedHealth,
                    static_cast<unsigned long long>(localStats.selectedHeroId),
                    static_cast<unsigned long long>(localStats.selectedAngleBase));
                Diagnostics::Info("[PIPELINE] Stage 4 local coords zero_head=%zu nonzero_pos=%zu best addr=0x%llX hero=0x%llX angle=0x%llX health=%d head_cm=(%d,%d,%d) pos_cm=(%d,%d,%d) camera_cm=(%d,%d,%d).",
                    localStats.zeroHeadCandidates,
                    localStats.nonZeroPositionCandidates,
                    static_cast<unsigned long long>(localStats.bestAddress),
                    static_cast<unsigned long long>(localStats.bestHeroId),
                    static_cast<unsigned long long>(localStats.bestAngleBase),
                    localStats.bestHealth,
                    localStats.bestHeadXCm,
                    localStats.bestHeadYCm,
                    localStats.bestHeadZCm,
                    localStats.bestPosXCm,
                    localStats.bestPosYCm,
                    localStats.bestPosZCm,
                    localStats.cameraXCm,
                    localStats.cameraYCm,
                    localStats.cameraZCm);
                lastLoggedRawCount = raw_entities.size();
                lastLoggedValidatedCount = valid_count;
                lastProcessLogTick = now;
            }
        }
        recordEntityCycle();
    }
}
