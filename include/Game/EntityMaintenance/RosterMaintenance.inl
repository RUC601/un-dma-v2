// Shared lexical step of entity_thread; requires its surrounding cycle state.
#include "Game/EntityMaintenance/SanitizeRoster.inl"

    constexpr float kObservationTeleportBaseMeters = 8.0f;
    constexpr float kObservationMaxSpeedMetersPerSecond = 120.0f;
    constexpr DWORD kObservationPreviousMaxAgeMs = 250;
    constexpr int kObservationMaxSkippedJumpFrames = 2;
    constexpr float kObservationPointJumpBaseMeters = 3.0f;
    constexpr float kObservationPointJumpAnchorSlackMeters = 2.0f;

    auto isFiniteVector = [](const Vector3& value) {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    };

    auto isUsableVector = [&](const Vector3& value) {
        return isFiniteVector(value) && value != Vector3(0, 0, 0);
    };

    auto observationAnchor = [&](const OW::c_entity& entity) {
        if (isUsableVector(entity.pos))
            return entity.pos;
        if (isUsableVector(entity.chest_pos))
            return entity.chest_pos;
        if (isUsableVector(entity.neck_pos))
            return entity.neck_pos;
        return entity.head_pos;
    };

    auto hasRenderableAnchor = [&](const OW::c_entity& entity) {
        if (isUsableVector(entity.pos) ||
            isUsableVector(entity.head_pos) ||
            isUsableVector(entity.neck_pos) ||
            isUsableVector(entity.chest_pos) ||
            (entity.cached_bot_chest_bone_valid && isUsableVector(entity.cached_bot_chest_bone))) {
            return true;
        }

        for (size_t boneIndex = 0; boneIndex < entity.skeleton_bones.size(); ++boneIndex) {
            if (entity.skeleton_bone_valid[boneIndex] &&
                isUsableVector(entity.skeleton_bones[boneIndex])) {
                return true;
            }
        }
        return false;
    };

    auto observationSampleSeconds = [](DWORD now, DWORD previousTick) {
        if (previousTick == 0 || now <= previousTick)
            return OW::kEntityProcessIntervalMs / 1000.0f;
        return std::clamp((now - previousTick) / 1000.0f, 0.004f, 0.250f);
    };

    auto maxObservationMovement = [&](DWORD now, const OW::c_entity& previous) {
        const float sampleSeconds = observationSampleSeconds(now, previous.last_seen_tick_ms);
        return (std::max)(
            kObservationTeleportBaseMeters,
            kObservationMaxSpeedMetersPerSecond * sampleSeconds);
    };

    auto resetRenderHistory = [](OW::c_entity& entity, DWORD now) {
        entity.render_sample_tick_ms = now;
        entity.previous_render_sample_tick_ms = now;
        entity.position_sample_tick_ms = now;
        entity.has_previous_render_sample = false;
        entity.previous_head_pos = entity.head_pos;
        entity.previous_velocity = entity.velocity;
        entity.previous_pos = entity.pos;
        entity.previous_neck_pos = entity.neck_pos;
        entity.previous_chest_pos = entity.chest_pos;
        entity.previous_skeleton_bones = entity.skeleton_bones;
        entity.previous_skeleton_bone_valid = entity.skeleton_bone_valid;
        entity.previous_cached_bot_chest_bone = entity.cached_bot_chest_bone;
        entity.previous_cached_bot_chest_bone_valid = entity.cached_bot_chest_bone_valid;
    };

    auto shouldSkipTeleportObservation = [&](const OW::c_entity& current,
                                             const RosterEntry& rosterEntry,
                                             DWORD now) {
        const OW::c_entity& previous = rosterEntry.entity;
        if (!previous.address || !previous.Alive ||
            previous.roster_state != OW::EntityRosterState::Fresh) {
            return false;
        }
        if (previous.HeroID != 0 && current.HeroID != 0 && previous.HeroID != current.HeroID)
            return false;
        if (previous.last_seen_tick_ms == 0 || now - previous.last_seen_tick_ms > kObservationPreviousMaxAgeMs)
            return false;

        const Vector3 previousAnchor = observationAnchor(previous);
        const Vector3 currentAnchor = observationAnchor(current);
        if (!isUsableVector(previousAnchor) || !isUsableVector(currentAnchor))
            return false;

        const float movement = previousAnchor.DistTo(currentAnchor);
        return std::isfinite(movement) && movement > maxObservationMovement(now, previous);
    };

    auto stabilizeObservationPointOutliers = [&](OW::c_entity& current,
                                                 const OW::c_entity& previous,
                                                 const OW::c_entity& previousObservation,
                                                 DWORD now) {
        if (!previous.address || previous.last_seen_tick_ms == 0 ||
            now - previous.last_seen_tick_ms > kObservationPreviousMaxAgeMs) {
            return static_cast<size_t>(0);
        }

        const Vector3 previousAnchor = observationAnchor(previous);
        const Vector3 currentAnchor = observationAnchor(current);
        if (!isUsableVector(previousAnchor) || !isUsableVector(currentAnchor))
            return static_cast<size_t>(0);

        const float anchorMovement = previousAnchor.DistTo(currentAnchor);
        if (!std::isfinite(anchorMovement) || anchorMovement > maxObservationMovement(now, previous))
            return static_cast<size_t>(0);

        const float maxPointMovement = (std::max)(
            kObservationPointJumpBaseMeters,
            anchorMovement + kObservationPointJumpAnchorSlackMeters);
        size_t suppressed = 0;

        auto suppressPoint = [&](Vector3& point, const Vector3& previousPoint, const Vector3& observedPoint) {
            if (!isUsableVector(point) || !isUsableVector(previousPoint))
                return;
            const float movement = previousPoint.DistTo(point);
            const bool repeatedObservation = isUsableVector(observedPoint) &&
                point.DistTo(observedPoint) <= maxPointMovement;
            if (std::isfinite(movement) && movement > maxPointMovement && !repeatedObservation) {
                point = previousPoint;
                ++suppressed;
            }
        };

        for (size_t boneIndex = 0; boneIndex < current.skeleton_bones.size(); ++boneIndex) {
            if (!current.skeleton_bone_valid[boneIndex] ||
                !previous.skeleton_bone_valid[boneIndex]) {
                continue;
            }
            suppressPoint(current.skeleton_bones[boneIndex], previous.skeleton_bones[boneIndex],
                previousObservation.skeleton_bone_valid[boneIndex] ? previousObservation.skeleton_bones[boneIndex] : Vector3{});
        }

        if (current.skeleton_bone_valid[0]) current.head_pos = current.skeleton_bones[0];
        if (current.skeleton_bone_valid[1]) current.neck_pos = current.skeleton_bones[1];
        if (current.skeleton_bone_valid[2]) current.chest_pos = current.skeleton_bones[2];

        // Position has its own bounded teleport confirmation. Applying a second
        // point gate here can permanently pin a valid new anchor to the old one.
        suppressPoint(current.head_pos, previous.head_pos, previousObservation.head_pos);
        suppressPoint(current.neck_pos, previous.neck_pos, previousObservation.neck_pos);
        suppressPoint(current.chest_pos, previous.chest_pos, previousObservation.chest_pos);
        if (current.cached_bot_chest_bone_valid && previous.cached_bot_chest_bone_valid)
            suppressPoint(current.cached_bot_chest_bone, previous.cached_bot_chest_bone, previousObservation.cached_bot_chest_bone);

        return suppressed;
    };

    auto publishRosterSnapshot = [&](DWORD now,
                                     size_t heroChanged,
                                     std::vector<OW::c_entity>& published,
                                     Diagnostics::EntityLifecycleStats* lifecycleStats = nullptr) {
        Diagnostics::RosterStats rosterStats{};
        rosterStats.heroChanged = heroChanged;
        published.clear();
        published.reserve(entityRoster.size());

        for (auto it = entityRoster.begin(); it != entityRoster.end();) {
            RosterEntry& rosterEntry = *it->second;
            OW::c_entity& rosterEntity = rosterEntry.entity;
            const bool seen = rosterEntry.seenThisCycle;
            if(const auto* discovery=cycleDiscoveries.Find(rosterEntity.address,rosterEntity.LinkParent,now);
                discovery && discovery->match==rosterEntity.match_id && discovery->hero==rosterEntity.HeroID) {
                rosterEntity.last_discovered_tick_ms=discovery->tick;
            }
            if (rosterEntry.lastScanSeenTick == 0 && rosterEntity.last_scan_seen_tick_ms != 0)
                rosterEntry.lastScanSeenTick = rosterEntity.last_scan_seen_tick_ms;
            if (rosterEntry.lastHotReadTick == 0 && rosterEntity.last_hot_read_tick_ms != 0)
                rosterEntry.lastHotReadTick = rosterEntity.last_hot_read_tick_ms;
            rosterEntity.last_scan_seen_tick_ms = rosterEntry.lastScanSeenTick;
            rosterEntity.last_hot_read_tick_ms = rosterEntry.lastHotReadTick;
            rosterEntity.scan_seen_gap_ms =
                rosterEntry.lastScanSeenTick != 0 ? now - rosterEntry.lastScanSeenTick : 0;
            rosterEntity.hot_read_gap_ms =
                rosterEntry.lastHotReadTick != 0 ? now - rosterEntry.lastHotReadTick : 0;
            rosterEntity.consecutive_scan_miss_count = rosterEntry.consecutiveScanMissCount;
            const DWORD lastSeen = rosterEntity.last_seen_tick_ms;

            bool expired=OW::EntityRosterPolicy::Expired(rosterEntity.last_discovered_tick_ms,now);
            if(expired) {
                // A producer can discover the actor while this cycle is doing slow work.
                std::scoped_lock lock(g_mutex,OW::raw_scan_mutex);
                const auto* latest=OW::entity_discoveries.Find(rosterEntity.address,rosterEntity.LinkParent,now);
                if(OW::entity_discoveries.connection==lastConnectionEpoch &&
                    OW::entity_discoveries.registry==processRegistryGeneration && OW::entity_discoveries.scene==processSceneGeneration &&
                    latest && latest->match==rosterEntity.match_id && latest->hero==rosterEntity.HeroID) {
                    rosterEntity.last_discovered_tick_ms=latest->tick;cycleDiscoveries.Observe(*latest);expired=false;
                } else {
                    const auto address=rosterEntity.address;
                    OW::BlockEntityBody(address,now);++OW::entity_seat_expired_count;
                    {
                        std::lock_guard<std::mutex> bodyLock(OW::entityBodyValidityMutex);
                        OW::entityBodyDiscoveries.erase(address);
                        OW::entityBodyAuthorities.erase(address);
                    }
                    OW::raw_discovery_stamps.erase(address);OW::entity_discoveries.actors.erase(address);
                    const auto matches=[&](const auto& pair){return pair.first==address;};
                    std::erase_if(OW::ow_entities,matches);std::erase_if(OW::ow_entities_scan,matches);
                    std::erase_if(OW::latest_raw_scan_snapshot.raw_entities,matches);
                    std::erase_if(OW::latest_raw_scan_snapshot.membership_entities,matches);
                    std::erase_if(OW::latest_raw_scan_snapshot.binding_entities,matches);
                    std::erase(OW::latest_raw_scan_snapshot.actual_members,address);
                }
            }
            if (expired) {
                const auto address=rosterEntity.address;
                componentBaseCache.erase(address);dynamicEntityCache.erase(address);
                discoveryAttemptTicks.erase(address);
                Diagnostics::Info("Entity seat expired: reason=no_actual_discovery_13000ms address=0x%llX.",address);
                it = entityRoster.erase(it);
                rosterStats.expired++;
                if (lifecycleStats)
                    ++lifecycleStats->entityRecordExpiredCount;
                continue;
            }

            const bool actualMember= !completeActorMembershipKnown ||
                std::find(cycleMemberPairs.begin(),cycleMemberPairs.end(),std::make_pair(rosterEntity.address,rosterEntity.LinkParent))!=cycleMemberPairs.end();
            if(!actualMember || !rosterEntity.HasFreshLifeState(now) || !OW::PresentationTiming::Fresh(rosterEntity.anchor_sample_tick_ms,now))
                rosterEntity.body_valid=false;
            const bool keepFreshDuringCnGrace = actualMember && rosterEntity.body_valid &&
                OW::offset::IsCnProfile() &&
                !seen &&
                rosterEntity.roster_state == OW::EntityRosterState::Fresh &&
                rosterEntity.Alive &&
                rosterEntity.PlayerHealth > 0.0f &&
                hasRenderableAnchor(rosterEntity) &&
                lastSeen != 0 &&
                OW::PresentationTiming::Fresh(rosterEntity.anchor_sample_tick_ms, now);

            if ((!actualMember || !rosterEntity.HasFreshLifeState(now) || (!seen && !keepFreshDuringCnGrace) ||
                 !OW::PresentationTiming::Fresh(rosterEntity.anchor_sample_tick_ms, now)) &&
                rosterEntity.roster_state != OW::EntityRosterState::Dead) {
                if (rosterEntity.roster_state != OW::EntityRosterState::Missing &&
                    lifecycleStats) {
                    ++lifecycleStats->entityRecordMarkMissingCount;
                }
                if (rosterEntity.missing_since_tick_ms == 0) {
                    missingTransitionNeedsScan=true;
                    rosterEntity.missing_since_tick_ms = now;
                    rosterEntry.respawnWatchStartedTick=now;
                    rosterEntry.respawnLastScanRequestTick=0;rosterEntry.respawnFirstScanRequested=false;
                    RuntimeTrace::Emit(RuntimeTrace::EntityMissing,rosterEntity.address,rosterEntity.actor_generation,rosterEntity.anchor_sample_tick_ms,now);
                }
                rosterEntity.roster_state = OW::EntityRosterState::Missing;
            }

            if (rosterEntity.roster_state == OW::EntityRosterState::Missing ||
                rosterEntity.roster_state == OW::EntityRosterState::Dead) {
                sanitizeStaleRosterEntity(rosterEntity);
            }

            if (OW::offset::IsCnProfile() &&
                rosterEntity.roster_state == OW::EntityRosterState::Fresh &&
                !hasRenderableAnchor(rosterEntity)) {
                it = entityRoster.erase(it);
                rosterStats.expired++;
                if (lifecycleStats)
                    ++lifecycleStats->entityRecordExpiredCount;
                continue;
            }

            switch (rosterEntity.roster_state) {
            case OW::EntityRosterState::Fresh:
                rosterStats.fresh++;
                break;
            case OW::EntityRosterState::Dead:
                rosterStats.dead++;
                break;
            case OW::EntityRosterState::Missing:
                rosterStats.missing++;
                break;
            }

            rosterEntity.published_tick_ms = now;
            rosterEntity.publish_sequence=entityPublishSequence;
            const float traceEntity[]={rosterEntity.pos.X,rosterEntity.pos.Y,rosterEntity.pos.Z,
                float(rosterEntity.roster_state),float(rosterEntity.Alive),
                float(rosterEntity.read_completed_tick_ms-rosterEntity.read_started_tick_ms)};
            RuntimeTrace::Emit(RuntimeTrace::EntityPublish,rosterEntity.address,rosterEntity.actor_generation,
                rosterEntity.anchor_sample_tick_ms,entityPublishSequence,traceEntity,6);
            published.push_back(rosterEntity);
            ++it;
        }

        std::sort(published.begin(), published.end(),
            [](const OW::c_entity& lhs, const OW::c_entity& rhs) {
                return lhs.roster_key < rhs.roster_key;
            });

        return rosterStats;
    };

