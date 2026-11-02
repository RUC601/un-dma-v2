// Shared lexical step of entity_thread; requires its surrounding cycle state.
        auto attachPreviousRenderSample = [&](OW::c_entity& entity) {
            const DWORD sampleTick = entity.anchor_sample_tick_ms;
            entity.render_sample_tick_ms = sampleTick;

            const OW::c_entity* previousEntity = nullptr;
            const auto previousIt = previousEntityByAddress.find(entity.address);
            if (previousIt != previousEntityByAddress.end())
                previousEntity = previousIt->second;
            if (!previousEntity && entity.roster_key != 0) {
                const auto rosterPreviousIt = previousEntityByRosterKey.find(entity.roster_key);
                if (rosterPreviousIt != previousEntityByRosterKey.end())
                    previousEntity = rosterPreviousIt->second;
            }

            if (previousEntity && !OW::PresentationTiming::CanReuseHistory(
                previousEntity->PresentationIdentity(),entity.PresentationIdentity())) previousEntity=nullptr;
            if (!previousEntity) {
                entity.previous_render_sample_tick_ms = sampleTick;
                entity.position_sample_tick_ms = sampleTick;
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
                return;
            }

            const OW::c_entity& previous = *previousEntity;
            entity.previous_sample_time_us=previous.sample_time_us;
            entity.previous_render_sample_tick_ms =
                previous.render_sample_tick_ms ? previous.render_sample_tick_ms : sampleTick;
            const bool positionChanged =
                isUsableVector(entity.pos) &&
                isUsableVector(previous.pos) &&
                entity.pos.DistTo(previous.pos) > 0.03f;
            entity.position_sample_tick_ms = positionChanged
                ? sampleTick
                : (previous.position_sample_tick_ms ? previous.position_sample_tick_ms : sampleTick);
            entity.has_previous_render_sample =
                previous.render_sample_tick_ms != 0 &&
                previous.render_sample_tick_ms != sampleTick;
            entity.previous_head_pos = previous.head_pos;
            entity.previous_velocity = previous.velocity;
            entity.previous_pos = previous.pos;
            entity.previous_neck_pos = previous.neck_pos;
            entity.previous_chest_pos = previous.chest_pos;
            entity.previous_skeleton_bones = previous.skeleton_bones;
            entity.previous_skeleton_bone_valid = previous.skeleton_bone_valid;
            entity.previous_cached_bot_chest_bone = previous.cached_bot_chest_bone;
            entity.previous_cached_bot_chest_bone_valid = previous.cached_bot_chest_bone_valid;
        };

        // Publish established actors before slow component/skeleton/skill maintenance.
