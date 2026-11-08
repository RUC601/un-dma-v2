        // Optional game writes stay on the acquisition owner, separate from Overlay rendering.
#ifndef UNLEASHED_OBSERVATION_ONLY
        {
            size_t outlineBudget = 1;
            const bool world = offset::ActiveProfile() == offset::RuntimeProfile::World155284;
            const bool supported = world || offset::Active().componentTransform == offset::ComponentTransformMode::Cn155284;
            for (auto& [key, record] : entityRoster) {
                auto& state = record->outlineState;
                if (!OW::Config::gameOutlines && !state.Active()) continue;
                const auto& actor = record->entity;
                const std::array<uint64_t, 6> identity{actor.address, actor.LinkParent, actor.HeroID,
                    actor.actor_generation, actor.connection_epoch, actor.scene_generation};
                if (record->outlineIdentity != identity) {
                    state = {};
                    record->outlineIdentity = identity;
                }
                const DWORD outlineNow = GetTickCount();
                const bool enabled = OW::Config::gameOutlines && actor.Team && actor.team_known;
                const auto& color = actor.Vis ? OW::Config::enargb : OW::Config::invisnenargb;
                const uint32_t packed = OW::EdOutline::Color(color.x, color.y, color.z);
                if (!supported || !state.Due(outlineNow, enabled, actor.Vis, packed) || !outlineBudget) continue;
                if (!actor.HasUsableBody(outlineNow, processSceneGeneration)) continue;
                --outlineBudget;
                state.restoring = !enabled;
                state.attempted = true;
                state.nextAttempt = outlineNow + 500;
                state.lastVisible = actor.Vis;
                state.lastColor = packed;
                auto read = [](uint64_t at, void* data, size_t size) { return SDK->read_range(at, data, size); };
                auto write = [](uint64_t at, const void* data, size_t size) { return mem.Write(at, data, size); };
                const auto component = OW::DecryptComponent(actor.address, 0x5A);
                OW::EdOutline::Target target{};
                if (!OW::EdOutline::Resolve(component, read, target)) continue;
                auto current = [&] {
                    uint32_t match = 0;
                    OW::EdOutline::Target checked{};
                    return OW::RuntimeWorkersRunning() && offset::ActiveProfile() == runtimeProfile &&
                        actor.HasUsableBody(GetTickCount(), processSceneGeneration) &&
                        read(actor.address + offset::Entity_MatchId, &match, sizeof(match)) && match == actor.match_id &&
                        OW::DecryptComponent(actor.address, 0x5A) == component &&
                        OW::EdOutline::Resolve(component, read, checked) && checked == target;
                };
                if (enabled) {
                    state.Apply(target, actor.Vis, packed, outlineNow, read, write, current, world);
                } else if (state.target == target) {
                    if (state.Restore(read, write, current)) state = {};
                } else state = {};
            }
        }
#endif
