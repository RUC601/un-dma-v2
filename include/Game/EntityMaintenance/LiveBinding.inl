// Owner-thread local handoff: current controller evidence does not wait for a full roster.
        auto verifyCurrentLocalBinding = [&](const OW::c_entity& candidate, uint32_t controllerUid,
                const char*& failure,uint64_t* observedHero=nullptr) {
            const auto fail=[&](const char* reason){failure=reason;return false;};
            failure=nullptr;
            if(observedHero)*observedHero=0;
            if(!offset::UsesProtectedEntityLayout() || !candidate.AngleBase)return fail("local_controller_missing");
            OW::EntityHeaderSnapshot actorHeader{},controllerHeader{};
            if(!actorHeader.Read(candidate.address) || !controllerHeader.Read(candidate.LinkParent))
                return fail("live_header_read_failed");
            uint32_t actorMatch=0,parentMatch=0;
            if(!actorHeader.ReadParentOffset(offset::Entity_MatchId,actorMatch) ||
               !controllerHeader.ReadParentOffset(offset::Entity_MatchId,parentMatch))return fail("live_identity_read_failed");
            if(actorMatch!=candidate.match_id || !actorMatch)return fail("live_actor_changed");
            if(parentMatch!=controllerUid || !parentMatch)return fail("live_controller_changed");
            const auto link=OW::DecryptComponent(candidate.LinkParent,offset::Active().typeLink,&controllerHeader,true);
            const auto angle=OW::DecryptComponent(candidate.LinkParent,offset::Active().typePlayerController,&controllerHeader,true);
            if(!link || link!=candidate.LinkBase || !angle || angle!=candidate.AngleBase)
                return fail("live_components_changed");
            std::array<uint32_t,2> ids{};
            if(!SDK->read_range(link+offset::Link_TargetId,ids.data(),sizeof(ids)))return fail("live_pair_read_failed");
            if(ids[1]!=actorMatch)return fail("live_pair_mismatch");
            const auto mapped=OW::ResolveCnLinkTargetMapBase(link,offset::Link_UniqueId);
            if(mapped && mapped!=candidate.address)return fail("live_pair_other_context");
            if(!mapped) {
                std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
                const auto& topology=OW::observed_entity_topology;
                if(std::count_if(topology.member_ids.begin(),topology.member_ids.end(),
                    [&](const auto& member){return member.second==actorMatch;})>1)
                    return fail("live_pair_ambiguous");
            }
            auto hero=OW::DecryptComponent(candidate.LinkParent,offset::Active().typeHeroId,&controllerHeader,true);
            if(!hero)hero=OW::DecryptComponent(candidate.address,offset::Active().typeHeroId,&actorHeader,true);
            uint64_t heroId=0;
            if(!hero || hero!=candidate.HeroBase || !SDK->read_range(hero+0xD0,&heroId,sizeof(heroId)))
                return fail("live_hero_read_failed");
            if(observedHero)*observedHero=heroId;
            if(!OW::GameData::IsKnownHeroId(heroId))return fail("live_hero_unknown");
            if(heroId!=candidate.HeroID)return fail("live_hero_changed");
            std::array<uint32_t,2> finalIds{};
            uint32_t finalActor=0,finalParent=0;
            if(!SDK->read_range(link+offset::Link_TargetId,finalIds.data(),sizeof(finalIds)) ||
               !SDK->read_range(candidate.address+offset::Entity_MatchId,&finalActor,sizeof(finalActor)) ||
               !SDK->read_range(candidate.LinkParent+offset::Entity_MatchId,&finalParent,sizeof(finalParent)))
                return fail("live_recheck_read_failed");
            if(finalIds!=ids || finalActor!=actorMatch || finalParent!=parentMatch)return fail("live_binding_changed");
            return true;
        };
        auto publishCurrentLocalBinding = [&](const OW::EntityRosterPolicy::Discovery& proof) {
            std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
            if(!OW::RuntimeWorkersRunning() || !OW::ProcessConnection::IsConnected() ||
               OW::ProcessConnection::ConnectionEpoch()!=connectionEpoch ||
               OW::EntitySceneGeneration()!=processSceneGeneration || !OW::AcceptEntityRegistry(processRegistry))return false;
            OW::RawEntityScanSnapshot sample{};
            sample.connection_epoch=connectionEpoch;sample.scene_generation=processSceneGeneration;sample.registry=processRegistry;
            sample.raw_entities={{proof.address,proof.link}};sample.actual_members={proof.address};sample.discoveries={proof};
            sample.binding_entities=sample.raw_entities;sample.binding_complete=true;sample.source=2;sample.valid=true;
            auto previous=OW::latest_raw_scan_snapshot.membership_entities;
            OW::MergeRecentRawDiscovery(sample);
            auto current=sample.membership_entities;
            std::sort(previous.begin(),previous.end());std::sort(current.begin(),current.end());
            if(previous!=current)++OW::observed_topology_revision;
            sample.generation=++OW::raw_scan_publication_sequence;
            OW::latest_raw_scan_snapshot=std::move(sample);
            cycleDiscoveries=OW::entity_discoveries;
            cycleMemberPairs=OW::latest_raw_scan_snapshot.membership_entities;
            completeActorMembershipKnown=OW::latest_raw_scan_snapshot.membership_known;
            latestCompleteActorMembers.clear();
            for(const auto& pair:cycleMemberPairs)latestCompleteActorMembers.insert(pair.first);
            return true;
        };
