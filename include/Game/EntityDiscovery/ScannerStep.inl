// One production scanner iteration; also used by the bounded offline driver.
        ++scanLoopCount;
        if (!OW::ProcessConnection::IsConnected()) {
            if (scanOwnerEpoch != 0) {
                cnStableScanPairs.clear();
                scanOwnerEpoch = 0;
                lastScanTick = 0;
                lastScanCycleTick = 0;
                lastTopologyCountProbeTick = 0;
                scanTopologyCandidateCount = 0;
                hasTopologyCandidateBaseline = false;
                ownerScanCompletedCount = 0;
            }
            OW::SetEntityScanNextDueTick(0);
            Diagnostics::RecordEntityScanCycle(0, 0.0);
            publishScanPipelineStats();
            OW::RuntimeWorkerSleep(100);
            continue;
        }

        const uint64_t currentConnectionEpoch = OW::ProcessConnection::ConnectionEpoch();
        const uint64_t currentScene = OW::EntitySceneGeneration();
        const auto currentRuntimeProfile = OW::offset::ActiveProfile();
        if (currentConnectionEpoch != scanOwnerEpoch || currentScene != scanOwnerScene ||
            currentRuntimeProfile != scanOwnerProfile) {
            cnStableScanPairs.clear();
            lastScanTick = 0;
            lastScanCycleTick = 0;
            lastTopologyCountProbeTick = static_cast<DWORD>(OW::EntityRegistryNowMs());
            scanTopologyCandidateCount = 0;
            hasTopologyCandidateBaseline = false;
            ownerScanCompletedCount = 0;
            scanOwnerEpoch = currentConnectionEpoch;
            scanOwnerScene = currentScene;
            scanOwnerProfile = currentRuntimeProfile;
            Diagnostics::Info(
                "Entity scan owner reset: profile=%s connection_epoch=%llu.",
                OW::offset::ActiveProfileName(),
                static_cast<unsigned long long>(scanOwnerEpoch));
        }

        const DWORD now = static_cast<DWORD>(OW::EntityRegistryNowMs());
        bool pending_scan = false;
        bool known_entities_empty = true;
        bool fast_rescan = false;
        bool latest_raw_entities_empty = true;
        uint64_t topologyRescanRequestCountSnapshot = 0;
        uint64_t topologyRescanCompletedRequestCountSnapshot = 0;
        if (scanLatestWinsEnabled) {
            std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
            pending_scan = OW::latest_raw_scan_snapshot.valid &&
                OW::latest_raw_scan_snapshot.generation != OW::last_consumed_raw_scan_generation;
            latest_raw_entities_empty =
                !OW::latest_raw_scan_snapshot.valid ||
                OW::latest_raw_scan_snapshot.raw_entities.empty();
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!scanLatestWinsEnabled) {
                pending_scan = OW::abletotread != 0;
                latest_raw_entities_empty = OW::ow_entities_scan.empty();
            }
            known_entities_empty = OW::ow_entities.empty() && latest_raw_entities_empty;
            topologyRescanRequestCountSnapshot =
                OW::entity_topology_rescan_request_count;
            topologyRescanCompletedRequestCountSnapshot =
                OW::entity_topology_rescan_completed_request_count;
            fast_rescan =
                topologyRescanRequestCountSnapshot !=
                    topologyRescanCompletedRequestCountSnapshot ||
                (OW::entity_fast_scan_until_tick != 0 &&
                 now < OW::entity_fast_scan_until_tick);
        }
        scanTopologyRescanRequestCount = topologyRescanRequestCountSnapshot;

        // Poll before pending-result and stable-topology gates, including legacy mode.
        const auto registryState=OW::RefreshEntityRegistry(false);
        if(OW::DynamicEntityRegistryEnabled()) {
            if(!registryState.available) {
                OW::RecordEntitySceneAudit(currentConnectionEpoch,registryState.snapshot.generation,currentScene,OW::EntityRosterPolicy::SceneAudit::Unknown,static_cast<DWORD>(OW::EntityRegistryNowMs()));
                OW::RuntimeWorkerSleep(50);continue;
            }
            if(scanRegistryGeneration!=registryState.snapshot.generation) {
                scanRegistryGeneration=registryState.snapshot.generation;
                cnStableScanPairs.clear();lastTopologyMembers.clear();lastTopologyRoot=0;
                hasTopologyCandidateBaseline=false;lastScanTick=0;lastTopologyCountProbeTick=0;
            }
            fast_rescan=fast_rescan || registryState.rescanPending;
        }

        const bool stableTopologyCanReuse=coldTopologyScanEnabled && lastScanTick!=0 && !known_entities_empty && !fast_rescan;
        if(stableTopologyCanReuse && now-lastScanTick<2000) {
            bool changed=false;
            if(lastTopologyCountProbeTick==0 || now-lastTopologyCountProbeTick>=250) {
                lastTopologyCountProbeTick=now;++scanTopologyCountProbeCount;
                const auto topology=OW::ProbeEntityTopologyMembers(false);
                if(topology.complete) {
                    changed=topology.root!=lastTopologyRoot || topology.members!=lastTopologyMembers;
                    lastTopologyMembers=topology.members;lastTopologyRoot=topology.root;
                    scanTopologyCandidateCount=topology.members.size();
                    if(changed)++scanTopologyCountProbeChangeCount;
                }
            }
            if(!changed) {
                ++scanSkipStableTopologyCount;updateScanPendingAge(now);publishScanPipelineStats();
                OW::RuntimeWorkerSleep(OW::kEntityScannerIdleSleepMs);continue;
            }
            fast_rescan=true;
        }

        const DWORD scanInterval = (known_entities_empty || fast_rescan)
            ? OW::kEntityEmptyScanIntervalMs
            : OW::kEntityScanIntervalMs;
        OW::SetEntityScanNextDueTick(lastScanTick == 0 ? now : lastScanTick + scanInterval);
        const bool scanDue = lastScanTick == 0 || now - lastScanTick >= scanInterval;
        updateScanPendingAge(now);
        if (scanDue)
            ++scanDueCount;
        else
            ++scanSkipNotDueCount;
        if ((scanLatestWinsEnabled || !pending_scan) && scanDue) {
            if (lastScanCycleTick != 0) {
                const DWORD elapsed = now - lastScanCycleTick;
                entityScanHz = elapsed > 0 ? (1000.0 / static_cast<double>(elapsed)) : 0.0;
            }
            lastScanCycleTick = now;
            lastScanTick = now;
            OW::SetEntityScanNextDueTick(now + scanInterval);

            ++scanStartedCount;
            const uint64_t scanConnectionEpoch = currentConnectionEpoch;
            const uint64_t scanScene = currentScene;
            const auto scanRuntimeProfile = currentRuntimeProfile;
            uint64_t scanTopologyRevision=0;
            {std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);scanTopologyRevision=OW::observed_topology_revision;}
            const auto getOwEntitiesStartedAt = std::chrono::steady_clock::now();
            std::vector<std::pair<uint64_t, uint64_t>> scanned{};
            {
                OW::ScopedEntityScanDmaActivity scanDmaActivity;
                scanned = OW::get_ow_entities();
            }
            scanGetOwEntitiesMs = static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - getOwEntitiesStartedAt).count()) / 1000.0;
            ++scanCompletedCount;
            ++ownerScanCompletedCount;
            if (!OW::ProcessConnection::IsConnected() ||
                OW::EntitySceneGeneration()!=scanScene ||
                OW::ProcessConnection::ConnectionEpoch() != scanConnectionEpoch ||
                OW::offset::ActiveProfile() != scanRuntimeProfile) {
                Diagnostics::Trace(
                    "Discarded entity scan from stale connection epoch=%llu.",
                    static_cast<unsigned long long>(scanConnectionEpoch));
                publishScanPipelineStats();
                continue;
            }
            ++scanGeneration;
            if (scanGetOwEntitiesMs > scanMaxGetOwEntitiesMs) {
                scanMaxGetOwEntitiesMs = scanGetOwEntitiesMs;
                scanMaxGetOwEntitiesGeneration = scanGeneration;
                scanMaxGetOwEntitiesDetail = Diagnostics::SnapshotEntityScanDetailStats();
            }
            bool scanComplete=OW::entityScanComplete;
            const auto registry=OW::entityScanRegistry;
            std::vector<OW::EntityRosterPolicy::Discovery> scanDiscoveries;
            if(OW::AcceptEntityRegistry(registry)) {
                const auto topology=OW::ProbeEntityTopologyMembers(true);
                scanComplete=scanComplete && topology.complete && topology.member_ids_complete;
                const auto audit=OW::AuditEntityScene(topology);
                OW::RecordEntitySceneAudit(scanConnectionEpoch,topology.registry.generation,scanScene,
                    OW::entityScanReadComplete ? audit : OW::EntityRosterPolicy::SceneAudit::Unknown,static_cast<DWORD>(OW::EntityRegistryNowMs()));
                std::unordered_set<uint64_t> verifiedLinks;
                std::vector<uint64_t> links;
                for(const auto& pair:scanned)if(verifiedLinks.insert(pair.second).second)links.push_back(pair.second);
                {
                    std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
                    for(const auto& [address,proof]:OW::entity_discoveries.actors)
                        if(verifiedLinks.insert(proof.link).second)links.push_back(proof.link);
                }
                for(auto link:links) {
                    std::vector<OW::EntityRosterPolicy::Discovery> discoveries;
                    if(!OW::ProbePriorityActor(link,topology,true,nullptr,nullptr,&discoveries).empty())
                        scanDiscoveries.insert(scanDiscoveries.end(),discoveries.begin(),discoveries.end());
                }
                for(const auto& proof:scanDiscoveries) {
                    const auto pair=std::make_pair(proof.address,proof.link);
                    if(std::find(scanned.begin(),scanned.end(),pair)==scanned.end())scanned.push_back(pair);
                }
                {
                    std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
                    for(const auto& [address,proof]:OW::entity_discoveries.actors) {
                        const bool stillInTable=std::binary_search(topology.members.begin(),topology.members.end(),address) ||
                            std::binary_search(topology.members.begin(),topology.members.end(),proof.link);
                        const bool verified=std::any_of(scanDiscoveries.begin(),scanDiscoveries.end(),[&](const auto& current) {
                            return current.address==address && current.link==proof.link;
                        });
                        const bool replaced=std::any_of(scanDiscoveries.begin(),scanDiscoveries.end(),[&](const auto& current) {
                            return current.link==proof.link && current.address!=address;
                        });
                        if(stillInTable && !verified && !replaced)scanComplete=false;
                    }
                }
            } else {
                OW::RecordEntitySceneAudit(scanConnectionEpoch,registry.generation,scanScene,OW::EntityRosterPolicy::SceneAudit::Unknown,static_cast<DWORD>(OW::EntityRegistryNowMs()));
            }
            if(OW::entityScanStatus==OW::EntityRegistry::ScanStatus::Invalidated ||
                OW::entityScanStatus==OW::EntityRegistry::ScanStatus::Unavailable) {
                ++scanFailedCount;publishScanPipelineStats();OW::RuntimeWorkerSleep(50);continue;
            }
            REGISTRY_RELOCATION_EVENT("scanner_before_commit");
            OW::RefreshEntityRegistry(true);
            if(!OW::AcceptEntityRegistry(registry)) {
                ++scanFailedCount;publishScanPipelineStats();OW::RuntimeWorkerSleep(50);continue;
            }
            RuntimeTrace::Emit(RuntimeTrace::Scan,scanGeneration,scanned.size(),scanComplete,scanConnectionEpoch);
            scanResultRawCount = scanned.size();
            if (scanned.empty() && !scanComplete)
                ++scanFailedCount;
            Diagnostics::RecordEntityScanCycle(scanned.size(), entityScanHz);
            Diagnostics::Trace("Entity scan cycle found %zu raw entities.", scanned.size());
            if (OW::PipelineDebugEnabled()) {
                const DWORD logNow = static_cast<DWORD>(OW::EntityRegistryNowMs());
                if (lastLoggedScanCount != scanned.size() || lastScanLogTick == 0 ||
                    logNow - lastScanLogTick >= 1000) {
                    Diagnostics::Info("[PIPELINE] Stage 3 entity scan raw=%zu scan_hz=%.1f interval_ms=%lu",
                        scanned.size(),
                        entityScanHz,
                        static_cast<unsigned long>(scanInterval));
                    lastLoggedScanCount = scanned.size();
                    lastScanLogTick = logNow;
                }
            }
            ++scanPublishAttemptCount;
            bool scanCommitted=false;
            if (scanLatestWinsEnabled) {
                if (scanComplete || !scanned.empty()) {
                    OW::RawEntityScanSnapshot snapshot{};
                    snapshot.scene_generation=scanScene;snapshot.discoveries=scanDiscoveries;
                    snapshot.registry=registry;
                    snapshot.raw_entities = std::move(scanned);
                    for(const auto& pair:snapshot.raw_entities)snapshot.actual_members.push_back(pair.first);
                    snapshot.connection_epoch=scanConnectionEpoch;
                    snapshot.complete = scanComplete;
                    snapshot.scan_started_at = getOwEntitiesStartedAt;
                    snapshot.scan_finished_at = std::chrono::steady_clock::now();
                    snapshot.valid = true;
                    bool published = false;
                    {
#include "Game/EntityDiscovery/CommitFullScan.inl"
                    }
                    if (published) {
                        ++scanPublishSuccessCount;
                        lastScanSuccessTick = static_cast<DWORD>(OW::EntityRegistryNowMs());
                    } else {
                        Diagnostics::Trace(
                            "Discarded entity scan at stale latest-wins commit epoch=%llu.",
                            static_cast<unsigned long long>(scanConnectionEpoch));
                    }
                }
            } else {
                std::scoped_lock lock(g_mutex,OW::raw_scan_mutex);
                if (OW::ProcessConnection::IsConnected() &&
                    OW::EntitySceneGeneration()==scanScene &&
                    OW::ProcessConnection::ConnectionEpoch() == scanConnectionEpoch &&
                    OW::offset::ActiveProfile() == scanRuntimeProfile &&
                    OW::abletotread == 0 && OW::AcceptEntityRegistry(registry)) {
                    OW::RawEntityScanSnapshot evidence{};
                    evidence.scene_generation=scanScene;evidence.connection_epoch=scanConnectionEpoch;
                    evidence.registry=registry;evidence.complete=scanComplete;evidence.raw_entities=scanned;
                    evidence.discoveries=scanDiscoveries;OW::MergeRecentRawDiscovery(evidence);
                    OW::latest_raw_scan_snapshot=std::move(evidence);
                    OW::EntityRosterPolicy::MergePartialScan(scanned,OW::ow_entities,
                        scanComplete || OW::ow_entities_registry.generation!=registry.generation);
                    OW::ow_entities_scan = std::move(scanned);
                    OW::ow_entities_scan_registry=registry;
                    OW::ow_entities_scan_complete=scanComplete;
                    OW::ow_entities_scene=scanScene;
                    OW::abletotread = 1;
                    scanCommitted=scanComplete;
                    ++scanPublishSuccessCount;
                    lastScanSuccessTick = static_cast<DWORD>(OW::EntityRegistryNowMs());
                }
            }
            if(scanCommitted)scanCommitted=OW::AcknowledgeEntityRegistry(registry,OW::EntityRegistry::ScanStatus::Complete);
            if (coldTopologyScanEnabled && fast_rescan && scanCommitted) {
                std::lock_guard<std::mutex> lock(g_mutex);
                OW::entity_topology_rescan_completed_request_count =
                    (std::max)(
                        OW::entity_topology_rescan_completed_request_count,
                        topologyRescanRequestCountSnapshot);
                if (OW::entity_topology_rescan_request_count ==
                    OW::entity_topology_rescan_completed_request_count) {
                    OW::entity_fast_scan_until_tick = 0;
                }
            }
        } else if (pending_scan && scanDue) {
            ++scanSkipPendingCount;
        }
        updateScanPendingAge(static_cast<DWORD>(OW::EntityRegistryNowMs()));
        publishScanPipelineStats();
        OW::RuntimeWorkerSleep(OW::kEntityScannerIdleSleepMs);
