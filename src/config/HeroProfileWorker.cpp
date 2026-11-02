#include "HeroProfileWorker.hpp"
#include "Game/Overwatch.hpp"

// =========================================================================
// Config save/load thread
// =========================================================================

namespace OW { namespace Config {
    bool SaveConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
    void LoadConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
}}

void configsavenloadthread() {
    OW::HeroProfileSwitchDebouncer heroSwitchDebouncer(300);
    uint64_t observedConnectionEpoch = 0;
    DWORD lastInvalidHeroLogTick = 0;
    while (OW::RuntimeWorkersRunning()) {
        if (!OW::ProcessConnection::IsConnected()) {
            heroSwitchDebouncer.Reset();
            observedConnectionEpoch = 0;
            OW::RuntimeWorkerSleep(100);
            continue;
        }

        const uint64_t connectionEpoch =
            OW::ProcessConnection::ConnectionEpoch();
        if (connectionEpoch != observedConnectionEpoch) {
            heroSwitchDebouncer.Reset();
            observedConnectionEpoch = connectionEpoch;
        }

        const OW::c_entity localSnapshot = OW::SnapshotLocalEntity();
        const uint64_t currentHeroId = localSnapshot.HeroID;
        const bool validHero = OW::GameData::IsKnownHeroId(currentHeroId);
        const DWORD now = GetTickCount();
        if (currentHeroId != 0 && !validHero &&
            (lastInvalidHeroLogTick == 0 || now - lastInvalidHeroLogTick >= 5000)) {
            Diagnostics::Warn(
                "Ignored unknown local hero id during profile selection. heroId=0x%llX linkBase=0x%llX.",
                static_cast<unsigned long long>(currentHeroId),
                static_cast<unsigned long long>(localSnapshot.LinkBase));
            lastInvalidHeroLogTick = now;
        }

        bool switchedHero = false;
        if (!OW::Config::Menu) {
            const auto profileSwitch = heroSwitchDebouncer.Observe(
                currentHeroId,
                localSnapshot.LinkBase,
                GetTickCount64(),
                validHero);
            if (profileSwitch) {
                if (profileSwitch->previousHeroId != 0) {
                    if (!OW::Config::SaveConfigForHero(
                        OW::Config::ConfigPath(),
                        profileSwitch->previousHeroId,
                        profileSwitch->previousLinkBase)) {
                        heroSwitchDebouncer.RejectSwitch(*profileSwitch);
                        Diagnostics::Warn("Hero profile switch deferred: saving previous hero failed. hero=0x%llX",
                            static_cast<unsigned long long>(profileSwitch->previousHeroId));
                        OW::RuntimeWorkerSleep(1000);
                        continue;
                    }
                }

                OW::Config::LoadConfigForHero(
                    OW::Config::ConfigPath(),
                    profileSwitch->nextHeroId,
                    profileSwitch->nextLinkBase);
                const int outputReconcileStatus =
                    V2::Output::ReconcileRuntimeFromConfig(std::chrono::milliseconds(500));
                if (outputReconcileStatus != success) {
                    Diagnostics::Warn(
                        "KMBox hero-profile reconcile failed. heroId=0x%llX status=%d",
                        static_cast<unsigned long long>(profileSwitch->nextHeroId),
                        outputReconcileStatus);
                }
                OW::Config::nowhero = "Now using: " +
                    OW::GetHeroEngNames(
                        profileSwitch->nextHeroId,
                        profileSwitch->nextLinkBase);
                switchedHero = true;
            }
        }

        if (!switchedHero && OW::Config::manualsave &&
            heroSwitchDebouncer.ConfirmedHeroId() != 0) {
            if (OW::Config::SaveConfigForHero(
                OW::Config::ConfigPath(),
                heroSwitchDebouncer.ConfirmedHeroId(),
                heroSwitchDebouncer.ConfirmedLinkBase())) {
                OW::Config::manualsave = false;
            } else {
                OW::RuntimeWorkerSleep(1000);
            }
        }
        OW::RuntimeWorkerSleep(2);
    }

}
