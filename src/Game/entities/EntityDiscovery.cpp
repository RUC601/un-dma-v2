#include "EntityWorkers.hpp"
#include "Game/Overwatch.hpp"
#include "Game/EntityDiscovery/WorkSchedule.hpp"
#include "Game/EntityDiscovery/PositiveCandidates.hpp"
#include <memory>

namespace {
struct DiscoveryState {
    std::unique_ptr<OW::EntityScanTask> scan;
    OW::EntityTopologySample topology;
    OW::EntityRegistry::Snapshot registry;
    std::vector<OW::EntityRosterPolicy::Discovery> proofs;
    std::vector<std::pair<uint64_t,uint64_t>> pendingPairs;
    std::vector<uint64_t> knownLinks;
    std::vector<uint64_t> validationLinks;
    std::unordered_set<uint64_t> worldObjects;
    std::vector<OW::EntityRosterPolicy::Discovery> validatedProofs;
    size_t pairCursor=0, publishedPairCount=0;
    size_t validationCursor=0;
    uint64_t validationRevision=0;
    bool validating=false, bindingsComplete=false;
    uint64_t epoch=0, scene=0, completed=0, scanRequest=0;
    DWORD lastScan=0, lastKnownProbe=0, lastLocalProbe=0;
    uint64_t localLink=0;
    OW::EntityRosterPolicy::Discovery lastLocalIdentity{};
    OW::EntityDiscovery::WorkSchedule workSchedule;
    OW::EntityDiscovery::BindingSchedule bindingSchedule;
    OW::EntityDiscovery::PositiveCandidates positive;
    std::vector<uint64_t> positiveTable;
    uint64_t positiveTableOffset=0;
    DWORD positiveTableTick=0,positiveHeaderTick=0;
    bool positiveTableReading=false;
    uint8_t positiveCursor=0;
    uint64_t positiveAttempts=0,positiveIndexAttempts=0,positiveAccepted=0;
    std::chrono::steady_clock::time_point started;
};

bool PublishDiscovery(DiscoveryState& state,
        std::vector<std::pair<uint64_t,uint64_t>> pairs,
        std::vector<OW::EntityRosterPolicy::Discovery> proofs,
        bool complete, bool bindingComplete=false,
        std::vector<std::pair<uint64_t,uint64_t>> bindings={}, bool scanPositive=false,bool currentPositive=false,
        bool* committed=nullptr) {
    if(committed)*committed=false;
    OW::RawEntityScanSnapshot sample{};
    sample.scene_generation=state.scene; sample.connection_epoch=state.epoch;
    sample.registry=state.registry; sample.raw_entities=std::move(pairs);
    sample.discoveries=std::move(proofs); sample.complete=complete;
    sample.binding_complete=bindingComplete; sample.binding_entities=std::move(bindings);
    for(const auto& pair:sample.raw_entities)sample.actual_members.push_back(pair.first);
    sample.scan_started_at=state.started; sample.scan_finished_at=std::chrono::steady_clock::now();
    // A live binding keeps its replacement authority while the full scan is validating.
    sample.source=bindingComplete || currentPositive?2:(state.validating?1:2); sample.valid=true;
    std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
    if(!OW::RuntimeWorkersRunning() || !OW::ProcessConnection::IsConnected() ||
        OW::ProcessConnection::ConnectionEpoch()!=state.epoch ||
        OW::EntitySceneGeneration()!=state.scene || !OW::AcceptEntityRegistry(state.registry))return false;
    sample.complete=OW::EntityRosterPolicy::ScanCanRetire(sample.complete,
        state.validationRevision,OW::observed_topology_revision);
    const auto previousRevision=OW::observed_topology_revision;
    auto previousMembers=OW::latest_raw_scan_snapshot.membership_entities;
    OW::MergeRecentRawDiscovery(sample);
    if(!sample.complete) {
        auto currentMembers=sample.membership_entities;
        std::sort(previousMembers.begin(),previousMembers.end());
        std::sort(currentMembers.begin(),currentMembers.end());
        if(previousMembers!=currentMembers)++OW::observed_topology_revision;
    }
    // Only this scan's own positive discoveries may advance its deletion fence.
    if(scanPositive && state.validationRevision==previousRevision)
        state.validationRevision=OW::observed_topology_revision;
    sample.generation=++OW::raw_scan_publication_sequence;
    const bool authoritative=sample.complete;
    OW::latest_raw_scan_snapshot=std::move(sample);
    if(committed)*committed=true;
    return authoritative;
}
}

thread_local const char* discoveryOperation="idle";
static void AdvancePositiveDiscovery(DiscoveryState& state,DWORD now) {
    // One bounded table block, 128 small header fingerprints, or one validated
    // current binding per visit. No scan-index mutation or deletion authority.
    const std::array<bool,3> due{state.positiveTableReading || !state.positiveTableTick || now-state.positiveTableTick>=250,
        !state.positive.members.empty() && (!state.positive.urgent.empty() || !state.positiveHeaderTick || now-state.positiveHeaderTick>=8),
        !state.positive.probes.empty()};
    uint8_t work=3;
    for(uint8_t offset=0;offset<due.size();++offset) {
        const auto index=uint8_t((state.positiveCursor+offset)%due.size());
        if(due[index]){work=index;state.positiveCursor=uint8_t((index+1)%due.size());break;}
    }
    if(work==3)return;
    if(work==0) {
        discoveryOperation="positive_table";
        if(!state.positiveTableReading) {
            state.positiveTable.clear();state.positiveTableOffset=0;state.positiveTableReading=true;
        }
        std::array<uint8_t,0x10000> bytes{};
        const size_t count=OW::EntityRegistry::ChunkBytes(state.registry,state.positiveTableOffset,bytes.size());
        const bool exact=count && mem.ReadFreshExact(state.registry.root+state.positiveTableOffset,bytes.data(),count);
        if(!exact || !OW::AcceptEntityRegistry(state.registry)) {
            state.positiveTableReading=false;state.positiveTableTick=now;return;
        }
        std::vector<uint64_t> blockMembers;
        for(size_t offset=0;offset+16<=count;offset+=16) {
            uint64_t parent=0;std::memcpy(&parent,bytes.data()+offset,8);
            if(OW::IsPlausibleUserPointer(parent) && OW::IsLikelyCnEntityParent(parent,state.registry.root,
                state.registry.Bytes(),OW::SDK->dwGameBase,OW::offset::Active().imageSize))
                blockMembers.push_back(parent);
        }
        // Incremental additions need the same current table ticket that the
        // old whole-table commit required. A cached registry check is not enough.
        OW::RefreshEntityRegistry(true);
        if(!OW::AcceptEntityRegistry(state.registry)) {
            state.positiveTableReading=false;state.positiveTableTick=now;return;
        }
        state.positiveTable.insert(state.positiveTable.end(),blockMembers.begin(),blockMembers.end());
        state.positive.ObserveAdditionalMembers(std::move(blockMembers));
        state.positiveTableOffset+=count;
        if(state.positiveTableOffset>=state.registry.Bytes()) {
            state.positive.ObserveMembers(std::move(state.positiveTable));
            state.positiveTableReading=false;state.positiveTableTick=now;
        }
        return;
    }
    if(work==2) {
        discoveryOperation="positive_binding";
        const auto parent=state.positive.NextProbe();if(!parent)return;
        OW::EntityTopologySample topology;
        topology.root=state.registry.root;topology.registry=state.registry;
        topology.complete=true;topology.member_ids_complete=false;topology.members=state.positive.members;
        topology.member_ids_complete=state.positive.FreshMemberIds(now,topology.member_ids);
        ++state.positiveAttempts;
        state.positiveIndexAttempts+=topology.member_ids_complete;
        std::vector<OW::EntityRosterPolicy::Discovery> proofs;
        auto pairs=OW::ProbePriorityActor(parent,topology,false,nullptr,nullptr,&proofs,nullptr,nullptr,false,true);
        bool accepted=false;
        if(!pairs.empty() && !proofs.empty() && OW::AcceptEntityRegistry(state.registry))
            PublishDiscovery(state,std::move(pairs),proofs,false,false,{},false,true,&accepted);
        if(accepted) {
            ++state.positiveAccepted;
            if(std::find(state.knownLinks.begin(),state.knownLinks.end(),parent)==state.knownLinks.end())state.knownLinks.push_back(parent);
            Diagnostics::Info("[V2-POSITIVE-DISCOVERY] actor=0x%llX link=0x%llX match=%u hero=0x%llX",
                proofs.front().address,parent,proofs.front().match,proofs.front().hero);
        }
        state.positive.FinishProbe(parent,accepted,now);
        return;
    }
    discoveryOperation="positive_headers";state.positiveHeaderTick=now;
    constexpr size_t headerBatch=128;
    const auto parents=state.positive.NextHeaders(headerBatch);
    struct Header {std::array<uint8_t,44> bytes{};DWORD actual=0;bool prepared=false;};
    std::array<Header,headerBatch> headers{};
    const auto handle=mem.CreateScatterHandle();if(!handle)return;
    for(size_t i=0;i<parents.size();++i)headers[i].prepared=mem.AddScatterReadRequest(handle,
        parents[i]+0x110,headers[i].bytes.data(),headers[i].bytes.size(),&headers[i].actual);
    const bool executed=mem.ExecuteReadScatter(handle);mem.CloseScatterHandle(handle);
    if(!OW::AcceptEntityRegistry(state.registry))return;
    for(size_t i=0;i<parents.size();++i) {
        OW::EntityDiscovery::PositiveCandidates::Fingerprint value;
        std::memcpy(value.types.data(),headers[i].bytes.data(),32);
        std::memcpy(&value.match,headers[i].bytes.data()+0x28,4);
        state.positive.ObserveHeader(parents[i],value,executed && headers[i].prepared && headers[i].actual==44,
            now,OW::offset::Active().typeLink,OW::offset::Active().typeHeroId);
    }
}
static void AdvanceDiscovery(DiscoveryState& state) {
    Diagnostics::ScopedDmaCallsite tag(Diagnostics::DmaCallsite::EntityScan);
    const auto epoch=OW::ProcessConnection::ConnectionEpoch();
    const auto scene=OW::EntitySceneGeneration();
    const auto registry=OW::RefreshEntityRegistry(false);
    if(state.epoch!=epoch || state.scene!=scene || state.registry.generation!=registry.snapshot.generation) {
        state=DiscoveryState{};state.epoch=epoch;state.scene=scene;state.registry=registry.snapshot;
    }
    if(!registry.available)return;
    const DWORD now=static_cast<DWORD>(OW::EntityRegistryNowMs());
    const auto frame=OW::SnapshotFrame();
    const auto& local=frame->local;
    const bool currentMapProfile=
        OW::offset::Active().contextMap==OW::offset::ContextMapMode::Protected155284 ||
        OW::offset::Active().contextMap==OW::offset::ContextMapMode::World155284;
    // Keep only a controller hint across body replacement; the probe still validates the entire new binding.
    if(local.LinkParent && local.AngleBase && local.connection_epoch==epoch && local.scene_generation==scene)
        state.localLink=local.LinkParent;
    const auto work=state.workSchedule.Select(
        state.localLink && state.topology.complete &&
            (!state.lastLocalProbe || now-state.lastLocalProbe>=50),
        !state.knownLinks.empty() && now-state.lastKnownProbe>=32 && state.topology.complete,
        state.scan || state.pairCursor<state.pendingPairs.size() || !state.lastScan || now-state.lastScan>=250,
        currentMapProfile && OW::DynamicEntityRegistryEnabled() &&
            (state.positiveTableReading || !state.positiveTableTick || now-state.positiveTableTick>=250 ||
             !state.positive.probes.empty() ||
             (!state.positive.members.empty() && (!state.positive.urgent.empty() || now-state.positiveHeaderTick>=8))));
    if(work==OW::EntityDiscovery::Work::None)return;
    if(work==OW::EntityDiscovery::Work::PositiveBinding){AdvancePositiveDiscovery(state,now);return;}
    if(work==OW::EntityDiscovery::Work::LocalBinding) {
        discoveryOperation="local_binding";
        std::vector<OW::EntityRosterPolicy::Discovery> proofs;
        std::vector<std::pair<uint64_t,uint64_t>> bindings;
        std::vector<OW::EntityRosterPolicy::Discovery> hotActors;
        if(local.HasUsableBody(now,scene))
            hotActors.push_back({local.address,local.LinkParent,local.HeroID,local.match_id,local.anchor_sample_tick_ms});
        bool complete=false;
        auto pairs=OW::ProbePriorityActor(state.localLink,state.topology,true,&bindings,&complete,&proofs,nullptr,&hotActors,true);
        for(const auto& proof:proofs)if(proof.address!=state.lastLocalIdentity.address ||
            proof.match!=state.lastLocalIdentity.match || proof.hero!=state.lastLocalIdentity.hero) {
            Diagnostics::Info("[V2-LOCAL-PROBE] actor=0x%llX link=0x%llX hero=0x%llX match=%u binding_complete=%d",
                proof.address,proof.link,proof.hero,proof.match,int(complete));
            state.lastLocalIdentity=proof;
        }
        if(!pairs.empty())PublishDiscovery(state,std::move(pairs),std::move(proofs),false,complete,std::move(bindings));
        state.lastLocalProbe=static_cast<DWORD>(OW::EntityRegistryNowMs());
        return;
    }
    // Renew known bindings from the same topology used by the resumable full scan.
    if(work==OW::EntityDiscovery::Work::KnownBinding) {
        discoveryOperation="known_binding";
        state.lastKnownProbe=now;
        std::vector<uint8_t> recovery(state.knownLinks.size(),1);
        for(size_t i=0;i<state.knownLinks.size();++i)
            for(const auto& entity:frame->entities)
                if(entity.LinkParent==state.knownLinks[i] && entity.HasUsableBody(now,scene)) {
                    recovery[i]=0;break;
                }
        const auto selected=state.bindingSchedule.Select(recovery);
        const auto parent=state.knownLinks[selected];
        std::vector<OW::EntityRosterPolicy::Discovery> proofs;
        std::vector<std::pair<uint64_t,uint64_t>> bindings;
        bool complete=false;
        std::vector<OW::EntityRosterPolicy::Discovery> hotActors;
        for(const auto& entity:frame->entities)if(entity.HasUsableBody(now,scene))
            hotActors.push_back({entity.address,entity.LinkParent,entity.HeroID,entity.match_id,entity.anchor_sample_tick_ms});
        // During respawn the old UID index can still point at an intact corpse.
        // Consult the controller's current map before that cached index, then
        // retain the probe's exact life/identity and final-map checks.
        const bool currentMap=recovery[selected] && currentMapProfile;
        OW::EntityTopologySample recoveryTopology;
        const bool freshRecoveryIndex=currentMap && state.positive.Contains(parent) &&
            state.positive.FreshMemberIds(now,recoveryTopology.member_ids);
        if(freshRecoveryIndex) {
            recoveryTopology.root=state.registry.root;recoveryTopology.registry=state.registry;
            recoveryTopology.complete=true;recoveryTopology.member_ids_complete=true;
            recoveryTopology.members=state.positive.members;
        }
        auto pairs=OW::ProbePriorityActor(parent,freshRecoveryIndex?recoveryTopology:state.topology,true,
            &bindings,&complete,&proofs,nullptr,&hotActors,currentMap,freshRecoveryIndex);
        if(!pairs.empty())PublishDiscovery(state,std::move(pairs),std::move(proofs),false,complete,
            std::move(bindings),false,freshRecoveryIndex);
        return;
    }
    if(state.pairCursor<state.pendingPairs.size()) {
        discoveryOperation="new_binding";
        const auto pair=state.pendingPairs[state.pairCursor++];
        uint64_t objectId=0;
        const auto read=[](uint64_t at,void* data,size_t size){return OW::SDK->read_range(at,data,size);};
        if(pair.first==pair.second && V2::WorldObjects::ReadId(pair.first,read,objectId)) {
            state.worldObjects.insert(pair.first);
            // Objects have no player binding proof and never gain actor-deletion authority.
            PublishDiscovery(state,{pair},{},false);
            return;
        }
        std::vector<OW::EntityRosterPolicy::Discovery> proofs;
        auto pairs=OW::ProbePriorityActor(pair.second,OW::entityScanTopology,false,nullptr,nullptr,&proofs);
        if(!pairs.empty()) {
            state.topology=OW::entityScanTopology;
            for(const auto& proof:proofs)
                if(std::find(state.knownLinks.begin(),state.knownLinks.end(),proof.link)==state.knownLinks.end())state.knownLinks.push_back(proof.link);
            state.proofs.insert(state.proofs.end(),proofs.begin(),proofs.end());
            PublishDiscovery(state,std::move(pairs),std::move(proofs),false,false,{},true);
        }
        return;
    }
    if(!state.scan) {
        if(state.lastScan && now-state.lastScan<250)return;
        state.started=std::chrono::steady_clock::now();state.lastScan=now;
        state.proofs.clear();state.pendingPairs.clear();state.worldObjects.clear();state.pairCursor=0;state.publishedPairCount=0;
        state.validationLinks.clear();state.validatedProofs.clear();state.validationCursor=0;
        state.validating=false;state.bindingsComplete=false;
        state.scan=std::make_unique<OW::EntityScanTask>(OW::BeginEntityScan());
        {std::lock_guard<std::mutex> lock(g_mutex);state.scanRequest=OW::entity_topology_rescan_request_count;}
        {std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);state.validationRevision=OW::observed_topology_revision;}
    }
    discoveryOperation="scan_slice";
    const auto sliceStarted=std::chrono::steady_clock::now();
    const auto previousPairCount=state.scan->Pairs().size();
    // Keep the 650us time budget, but allow pure in-memory pairing yields to
    // drain without wasting hundreds of discovery visits on empty slices.
    for(int step=0;step<512 && !state.scan->Done();++step) {
        const auto phase=state.scan->handle.promise().progress.phase;
        discoveryOperation=phase==OW::EntityScanPhase::Slots?"scan_slots":phase==OW::EntityScanPhase::Headers?"scan_headers":"scan_pairing";
        state.scan->Resume();
        if(state.scan->Pairs().size()!=previousPairCount ||
            std::chrono::steady_clock::now()-sliceStarted>=std::chrono::microseconds(650))break;
    }
    const auto& pairs=state.scan->Pairs();
    while(state.publishedPairCount<pairs.size())state.pendingPairs.push_back(pairs[state.publishedPairCount++]);
    if(!state.scan->Done() || state.pairCursor<state.pendingPairs.size())return;
    state.topology=OW::entityScanTopology;
    // Revalidate the final paired roster with current headers. A table read can
    // finish successfully while all the actor bindings have become unusable.
    if(!state.validating) {
        state.validating=true;
        state.bindingsComplete=OW::entityScanComplete && state.topology.member_ids_complete;
        for(const auto& pair:pairs)if(!state.worldObjects.contains(pair.first))state.validationLinks.push_back(pair.second);
        for(const auto& [parent,header]:OW::entityScanHeaders) {
            if(state.worldObjects.contains(parent))continue;
            uint64_t linkBits=0,heroBits=0;
            const auto linkType=OW::offset::Active().typeLink,heroType=OW::offset::Active().typeHeroId;
            if((header.ReadParentOffset(0x110+8ull*(linkType>>6),linkBits) && (linkBits&(1ull<<(linkType&63)))) ||
                (header.ReadParentOffset(0x110+8ull*(heroType>>6),heroBits) && (heroBits&(1ull<<(heroType&63)))))
                state.validationLinks.push_back(parent);
        }
        std::sort(state.validationLinks.begin(),state.validationLinks.end());
        state.validationLinks.erase(std::unique(state.validationLinks.begin(),state.validationLinks.end()),state.validationLinks.end());
    }
    if(state.validationCursor<state.validationLinks.size()) {
        discoveryOperation="validate_roster";
        const auto parent=state.validationLinks[state.validationCursor++];
        bool resolved=false;
        std::vector<OW::EntityRosterPolicy::Discovery> proofs;
        const auto verified=OW::ProbePriorityActor(parent,state.topology,false,nullptr,&resolved,&proofs);
        if(!verified.empty() && resolved) {
            state.validatedProofs.insert(state.validatedProofs.end(),proofs.begin(),proofs.end());
        } else if(!verified.empty() && !proofs.empty() &&
            OW::offset::Active().contextMap==OW::offset::ContextMapMode::World155284) {
            // A current World map may discover a body that the old scan could
            // not pair. Publish that proof without granting roster retirement.
            state.bindingsComplete=false;
            state.proofs.insert(state.proofs.end(),proofs.begin(),proofs.end());
            PublishDiscovery(state,verified,proofs,false,false,{},true,true);
        } else {
            auto member=state.topology;member.members={parent};
            if(OW::AuditEntityScene(member)!=OW::EntityRosterPolicy::SceneAudit::Empty)
                state.bindingsComplete=false;
        }
        return;
    }
    const bool readComplete=OW::entityScanComplete;
    const bool authoritative=OW::EntityRosterPolicy::AuthoritativeRoster(readComplete,
        state.bindingsComplete,state.validatedProofs);
    if(readComplete) {
        discoveryOperation="scene_audit";
        const auto audit=!state.proofs.empty()?OW::EntityRosterPolicy::SceneAudit::Present:
            OW::AuditEntityScene(state.topology,&OW::entityScanHeaders);
        OW::RecordEntitySceneAudit(state.epoch,state.registry.generation,state.scene,audit,now);
    }
    const bool complete=PublishDiscovery(state,pairs,authoritative?state.validatedProofs:state.proofs,authoritative);
    {
        std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
        OW::observed_entity_topology=state.topology;OW::observed_topology_epoch=epoch;
        state.knownLinks.clear();
        std::unordered_set<uint64_t> seen;
        for(const auto& [address,proof]:OW::entity_discoveries.actors)
            if(seen.insert(proof.link).second)state.knownLinks.push_back(proof.link);
    }
    if(readComplete) {
        OW::AcknowledgeEntityRegistry(state.registry,OW::EntityRegistry::ScanStatus::Complete);
        std::lock_guard<std::mutex> lock(g_mutex);
        OW::entity_topology_rescan_completed_request_count=(std::max)(
            OW::entity_topology_rescan_completed_request_count,state.scanRequest);
    }
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-state.started).count();
    Diagnostics::RecordEntityScanCycle(pairs.size(),elapsed>0?1.0/elapsed:0);
    Diagnostics::Info("[UNDMA-V2-DISCOVERY] completed=%llu pairs=%zu members=%zu read_complete=%d roster_complete=%d proofs=%zu elapsed_ms=%.1f",
        ++state.completed,pairs.size(),state.topology.members.size(),readComplete,complete,state.validatedProofs.size(),elapsed*1000);
    state.scan.reset();
    state.validating=false;
}

void StepAcquisitionDiscovery() {
    const auto started=std::chrono::steady_clock::now();
    discoveryOperation="idle";
    static thread_local DiscoveryState state;
    AdvanceDiscovery(state);
    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    static thread_local auto reportedAt=started;
    static thread_local double maximum=0;
    static thread_local const char* slowest="idle";
    if(elapsed>maximum){maximum=elapsed;slowest=discoveryOperation;}
    if(started-reportedAt>=std::chrono::seconds(5)) {
        Diagnostics::Info("[UNDMA-V2-DISCOVERY-TIME] max_ms=%.3f operation=%s positive_members=%zu positive_headers=%zu positive_attempts=%llu positive_index_attempts=%llu positive_accepted=%llu",
            maximum,slowest,state.positive.members.size(),state.positive.entries.size(),
            state.positiveAttempts,state.positiveIndexAttempts,state.positiveAccepted);
        maximum=0;reportedAt=started;
    }
}
