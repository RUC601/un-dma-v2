#include "EntityWorkers.hpp"
#include "Game/Overwatch.hpp"
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
    std::vector<OW::EntityRosterPolicy::Discovery> validatedProofs;
    size_t pairCursor=0, publishedPairCount=0, knownCursor=0;
    size_t validationCursor=0;
    uint64_t validationRevision=0;
    bool validating=false, bindingsComplete=false;
    uint64_t epoch=0, scene=0, completed=0, scanRequest=0;
    DWORD lastScan=0, lastKnownProbe=0;
    std::chrono::steady_clock::time_point started;
};

bool PublishDiscovery(DiscoveryState& state,
        std::vector<std::pair<uint64_t,uint64_t>> pairs,
        std::vector<OW::EntityRosterPolicy::Discovery> proofs,
        bool complete, bool bindingComplete=false,
        std::vector<std::pair<uint64_t,uint64_t>> bindings={}, bool scanPositive=false) {
    OW::RawEntityScanSnapshot sample{};
    sample.scene_generation=state.scene; sample.connection_epoch=state.epoch;
    sample.registry=state.registry; sample.raw_entities=std::move(pairs);
    sample.discoveries=std::move(proofs); sample.complete=complete;
    sample.binding_complete=bindingComplete; sample.binding_entities=std::move(bindings);
    for(const auto& pair:sample.raw_entities)sample.actual_members.push_back(pair.first);
    sample.scan_started_at=state.started; sample.scan_finished_at=std::chrono::steady_clock::now();
    sample.source=state.validating?1:2; sample.valid=true;
    std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
    if(!OW::RuntimeWorkersRunning() || !OW::ProcessConnection::IsConnected() ||
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
    return authoritative;
}
}

thread_local const char* discoveryOperation="idle";
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
    // Renew known bindings from the same topology used by the resumable full scan.
    if(!state.validating && !state.knownLinks.empty() && now-state.lastKnownProbe>=32 && state.topology.complete) {
        discoveryOperation="known_binding";
        state.lastKnownProbe=now;
        const auto parent=state.knownLinks[state.knownCursor++%state.knownLinks.size()];
        std::vector<OW::EntityRosterPolicy::Discovery> proofs;
        std::vector<std::pair<uint64_t,uint64_t>> bindings;
        bool complete=false;
        const auto frame=OW::SnapshotFrame();
        std::vector<OW::EntityRosterPolicy::Discovery> hotActors;
        for(const auto& entity:frame->entities)if(entity.HasUsableBody(now,scene))
            hotActors.push_back({entity.address,entity.LinkParent,entity.HeroID,entity.match_id,entity.anchor_sample_tick_ms});
        auto pairs=OW::ProbePriorityActor(parent,state.topology,true,&bindings,&complete,&proofs,nullptr,&hotActors);
        if(!pairs.empty())PublishDiscovery(state,std::move(pairs),std::move(proofs),false,complete,std::move(bindings));
        return;
    }
    if(state.pairCursor<state.pendingPairs.size()) {
        discoveryOperation="new_binding";
        const auto pair=state.pendingPairs[state.pairCursor++];
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
        state.proofs.clear();state.pendingPairs.clear();state.pairCursor=0;state.publishedPairCount=0;
        state.validationLinks.clear();state.validatedProofs.clear();state.validationCursor=0;
        state.validating=false;state.bindingsComplete=false;
        state.scan=std::make_unique<OW::EntityScanTask>(OW::BeginEntityScan());
        {std::lock_guard<std::mutex> lock(g_mutex);state.scanRequest=OW::entity_topology_rescan_request_count;}
        {std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);state.validationRevision=OW::observed_topology_revision;}
    }
    discoveryOperation="scan_slice";
    const auto sliceStarted=std::chrono::steady_clock::now();
    const auto previousPairCount=state.scan->Pairs().size();
    for(int step=0;step<32 && !state.scan->Done();++step) {
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
        for(const auto& pair:pairs)state.validationLinks.push_back(pair.second);
        for(const auto& [parent,header]:OW::entityScanHeaders) {
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
        Diagnostics::Info("[UNDMA-V2-DISCOVERY-TIME] max_ms=%.3f operation=%s",maximum,slowest);
        maximum=0;reportedAt=started;
    }
}
