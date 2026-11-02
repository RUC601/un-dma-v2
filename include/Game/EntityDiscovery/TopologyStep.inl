// One iteration of the topology owner; shared with the bounded offline driver.
        const auto currentEpoch=OW::ProcessConnection::ConnectionEpoch();
        const auto currentScene=OW::EntitySceneGeneration();
        if(epoch!=currentEpoch || scene!=currentScene){epoch=currentEpoch;scene=currentScene;previous={};pending.clear();knownLinks.clear();lastProbe=0;}
        const auto registryState=OW::RefreshEntityRegistry(false);
        if(OW::DynamicEntityRegistryEnabled()) {
            if(!registryState.available){
                OW::RecordEntitySceneAudit(epoch,registryState.snapshot.generation,scene,OW::EntityRosterPolicy::SceneAudit::Unknown,static_cast<DWORD>(OW::EntityRegistryNowMs()));
                OW::RuntimeWorkerSleep(10);continue;
            }
            if(registryGeneration!=registryState.snapshot.generation) {
                registryGeneration=registryState.snapshot.generation;previous={};pending.clear();knownLinks.clear();lastProbe=0;
            }
        }
        const DWORD now=static_cast<DWORD>(OW::EntityRegistryNowMs());
        if(!lastProbe || now-lastProbe>=250) {
            const auto probeStartedMs=OW::EntityRegistryNowMs();
            auto topology=OW::ProbeEntityTopologyMembers(true);
            const float probeValues[]={float(topology.complete),float(topology.members.size())};
            RuntimeTrace::Emit(RuntimeTrace::TopologyProbe,probeStartedMs,OW::EntityRegistryNowMs(),topology.root,epoch,probeValues,2);
            REGISTRY_RELOCATION_EVENT("topology_read");
            const auto audit=OW::AuditEntityScene(topology);
            OW::RecordEntitySceneAudit(epoch,topology.registry.generation,scene,audit,static_cast<DWORD>(probeStartedMs));
            if(topology.complete && OW::ProcessConnection::ConnectionEpoch()==epoch && OW::EntitySceneGeneration()==scene && OW::AcceptEntityRegistry(topology.registry)) {
                {
                    auto added=OW::EntityRosterPolicy::AddedTopologyMembers(previous.members,topology.members,
                        previous.complete && previous.registry.generation==topology.registry.generation && previous.root==topology.root);
                    pending.erase(std::remove_if(pending.begin(),pending.end(),[&](auto parent) {
                        return !knownLinks.contains(parent) && !std::binary_search(topology.members.begin(),topology.members.end(),parent);
                    }),pending.end());
                    for(auto parent:added)if(std::find(pending.begin(),pending.end(),parent)==pending.end())pending.push_back(parent);
                }
                {
                    std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
                    if(OW::observed_topology_epoch!=epoch || topology.registry.generation!=OW::observed_entity_topology.registry.generation || topology.root!=OW::observed_entity_topology.root ||
                        topology.members!=OW::observed_entity_topology.members)++OW::observed_topology_revision;
                    OW::observed_entity_topology=topology;OW::observed_topology_epoch=epoch;
                    knownLinks.clear();
                    if(OW::entity_discoveries.connection==epoch && OW::entity_discoveries.registry==topology.registry.generation &&
                        OW::entity_discoveries.scene==scene)
                        for(const auto& [address,proof]:OW::entity_discoveries.actors)knownLinks.insert(proof.link);
                    if(OW::latest_raw_scan_snapshot.connection_epoch==epoch && OW::latest_raw_scan_snapshot.registry.generation==topology.registry.generation)
                        for(const auto& pair:OW::latest_raw_scan_snapshot.raw_entities)if(pair.second)knownLinks.insert(pair.second);
                    if(OW::entity_discoveries.actors.empty())for(auto member:topology.members)
                        if(std::find(pending.begin(),pending.end(),member)==pending.end())pending.push_back(member);
                }
                // Re-read known bindings as well: respawn can replace a child
                // actor without changing the address of its list/container.
                OW::EntityRosterPolicy::PrioritizeDiscoveryLinks(pending,knownLinks);
                previous=std::move(topology);
            }
            // A slow topology read must still leave time for discovery renewal.
            lastProbe=static_cast<DWORD>(OW::EntityRegistryNowMs());
        }
        const auto started=std::chrono::steady_clock::now();
        const auto budgetStarted=OW::EntityDiscoveryBudgetNow();
        while(!pending.empty() && OW::RuntimeWorkersRunning()) {
            const auto parent=pending.front();pending.erase(pending.begin());
#ifdef UNLEASHED_DISCOVERY_TEST
            if(OW::discoveryProbeTest)OW::discoveryProbeTest(parent,knownLinks.contains(parent),false,false);
#endif
            std::vector<std::pair<uint64_t,uint64_t>> bindingMembers;
            bool bindingComplete=false;
            std::vector<OW::EntityRosterPolicy::Discovery> discoveries;
            const auto pairs=OW::ProbePriorityActor(parent,previous,knownLinks.contains(parent),&bindingMembers,&bindingComplete,&discoveries);
#ifdef UNLEASHED_DISCOVERY_TEST
            if(OW::discoveryProbeTest)OW::discoveryProbeTest(parent,knownLinks.contains(parent),true,!pairs.empty());
#endif
            if(!pairs.empty()) {
                REGISTRY_RELOCATION_EVENT("discovery_read");
                OW::RefreshEntityRegistry(true);
                std::lock_guard<std::mutex> runningLock(OW::runtimeWorkerWaitMutex);
                std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
                if(OW::RuntimeWorkersRunning() && OW::ProcessConnection::IsConnected() && OW::ProcessConnection::ConnectionEpoch()==epoch &&
                    OW::EntitySceneGeneration()==scene && OW::observed_topology_epoch==epoch && OW::AcceptEntityRegistry(previous.registry)) {
                    OW::RawEntityScanSnapshot sample{};
                    sample.scene_generation=scene;sample.discoveries=std::move(discoveries);
                    sample.registry=previous.registry;
                    sample.raw_entities=pairs;
                    sample.binding_entities=std::move(bindingMembers);sample.binding_complete=bindingComplete;
                    for(const auto& pair:pairs)sample.actual_members.push_back(pair.first);
                    sample.generation=++OW::raw_scan_publication_sequence;sample.connection_epoch=epoch;sample.source=2;
                    if(std::any_of(pairs.begin(),pairs.end(),[&](const auto& pair) {
                        return std::find(OW::latest_raw_scan_snapshot.raw_entities.begin(),OW::latest_raw_scan_snapshot.raw_entities.end(),pair)==OW::latest_raw_scan_snapshot.raw_entities.end();
                    }))++OW::observed_topology_revision;
                    OW::MergeRecentRawDiscovery(sample);
                    if(sample.membership_entities!=OW::latest_raw_scan_snapshot.membership_entities)
                        ++OW::observed_topology_revision;
                    sample.valid=true;sample.complete=false;sample.scan_started_at=started;
                    sample.scan_finished_at=std::chrono::steady_clock::now();
                    RuntimeTrace::Emit(RuntimeTrace::Scan,sample.generation,pairs.size(),2,epoch);
                    OW::latest_raw_scan_snapshot=std::move(sample);
                    REGISTRY_RELOCATION_EVENT("discovery_committed");
                }
            }
            if(OW::EntityDiscoveryBudgetNow()-budgetStarted>=std::chrono::milliseconds(2))break;
        }
