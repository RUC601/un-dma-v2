        // Optional game writes stay on the acquisition owner, separate from Overlay rendering.
#ifndef UNLEASHED_OBSERVATION_ONLY
        {
            namespace Brightness = OW::OutlineBrightness;
            const auto brightnessCommand = Brightness::Snapshot();
            size_t outlineBudget = 1;
            const bool world = offset::ActiveProfile() == offset::RuntimeProfile::World155284;
            const bool supported = world || offset::Active().componentTransform == offset::ComponentTransformMode::Cn155284;
            for (auto& [key, record] : entityRoster) {
                auto& state = record->outlineState;
                auto& brightness = record->brightnessState;
                if (!OW::Config::gameOutlines && !state.Active() && !brightness.owned) continue;
                const auto& actor = record->entity;
                const std::array<uint64_t, 6> identity{actor.address, actor.LinkParent, actor.HeroID,
                    actor.actor_generation, actor.connection_epoch, actor.scene_generation};
                if (record->outlineIdentity != identity) {
                    state = {};
                    brightness = {};
                    record->outlineIdentity = identity;
                }
                const DWORD outlineNow = GetTickCount();
                const bool enabled = OW::Config::gameOutlines && actor.Team && actor.team_known &&
                    (!OW::Config::gameOutlineVisibleOnly || actor.Vis);
                const bool brightnessEnabled = enabled && brightnessCommand.enabled;
                const bool brightnessDue = brightness.Due(outlineNow, brightnessEnabled, brightnessCommand.revision);
                if (!enabled && !state.Active() && !brightness.owned) continue;
                const auto& color = actor.Vis ? OW::Config::enargb : OW::Config::invisnenargb;
                const uint32_t packed = OW::EdOutline::AnimatedColor(color.x, color.y, color.z,
                    OW::Config::gameOutlineRainbow, GetTickCount64());
                const bool outlineDue = supported && state.Due(outlineNow, enabled, actor.Vis, packed, OW::Config::gameOutlineRainbow);
                if ((!outlineDue && !brightnessDue) || !outlineBudget) continue;
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
                if (brightnessDue) {
                    const auto previousResult = brightness.result;
                    const auto previousRevision = brightness.appliedRevision;
                    brightness.nextAttempt = outlineNow + 500;
                    brightness.triedRevision = brightnessCommand.revision;
                    Brightness::Target target{};
                    if (Brightness::Resolve(component, SDK->dwGameBase, runtimeProfile, freshRead, target)) {
                        auto current = [&] {
                            Brightness::Target checked{};
                            return bodyCurrent() && Brightness::Resolve(component, SDK->dwGameBase,
                                runtimeProfile, freshRead, checked) && checked == target;
                        };
                        if (brightnessEnabled) {
                            brightness.Apply(target, brightnessCommand.value, brightnessCommand.revision, freshRead, write, current);
                        } else if (brightness.target == target) {
                            if (brightness.Restore(freshRead, write, current)) brightness = {};
                        } else brightness = {};
                    } else brightness.result = Brightness::Result::ReadFailed;
                    if (brightness.result != previousResult || brightness.appliedRevision != previousRevision)
                        Diagnostics::Info("Outline opacity: profile=%s actor=0x%llX component=0x%llX value=%.9g revision=%llu result=%d.",
                            offset::Active().name, static_cast<unsigned long long>(actor.address),
                            static_cast<unsigned long long>(component), double(brightnessCommand.value),
                            static_cast<unsigned long long>(brightnessCommand.revision), int(brightness.result));
                }
                if (!outlineDue) continue;
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
            uint32_t applied = 0, failed = 0, restoring = 0;
            for (const auto& [key, record] : entityRoster) {
                const auto& brightness = record->brightnessState;
                applied += brightnessCommand.enabled && brightness.appliedRevision == brightnessCommand.revision &&
                    brightness.result == Brightness::Result::Applied;
                failed += brightness.result == Brightness::Result::ReadFailed || brightness.result == Brightness::Result::WriteFailed;
                restoring += brightness.owned && (!brightnessCommand.enabled || !OW::Config::gameOutlines);
            }
            Brightness::AppliedCount = applied;
            Brightness::FailureCount = failed;
            Brightness::RestoreCount = restoring;
        }
#endif
