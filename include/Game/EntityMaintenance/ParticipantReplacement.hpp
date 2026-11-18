#pragma once
#include "Game/Overwatch.hpp"
#include "Game/ParticipantIdentity.hpp"

namespace OW::EntityMaintenance {
struct ParticipantReplacementVerifier {
    using Context=EntityRosterPolicy::ParticipantBindingContext;
    using Proof=EntityRosterPolicy::VerifiedDeadParticipantReplacement;

    static bool Matches(const Context& saved,const c_entity& body,uint32_t controllerUid,
            std::string_view label,const EntityRegistry::Snapshot& registry,uint64_t scene) {
        return saved.address==body.address && saved.link==body.LinkParent && saved.linkBase==body.LinkBase &&
            saved.match==body.match_id && saved.hero==body.HeroID && saved.generation==body.actor_generation &&
            saved.epoch==registry.epoch && saved.scene==scene && saved.registry==registry.generation &&
            saved.controllerUid==controllerUid && saved.label==label;
    }

    template<class Record,class Clock>
    static void Maintain(Record& record,const c_entity& body,const EntityRegistry::Snapshot& registry,
            uint64_t scene,Clock&& clock) {
        if(offset::Active().componentTransform!=offset::ComponentTransformMode::Cn155284 ||
            !body.actor_generation || !record.participantKey || !record.participantIdentityValid)return;
        const bool same=Matches(record.participantBindingContext,body,record.participantControllerUid,
            record.participantLabel,registry,scene);
        const auto now=clock();
        if(same && (record.participantBindingContext.context || (record.participantBindingContextAttemptTick &&
            uint32_t(now-record.participantBindingContextAttemptTick)<250)))return;
        // Identity can arrive after the hot body was applied. Only a new capture
        // needs a body copy with the newly obtained participant key and stat.
        auto identified=body;identified.participant_key=record.participantKey;identified.statcombase=record.stat;
        if(!AuthorityCurrent(identified,registry,scene,false))return;
        record.participantBindingContextAttemptTick=now;
        record.participantBindingContext=Capture(identified,record.participantControllerUid,record.participantLabel,registry,scene,clock);
        if(!AuthorityCurrent(identified,registry,scene,false))record.participantBindingContext.context=0;
    }

    template<class Clock>
    static Proof Verify(const c_entity& previous,uint32_t previousControllerUid,std::string_view previousLabel,
            const Context& saved,const c_entity& incoming,uint32_t incomingControllerUid,std::string_view incomingLabel,
            const EntityRegistry::Snapshot& registry,uint64_t scene,Clock&& clock,const char** failure=nullptr) {
        if(failure)*failure=nullptr;
        const auto fail=[&](const char* reason){if(failure)*failure=reason;return Proof{};};
        if(previous.address==incoming.address || previous.roster_state!=EntityRosterState::Dead || previous.Alive ||
            !previous.death_known || !previous.is_dead || !previous.death_sample_tick_ms || !previous.actor_generation)
            return fail("previous_death_unconfirmed");
        if(!incoming.Alive || !incoming.HasFreshLifeState(clock()) || !incoming.participant_key ||
            previous.participant_key!=incoming.participant_key || previousLabel!=incomingLabel ||
            !EntityRosterPolicy::ReliableParticipantId(incomingLabel) ||
            EntityRosterPolicy::ParticipantRosterKey(incomingLabel)!=incoming.participant_key)
            return fail("replacement_participant_unconfirmed");
        if(!saved.context || !Matches(saved,previous,previousControllerUid,previousLabel,registry,scene))
            return fail("previous_context_unconfirmed");
        if(!AuthorityCurrent(previous,registry,scene,true))return fail("previous_death_superseded");
        CnLinkTargetMapIdentity map;
        if(!ReadCurrent(incoming,incomingControllerUid,incomingLabel,registry,scene,true,clock,map,failure))return {};
        // The map context is the current controller parent. A respawn or
        // component relocation can legitimately give the successor a new
        // controller, so it must not equal the predecessor's saved context.
        // ReadCurrent already proves that the successor's map resolves to its
        // own controller and actor pair.
        if(map.context!=saved.context && map.context!=incoming.LinkParent)
            return fail("replacement_context_changed");
        if(!AuthorityCurrent(previous,registry,scene,true))return fail("previous_death_superseded");
        return Proof{true};
    }

private:
    static bool Current(const EntityRegistry::Snapshot& registry,uint64_t scene) {
        return offset::Active().componentTransform==offset::ComponentTransformMode::Cn155284 &&
            RuntimeWorkersRunning() && ProcessConnection::IsConnected() &&
            ProcessConnection::ConnectionEpoch()==registry.epoch && EntitySceneGeneration()==scene && AcceptEntityRegistry(registry);
    }
    static bool AuthorityCurrent(const c_entity& previous,const EntityRegistry::Snapshot& registry,uint64_t scene,bool requireDead) {
        std::lock_guard<std::mutex> registryLock(entityRegistryMonitor.mutex);
        std::lock_guard<std::mutex> bodyLock(entityBodyValidityMutex);
        const EntityRosterPolicy::Discovery identity{previous.address,previous.LinkParent,previous.HeroID,previous.match_id,0};
        if(!RuntimeWorkersRunning() || !ProcessConnection::IsConnected() ||
            ProcessConnection::ConnectionEpoch()!=registry.epoch || EntitySceneGeneration()!=scene ||
            !entityRegistryMonitor.state.Accept(registry) || previous.connection_epoch!=registry.epoch ||
            previous.scene_generation!=scene || !EntityBodyAuthorityMatchesLocked(identity,previous.actor_generation,
                previous.body_authority_revision,registry.epoch,scene,previous.body_absence_revision))return false;
        const auto& authority=entityBodyAuthorities.at(previous.address);
        return authority.registry==registry.generation && (!requireDead || !authority.alive);
    }
    template<class Clock>
    static Context Capture(const c_entity& body,uint32_t controllerUid,std::string_view label,
            const EntityRegistry::Snapshot& registry,uint64_t scene,Clock&& clock) {
        Context saved{body.address,body.LinkParent,body.LinkBase,body.HeroID,body.actor_generation,
            registry.epoch,scene,registry.generation,0,0,0,body.match_id,controllerUid,std::string(label)};
        CnLinkTargetMapIdentity map;
        if(ReadCurrent(body,controllerUid,label,registry,scene,false,clock,map,nullptr)) {
            saved.context=map.context;saved.owner=map.owner;saved.buckets=map.buckets;
        }
        return saved;
    }
    template<class Clock>
    static bool ReadCurrent(const c_entity& body,uint32_t controllerUid,std::string_view label,
            const EntityRegistry::Snapshot& registry,uint64_t scene,bool requireHealth,Clock&& clock,
            CnLinkTargetMapIdentity& map,const char** failure) {
        const auto fail=[&](const char* reason){if(failure)*failure=reason;return false;};
        const auto started=clock();
        if(!Current(registry,scene) || !body.address || !body.LinkParent || !body.match_id || !controllerUid ||
            !body.participant_key || !EntityRosterPolicy::ReliableParticipantId(label) ||
            EntityRosterPolicy::ParticipantRosterKey(label)!=body.participant_key)return fail("replacement_context_changed");
        // Re-resolve current components: cached participant strings and HP cannot prove a successor.
        const auto readHeaders=[&](EntityHeaderSnapshot& actor,EntityHeaderSnapshot& controller) {
            uint32_t actorId=0,parentId=0;
            if(!actor.Read(body.address) || !controller.Read(body.LinkParent) ||
                !actor.ReadParentOffset(offset::Entity_MatchId,actorId) || actorId!=body.match_id ||
                !controller.ReadParentOffset(offset::Entity_MatchId,parentId) || parentId!=controllerUid)return false;
            const auto link=DecryptComponent(body.LinkParent,offset::Active().typeLink,&controller,true);
            const auto stat=DecryptComponent(body.LinkParent,TYPE_STAT,&controller,true);
            const auto health=DecryptComponent(body.address,offset::Active().typeHealth,&actor,true);
            auto hero=DecryptComponent(body.LinkParent,offset::Active().typeHeroId,&controller,true);
            if(!hero)hero=DecryptComponent(body.address,offset::Active().typeHeroId,&actor,true);
            return link && link==body.LinkBase && stat && stat==body.statcombase &&
                health && health==body.HealthBase && hero && hero==body.HeroBase;
        };
        EntityHeaderSnapshot actor,controller;
        if(!readHeaders(actor,controller))return fail("replacement_header_changed");
        std::array<uint32_t,2> ids{};
        if(!SDK->read_range(body.LinkBase+offset::Link_TargetId,ids.data(),sizeof(ids)))return fail("replacement_pair_read_failed");
        const auto idOffset=ids[1]==body.match_id?offset::Link_UniqueId:offset::Link_TargetId;
        if(ids[idOffset==offset::Link_UniqueId?1:0]!=body.match_id)return fail("replacement_pair_mismatch");
        uint32_t mappedId=0;
        if(ResolveCnLinkTargetMapBase(body.LinkBase,idOffset,&mappedId,&map)!=body.address || mappedId!=body.match_id)
            return fail("replacement_map_unconfirmed");
        std::string liveLabel;
        if(!ReadParticipantLabel(body.statcombase,[](uint64_t at,void* out,size_t size){return SDK->read_range(at,out,size);},liveLabel) ||
            liveLabel!=label)return fail("replacement_live_participant_changed");
        uint64_t hero=0;
        if(!SDK->read_range(body.HeroBase+0xD0,&hero,sizeof(hero)) || hero!=body.HeroID || !GameData::IsKnownHeroId(hero))
            return fail("replacement_hero_changed");
        if(requireHealth) {
            health_compo_t raw{};HealthPayloadSummary health;
            if(!SDK->read_range(body.HealthBase,&raw,sizeof(raw)) || !TryParseHealthPayload(raw,health) ||
                health.isDead || health.total<=0)return fail("replacement_health_unconfirmed");
        }
        EntityHeaderSnapshot finalActor,finalController;
        std::array<uint64_t,6> actorLayout{},controllerLayout{},finalActorLayout{},finalControllerLayout{};
        if(!readHeaders(finalActor,finalController) || !actor.ReadComponentLayout(actorLayout) ||
            !controller.ReadComponentLayout(controllerLayout) || !finalActor.ReadComponentLayout(finalActorLayout) ||
            !finalController.ReadComponentLayout(finalControllerLayout) || actorLayout!=finalActorLayout || controllerLayout!=finalControllerLayout)
            return fail("replacement_header_changed");
        CnLinkTargetMapIdentity finalMap;
        std::array<uint32_t,2> finalIds{};
        if(!SDK->read_range(body.LinkBase+offset::Link_TargetId,finalIds.data(),sizeof(finalIds)) || finalIds!=ids ||
            ResolveCnLinkTargetMapBase(body.LinkBase,idOffset,&mappedId,&finalMap)!=body.address ||
            mappedId!=body.match_id || finalMap!=map)return fail("replacement_map_changed");
        const auto finished=clock();
        if(!Current(registry,scene) || uint32_t(finished-started)>100 ||
            (requireHealth && !body.HasFreshLifeState(finished)))return fail("replacement_sample_expired");
        return true;
    }
};
}
