        auto retireObservedBody = [&](const OW::c_entity& value) {
            OW::BlockEntityBody(value.address,processLoopTick);
            OW::TargetingDetail::InvalidateTargetBody(value.address,value.roster_key,value.actor_generation);
            {std::lock_guard<std::mutex> lock(OW::entityBodyValidityMutex);OW::entityBodyAuthorities.erase(value.address);}
            retiredActors[value.address]={value.match_id,value.LinkParent,false,OW::EntityBodyAbsenceRevision(value.address)};
            componentBaseCache.erase(value.address);dynamicEntityCache.erase(value.address);
        };
        // A trusted death is independent of position, bones and optional health pools.
        auto commitObservedDeath = [&](RosterEntry& record, OW::c_entity value,
                const OW::EntityRosterPolicy::Discovery& proof, OW::EntityBodyObservation observation, DWORD tick) {
            if(!value.address || !value.actor_generation || proof.match!=value.match_id || proof.hero!=value.HeroID)return false;
            value.connection_epoch=connectionEpoch;value.scene_generation=processSceneGeneration;
            value.last_discovered_tick_ms=proof.tick;
            value.death_known=true;value.is_dead=true;value.death_sample_tick_ms=tick;
            value.Alive=false;value.roster_state=OW::EntityRosterState::Dead;value.missing_since_tick_ms=0;
            value.body_observation_fence=observation.fence;value.body_absence_revision=observation.absence;
            value.body_authority_revision=OW::AdmitEntityBodyObservation(proof,value.actor_generation,
                processSceneGeneration,processRegistry,observation,false,true,entityAdmissionNow());
            if(!value.body_authority_revision)return false;
            const bool newlyDead=record.entity.roster_state!=OW::EntityRosterState::Dead;
            sanitizeStaleRosterEntity(value);
            OW::TargetingDetail::InvalidateTargetBody(value.address,value.roster_key,value.actor_generation);
            record.deathKnown=true;record.dead=true;record.deathUpdateTick=tick;record.alive=false;
            record.entity=std::move(value);record.seenThisCycle=true;record.lastHotReadTick=tick;
            record.visibilitySample = {};
            if(newlyDead) {
                ++lifecycleStats.entityRecordMarkDeadCount;
                record.respawnWatchStartedTick=tick;record.respawnFirstScanRequested=false;record.respawnLastScanRequestTick=0;
                record.cachedSkeletonValid=false;record.skeletonCache={};record.lastRawObservation={};
            }
            if(OW::TryCommitLocalEntity(record.entity,tick))
                localCycleSnapshot=record.entity;
            return true;
        };
