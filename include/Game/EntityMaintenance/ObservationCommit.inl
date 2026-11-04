            if(hotAlreadyApplied) {
                cacheIt->second->entity=entity;
                if(localStats.selectedAddress==entity.address && OW::TryCommitLocalEntity(entity,entityAdmissionNow()))
                    localCycleSnapshot=entity;
            } else {
// Shared lexical step of entity_thread; requires its surrounding cycle state.
            if (ComponentParent && LinkParent && name != "Unknown" && hasRenderablePosition) {
                auto& admissionRecord=*cacheIt->second;
                auto rejectAdmission=[&](size_t reason,const char* detail,const char* liveDetail="none",uint64_t liveHero=0) {
                    const DWORD now=entityAdmissionNow();
                    if(admissionRecord.admissionRejectReason==detail && admissionRecord.admissionRejectTick &&
                        uint32_t(now-admissionRecord.admissionRejectTick)<1000)return;
                    admissionRecord.admissionRejectReason=detail;admissionRecord.admissionRejectTick=now;
                    const auto found=cycleDiscoveries.actors.find(entity.address);
                    const auto* expected=found==cycleDiscoveries.actors.end()?nullptr:&found->second;
                    Diagnostics::Warn("[V2-ADMISSION] reason=%zu detail=%s live_detail=%s live_hero=0x%llX actor=0x%llX link=0x%llX match=%u hero=0x%llX expected_link=0x%llX expected_match=%u expected_hero=0x%llX proof_age_ms=%u member=%d roster=0x%llX life=%llu hp=%.1f healthDue=%d healthValid=%d healthTick=%u cycleTick=%u fence=%llu absence=%llu",
                        reason,detail,liveDetail,liveHero,entity.address,entity.LinkParent,entity.match_id,entity.HeroID,
                        expected?expected->link:0,expected?expected->match:0,expected?expected->hero:0,
                        expected?uint32_t(now-expected->tick):0,latestCompleteActorMembers.contains(entity.address),entity.roster_key,entity.actor_generation,
                        entity.PlayerHealth,int(refreshHealth),int(componentCache.healthValid),componentCache.healthUpdateTick,
                        processLoopTick,entity.body_observation_fence,entity.body_absence_revision);
                };
                const auto* initialFailure=OW::EntityRosterPolicy::DiscoveryAdmissionFailure(cycleDiscoveries,
                    {entity.address,entity.LinkParent,entity.HeroID,entity.match_id,0},completeActorMembershipKnown,
                    std::find(cycleMemberPairs.begin(),cycleMemberPairs.end(),std::make_pair(ComponentParent,LinkParent))!=cycleMemberPairs.end(),
                    entityAdmissionNow());
                bool liveBindingConfirmed=false;
                if(initialFailure) {
                    const char* liveFailure="not_local";
                    uint64_t liveHero=0;
                    if(entity.AngleBase && entity.Alive && verifyCurrentLocalBinding(entity,admissionRecord.participantControllerUid,liveFailure,&liveHero)) {
                        const OW::EntityRosterPolicy::Discovery current{entity.address,entity.LinkParent,entity.HeroID,entity.match_id,entityAdmissionNow()};
                        liveBindingConfirmed=publishCurrentLocalBinding(current);
                        if(!liveBindingConfirmed)liveFailure="live_context_changed";
                        else {
                            const auto observation=OW::CaptureEntityBodyObservation(entity.address);
                            entity.body_observation_fence=observation.fence;entity.body_absence_revision=observation.absence;
                            Diagnostics::Info("[V2-LOCAL-BINDING] accepted actor=0x%llX link=0x%llX hero=0x%llX match=%u bypassed=%s",
                                entity.address,entity.LinkParent,entity.HeroID,entity.match_id,initialFailure);
                        }
                    }
                    if(!liveBindingConfirmed){rejectAdmission(0,initialFailure,liveFailure,liveHero);continue;}
                }
                const auto* discovery=cycleDiscoveries.Find(ComponentParent,LinkParent,entityAdmissionNow());
                if(!discovery){rejectAdmission(0,"discovery_expired");continue;}
                entity.scene_generation=processSceneGeneration;
                entity.last_discovered_tick_ms=discovery->tick;
                entity.body_valid=entity.Alive;
                entity.roster_state = entity.Alive
                    ? OW::EntityRosterState::Fresh
                    : OW::EntityRosterState::Dead;
                entity.last_seen_tick_ms = processLoopTick;
                entity.missing_since_tick_ms = 0;
                if (entity.roster_state == OW::EntityRosterState::Dead)
                    sanitizeStaleRosterEntity(entity);
                if (entity.participant_key != 0) {
                    const auto legacyActorIt = std::find_if(
                        entityRoster.begin(),
                        entityRoster.end(),
                        [&](const auto& rosterPair) {
                            return rosterPair.first != entity.roster_key &&
                                (rosterPair.second->entity.address == ComponentParent ||
                                 (!rosterPair.second->entity.participant_key &&
                                  rosterPair.second->entity.LinkParent==LinkParent &&
                                  rosterPair.second->participantControllerUid==cacheIt->second->participantControllerUid));
                        });
                    if (legacyActorIt != entityRoster.end()) {
                        if (entityRoster.find(entity.roster_key) == entityRoster.end()) {
                            auto promotedEntry = legacyActorIt->second;
                            entityRoster.erase(legacyActorIt);
                            entityRoster.emplace(
                                entity.roster_key,
                                std::move(promotedEntry));
                        } else {
                            if(legacyActorIt->second->entity.address!=entity.address)
                                retireObservedBody(legacyActorIt->second->entity);
                            entityRoster.erase(legacyActorIt);
                        }
                    }
                }
                auto existingRosterIt = entityRoster.find(entity.roster_key);
                if (entity.participant_key != 0 &&
                    existingRosterIt != entityRoster.end() &&
                    existingRosterIt->second->entity.address != entity.address) {
                    const bool existingFresh =
                        existingRosterIt->second->entity.roster_state ==
                            OW::EntityRosterState::Fresh &&
                        existingRosterIt->second->entity.Alive;
                    const bool incomingFresh =
                        entity.roster_state == OW::EntityRosterState::Fresh &&
                        entity.Alive;
                    if(existingRosterIt->second->entity.participant_key &&
                       existingRosterIt->second->participantLabel!=cacheIt->second->participantLabel) {
                        entity.participant_key=0;
                        entity.roster_key=makeRosterKey(entity.match_id,LinkParent,ComponentParent);
                        existingRosterIt=entityRoster.find(entity.roster_key);
                    } else {
                    const bool sameController=existingRosterIt->second->entity.LinkParent==LinkParent &&
                        existingRosterIt->second->participantControllerUid==cacheIt->second->participantControllerUid;
                    uint32_t pairedUid=0;
                    const bool incomingPaired=sameController && (liveBindingConfirmed || (entity.LinkBase &&
                        OW::ResolveCnLinkTargetMapBase(entity.LinkBase,offset::Link_UniqueId,&pairedUid)==entity.address &&
                        pairedUid==entity.match_id));
                    switch (OW::EntityRosterPolicy::ResolveSameParticipantObservation(
                        existingRosterIt->second->seenThisCycle || existingFresh,
                        false,
                        existingFresh,
                        incomingFresh,
                        completeActorMembershipKnown,
                        latestCompleteActorMembers.contains(existingRosterIt->second->entity.address),
                        latestCompleteActorMembers.contains(entity.address),sameController,incomingPaired)) {
                    case OW::EntityRosterPolicy::SameParticipantObservationAction::KeepExisting:
                        rejectAdmission(1,!incomingFresh?"incoming_not_alive":sameController?"controller_pair_unconfirmed":"participant_ambiguous");
                        continue;
                    case OW::EntityRosterPolicy::SameParticipantObservationAction::ReplaceExisting:
                    default:
                        break;
                    }
                    }
                }
                const auto retiredIt=retiredActors.find(entity.address);
                if(retiredIt!=retiredActors.end()) {
                    if(!liveBindingConfirmed && !OW::EntityRosterPolicy::CanReturnRetiredActor(
                        retiredIt->second.matchId==entity.match_id && retiredIt->second.link==entity.LinkParent,
                        retiredIt->second.absent || OW::EntityBodyAbsenceRevision(entity.address)>retiredIt->second.absenceRevision,
                        latestCompleteActorMembers.contains(entity.address))){rejectAdmission(2,"retired_body_unconfirmed");continue;}
                    retiredActors.erase(retiredIt);
                }
                entity.connection_epoch=connectionEpoch;
                entity.actual_scan_generation=rawScanSeenComponents.contains(entity.address) ? OW::last_consumed_raw_scan_generation : 0;
                entity.actual_scan_source=rawScanSeenComponents.contains(entity.address) ? consumedScanSource : 0;
                const OW::c_entity* old=existingRosterIt==entityRoster.end()?nullptr:&existingRosterIt->second->entity;
                const bool sameLifetime=old && old->body_absence_revision==entity.body_absence_revision &&
                    OW::PresentationTiming::SameLifetime(old->PresentationIdentity(),entity.PresentationIdentity());
                if(old && !rawScanSeenComponents.contains(entity.address)) {
                    entity.actual_scan_generation=old->actual_scan_generation;entity.actual_scan_source=old->actual_scan_source;
                }
                entity.actor_generation=sameLifetime && old->actor_generation ? old->actor_generation : ++actorGenerationSequence;
                const char* authorityFailure=nullptr;
                entity.body_authority_revision=OW::AdmitEntityBodyObservation(*discovery,entity.actor_generation,
                    processSceneGeneration,processRegistry,{entity.body_observation_fence,entity.body_absence_revision},
                    entity.Alive,(refreshHealth || hotReads.HealthRead()) && componentCache.healthValid &&
                        componentCache.healthUpdateTick==processLoopTick,entityAdmissionNow(),&authorityFailure);
                if(!entity.body_authority_revision){rejectAdmission(3,authorityFailure);continue;}
                admissionRecord.admissionRejectReason={};
                if(!entity.Alive)OW::TargetingDetail::InvalidateTargetBody(entity.address,entity.roster_key,entity.actor_generation);
                if(old && !sameLifetime) {
                    const auto oldAddress=old->address,oldGeneration=old->actor_generation;
                    if(oldAddress!=entity.address) {
                        retireObservedBody(*old);
                    }
                    cacheIt->second->cachedSkeletonValid=false;cacheIt->second->skeletonCache={};
                    entity.has_previous_render_sample=false;
                    RuntimeTrace::Emit(RuntimeTrace::ActorReplace,oldAddress,entity.address,oldGeneration,entity.actor_generation);
                }
                // A match-key change must not leave an obsolete seat for the same body in this frame.
                for(auto it=entityRoster.begin();it!=entityRoster.end();) {
                    const auto stale=it->second->entity;
                    const bool sameLegacyController=entity.participant_key && !stale.participant_key &&
                        stale.LinkParent==LinkParent && it->second->participantControllerUid==cacheIt->second->participantControllerUid;
                    if(it->first!=entity.roster_key && (stale.address==entity.address || sameLegacyController)) {
                        if(stale.address!=entity.address) {
                            retireObservedBody(stale);
                        }
                        it=entityRoster.erase(it);
                    } else ++it;
                }
                if (entity.participant_key != 0) {
                    ScopedPipelinePhaseTimer recordTimer(
                        pipelineStats.phase.entityCacheRecordUpdateMs);

                }
                const bool beginRespawnWatch =
                    entity.roster_state == OW::EntityRosterState::Dead &&
                    (existingRosterIt == entityRoster.end() ||
                     existingRosterIt->second->entity.roster_state !=
                         OW::EntityRosterState::Dead);
                if (existingRosterIt == entityRoster.end()) {
                    ++lifecycleStats.entityRecordCreatedCount;
                    if (entity.roster_state == OW::EntityRosterState::Dead)
                        ++lifecycleStats.entityRecordMarkDeadCount;
                } else {
                    const OW::c_entity& previousRosterEntity = existingRosterIt->second->entity;
                    if (previousRosterEntity.address != 0 &&
                        previousRosterEntity.address != entity.address) {
                        ++lifecycleStats.entityRecordUpdatedActorCount;
                    }
                    if (previousRosterEntity.LinkParent != 0 &&
                        previousRosterEntity.LinkParent != entity.LinkParent) {
                        ++lifecycleStats.entityRecordLinkChangedCount;
                        if (previousRosterEntity.address == entity.address)
                            ++lifecycleStats.entityRecordLinkChangedSameComponentCount;
                        else
                            ++lifecycleStats.entityRecordLinkChangedComponentChangedCount;
                        if (previousRosterEntity.HeroID == 0 || entity.HeroID == 0)
                            ++lifecycleStats.entityRecordLinkChangedHeroUnknownCount;
                        else if (previousRosterEntity.HeroID == entity.HeroID)
                            ++lifecycleStats.entityRecordLinkChangedSameHeroCount;
                        else
                            ++lifecycleStats.entityRecordLinkChangedHeroChangedCount;
                        recordRosterLinkChangeKeyKind(entity.roster_key, lifecycleStats);
                    }
                    if (entity.roster_state == OW::EntityRosterState::Dead &&
                        previousRosterEntity.roster_state != OW::EntityRosterState::Dead) {
                        ++lifecycleStats.entityRecordMarkDeadCount;
                    }
                }
                entityRoster[entity.roster_key]=cacheIt->second;
                RosterEntry& rosterEntry = *cacheIt->second;
                if (entity.roster_state == OW::EntityRosterState::Fresh) {
                    rosterEntry.respawnWatchStartedTick = 0;
                    rosterEntry.respawnLastScanRequestTick = 0;
                    rosterEntry.respawnFirstScanRequested = false;
                } else if (beginRespawnWatch &&
                           rosterEntry.respawnWatchStartedTick == 0) {
                    rosterEntry.respawnWatchStartedTick = processLoopTick;
                    rosterEntry.respawnLastScanRequestTick = 0;
                    rosterEntry.respawnFirstScanRequested = false;
                }
                if (rosterEntry.lastScanSeenTick == 0 && entity.last_scan_seen_tick_ms != 0)
                    rosterEntry.lastScanSeenTick = entity.last_scan_seen_tick_ms;
                if (rosterEntry.lastHotReadTick == 0 && entity.last_hot_read_tick_ms != 0)
                    rosterEntry.lastHotReadTick = entity.last_hot_read_tick_ms;
                if (scanSeenThisCycle) {
                    rosterEntry.lastScanSeenTick = discovery->tick;
                    rosterEntry.consecutiveScanMissCount = 0;
                    rosterEntry.scanSeenThisCycle = true;
                } else if (scanMissHotRead) {
                    rosterEntry.hotReadRetainedThisCycle = true;
                    scanMissHotReadSucceededComponents.insert(ComponentParent);
                    ++lifecycleStats.entityRecordScanMissHotReadSuccessCount;
                }
                rosterEntry.lastHotReadTick = processLoopTick;
                entity.last_scan_seen_tick_ms = rosterEntry.lastScanSeenTick;
                entity.last_hot_read_tick_ms = rosterEntry.lastHotReadTick;
                entity.scan_seen_gap_ms =
                    rosterEntry.lastScanSeenTick != 0 ? processLoopTick - rosterEntry.lastScanSeenTick : 0;
                entity.hot_read_gap_ms =
                    rosterEntry.lastHotReadTick != 0 ? processLoopTick - rosterEntry.lastHotReadTick : 0;
                entity.consecutive_scan_miss_count = rosterEntry.consecutiveScanMissCount;

                if(!sameLifetime) {
                    resetRenderHistory(entity,entity.anchor_sample_tick_ms);
                    rosterEntry.lastRawObservation=entity;
                } else if (entity.roster_state == OW::EntityRosterState::Fresh &&
                    shouldSkipTeleportObservation(entity, rosterEntry, entity.anchor_sample_tick_ms)) {
                    if (rosterEntry.skippedJumpObservations < kObservationMaxSkippedJumpFrames) {
                        OW::c_entity held = rosterEntry.entity;
                        held.missing_since_tick_ms = 0;
                        held.roster_state = OW::EntityRosterState::Fresh;
                        held.last_scan_seen_tick_ms = rosterEntry.lastScanSeenTick;
                        held.last_hot_read_tick_ms = rosterEntry.lastHotReadTick;
                        held.scan_seen_gap_ms = entity.scan_seen_gap_ms;
                        held.hot_read_gap_ms = entity.hot_read_gap_ms;
                        held.consecutive_scan_miss_count = rosterEntry.consecutiveScanMissCount;
                        // Preserve positions and their original observation clocks.
                        rosterEntry.entity = held;
                        rosterEntry.seenThisCycle = true;
                        rosterEntry.skippedJumpObservations++;
                        tmp_entities.push_back(held);
                        if (detailedProcessLog) {
                            Diagnostics::Info("[PIPELINE] Stage 4 observation_gate skip_jump idx=%zu roster=0x%llX skipped=%d.",
                                i,
                                static_cast<unsigned long long>(entity.roster_key),
                                rosterEntry.skippedJumpObservations);
                        }
                        continue;
                    }

                    resetRenderHistory(entity, entity.anchor_sample_tick_ms);
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 observation_gate accept_jump_reset idx=%zu roster=0x%llX skipped=%d.",
                            i,
                            static_cast<unsigned long long>(entity.roster_key),
                            rosterEntry.skippedJumpObservations);
                    }
                } else {
                    // Compare successive *observations*, not the last filtered
                    // output, so a persistent valid pose is accepted next time.
                    const OW::c_entity rawObservation = entity;
                    const size_t suppressedPoints = stabilizeObservationPointOutliers(
                        entity,
                        rosterEntry.entity,
                        rosterEntry.lastRawObservation.address ? rosterEntry.lastRawObservation : rosterEntry.entity,
                        entity.anchor_sample_tick_ms);
                    rosterEntry.lastRawObservation = rawObservation;
                    if (suppressedPoints > 0 && detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 observation_gate suppress_points idx=%zu roster=0x%llX count=%zu.",
                            i,
                            static_cast<unsigned long long>(entity.roster_key),
                            suppressedPoints);
                    }
                    attachPreviousRenderSample(entity);
                }

                entity.last_seen_tick_ms = entity.anchor_sample_tick_ms;
                rosterEntry.entity = entity;
                if(localStats.selectedAddress==entity.address) {
                    if(OW::TryCommitLocalEntity(entity,entityAdmissionNow()))localCycleSnapshot=entity;
                }
                rosterEntry.seenThisCycle = true;
                rosterEntry.skippedJumpObservations = 0;
                tmp_entities.push_back(entity);
            } else {
                if (name == "Unknown") {
                    processStats.nameUnknown++;
                    recordNameUnknownSample(entity, ComponentParent, LinkParent, name);
                }
                Diagnostics::RecordInvalidEntity();
            }

            }
