// Runs once on the acquisition owner before actor discovery/early exits.
// Scene transitions and the optional colored-outline switch do not own this setting.
#ifndef UNLEASHED_OBSERVATION_ONLY
        {
            namespace Brightness = OW::OutlineBrightness;
            const auto requested = Brightness::Snapshot();
            const auto shutdownRestore = Brightness::ShutdownRestoreRequest.load(std::memory_order_acquire);
            const bool restoreForShutdown = shutdownRestore !=
                Brightness::ShutdownRestoreAcknowledged.load(std::memory_order_acquire);
            const bool enabled = requested.enabled && !restoreForShutdown;
            auto& state = nativeBrightnessState;
            const DWORD now = GetTickCount();
            if (restoreForShutdown && !state.owned) {
                Brightness::ShutdownRestoreAcknowledged.store(shutdownRestore, std::memory_order_release);
            }
            if ((restoreForShutdown && state.owned) || state.Due(now, enabled, requested.revision)) {
                const auto previousResult = state.result;
                const auto previousRevision = state.appliedRevision;
                state.nextAttempt = now + 500;
                state.triedRevision = requested.revision;
                const uint64_t base = SDK->dwGameBase;
                auto read = [](uint64_t at, void* data, size_t size) { return mem.ReadFreshExact(at, data, size); };
                auto write = [](uint64_t at, const void* data, size_t size) { return mem.Write(at, data, size); };
                Brightness::Target target{};
                Brightness::Result failure = Brightness::Result::ReadFailed;
                if (Brightness::Resolve(base, runtimeProfile, read, target, &failure)) {
                    auto current = [&] {
                        Brightness::Target checked{};
                        return OW::RuntimeWorkersRunning() && OW::ProcessConnection::IsConnected() &&
                            OW::ProcessConnection::ConnectionEpoch() == connectionEpoch &&
                            offset::ActiveProfile() == runtimeProfile && SDK->dwGameBase == base &&
                            Brightness::Resolve(base, runtimeProfile, read, checked) && checked == target;
                    };
                    if (enabled) {
                        state.Apply(target, requested.value, requested.revision, read, write, current);
                    } else if (state.target == target) {
                        state.Restore(read, write, current);
                    } else {
                        // A new array/profile no longer owns the old allocation.
                        state.Abandon();
                        state.result = Brightness::Result::Changed;
                    }
                } else state.result = failure;
                Brightness::AppliedCount = enabled && state.appliedRevision == requested.revision &&
                    state.result == Brightness::Result::Applied ? 1 : 0;
                Brightness::FailureCount = Brightness::IsFailure(state.result) ? 1 : 0;
                Brightness::RestoreCount = !enabled && state.owned ? 1 : 0;
                Brightness::LastResult = state.result;
                if (restoreForShutdown && !state.owned)
                    Brightness::ShutdownRestoreAcknowledged.store(shutdownRestore, std::memory_order_release);
                if (state.result != previousResult || state.appliedRevision != previousRevision)
                    Diagnostics::Info("Native outline brightness: profile=%s address=0x%llX percent=%.9g revision=%llu original=%.9g written=%.9g result=%s.",
                        offset::Active().name, static_cast<unsigned long long>(target.value), double(requested.value),
                        static_cast<unsigned long long>(requested.revision), double(std::bit_cast<float>(state.original)),
                        double(std::bit_cast<float>(state.written)), Brightness::ResultText(state.result));
            }
        }
#endif
