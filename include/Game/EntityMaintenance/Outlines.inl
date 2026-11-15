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
                const bool enabled = OW::Config::gameOutlines && actor.Team && actor.team_known &&
                    (!OW::Config::gameOutlineVisibleOnly || actor.Vis);
                if (!enabled && !state.Active()) continue;
                const auto& color = actor.Vis ? OW::Config::enargb : OW::Config::invisnenargb;
                const uint32_t packed = OW::EdOutline::AnimatedColor(color.x, color.y, color.z,
                    OW::Config::gameOutlineRainbow, GetTickCount64());
                const bool outlineDue = supported && state.Due(outlineNow, enabled, actor.Vis, packed, OW::Config::gameOutlineRainbow);
                if (!outlineDue || !outlineBudget) continue;
                if (!actor.HasUsableBody(outlineNow, processSceneGeneration)) continue;
                --outlineBudget;
                auto read = [](uint64_t at, void* data, size_t size) { return SDK->read_range(at, data, size); };
                auto freshRead = [](uint64_t at, void* data, size_t size) { return mem.ReadFreshExact(at, data, size); };
                auto write = [](uint64_t at, const void* data, size_t size) { return mem.Write(at, data, size); };
                const auto component = OW::DecryptComponent(actor.address, 0x5A);
                auto bodyCurrent = [&] {
                    uint32_t match = 0;
                    return OW::RuntimeWorkersRunning() && offset::ActiveProfile() == runtimeProfile &&
                        OW::ProcessConnection::ConnectionEpoch() == connectionEpoch &&
                        actor.HasUsableBody(GetTickCount(), processSceneGeneration) &&
                        freshRead(actor.address + offset::Entity_MatchId, &match, sizeof(match)) && match == actor.match_id &&
                        OW::DecryptComponent(actor.address, 0x5A) == component;
                };
                state.restoring = !enabled;
                state.attempted = true;
                state.nextAttempt = outlineNow + 500;
                state.lastVisible = actor.Vis;
                state.lastColor = packed;
                OW::EdOutline::Target target{};
                if (!OW::EdOutline::Resolve(component, read, target)) continue;
                auto current = [&] {
                    OW::EdOutline::Target checked{};
                    return bodyCurrent() && OW::EdOutline::Resolve(component, read, checked) && checked == target;
                };
                if (enabled) {
                    state.Apply(target, actor.Vis, packed, outlineNow, read, write, current, world,
                        OW::Config::gameOutlineRainbow ? 100 : 500);
                } else if (state.target == target) {
                    if (state.Restore(read, write, current)) state = {};
                } else state = {};
            }
        }
#endif
