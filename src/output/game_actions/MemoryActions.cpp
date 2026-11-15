#include "output/GameMemoryActions.hpp"
#include "MemoryPulse.hpp"
#include "Game/Entity.hpp"
#include "Game/Offsets.hpp"
#include "Game/RuntimeState.hpp"
#include "Game/SDK.hpp"
#include "Game/snapshots/SnapshotAccess.hpp"
#include "Utils/ProcessConnection.hpp"
#include <mutex>

namespace V2::Output::GameActions {
namespace {

bool SameBodyIdentity(const OW::c_entity& left, const OW::c_entity& right)
{
    return left.address == right.address && left.HeroID == right.HeroID &&
        left.actor_generation == right.actor_generation &&
        left.connection_epoch == right.connection_epoch &&
        left.scene_generation == right.scene_generation &&
        left.body_absence_revision == right.body_absence_revision;
}

bool SupportedProfile(OW::offset::RuntimeProfile profile)
{
    return profile == OW::offset::RuntimeProfile::Cn ||
        profile == OW::offset::RuntimeProfile::World155284 ||
        profile == OW::offset::RuntimeProfile::Nexon155284;
}

struct PulseSession {
    MemoryPulse pulse;
    OW::c_entity body;
    int pid = 0;
    OW::offset::RuntimeProfile profile = OW::offset::kProductRuntimeProfile;

    bool TargetCurrent() const
    {
        if (!OW::SDK || !OW::SDK->IsInitialized() || !mem.IsProcessInitialized() || pid <= 0 ||
            mem.GetCurrentProcessId() != pid || !OW::ProcessConnection::IsConnected() ||
            OW::offset::ActiveProfile() != profile ||
            OW::ProcessConnection::ConnectionEpoch() != body.connection_epoch ||
            OW::SDK->g_player_controller.load() != body.AngleBase)
            return false;

        const auto local = OW::SnapshotFrame()->local;
        return local.AngleBase == body.AngleBase && local.LinkParent == body.LinkParent &&
            local.match_id == body.match_id &&
            local.body_authority_revision == body.body_authority_revision &&
            SameBodyIdentity(local, body) &&
            local.HasUsableBody(GetTickCount(), OW::EntitySceneGeneration());
    }

    bool Start(const OW::c_entity& local, uint32_t mask, uint64_t holdMs, bool dryRun)
    {
#ifdef UNLEASHED_OBSERVATION_ONLY
        return false;
#else
        const auto activeProfile = OW::offset::ActiveProfile();
        if (!mask || dryRun || !local.AngleBase || pulse.owned || !SupportedProfile(activeProfile))
            return false;

        body = local;
        pid = mem.GetCurrentProcessId();
        profile = activeProfile;
        return pulse.Start(local.AngleBase + kActionOffset, GetTickCount64(),
            [](uint64_t address, void* value, size_t bytes) {
                return mem.ReadFreshExact(address, value, bytes);
            },
            [this](uint64_t address, const void* value, size_t bytes) {
                return mem.Write(address, value, bytes, pid);
            },
            [this] { return TargetCurrent(); }, mask, holdMs);
#endif
    }

    bool Update(bool keepHolding)
    {
        return pulse.Update(GetTickCount64(), keepHolding,
            [](uint64_t address, void* value, size_t bytes) {
                return mem.ReadFreshExact(address, value, bytes);
            },
            [this](uint64_t address, const void* value, size_t bytes) {
                return mem.Write(address, value, bytes, pid);
            },
            [this] { return TargetCurrent(); });
    }
};

// The two producers retain independent ownership and release deadlines.
std::mutex stateMutex;
PulseSession melee;
PulseSession skill;

}

uint32_t ActionMask(Action action) noexcept
{
    switch (action) {
    case Action::Ability1: return 0x8;
    case Action::Ability2: return 0x10;
    case Action::Ability3: return 0x40; // Candidate from the legacy action-word layout.
    case Action::Ultimate: return 0x20;
    case Action::Melee: return kActionMask;
    default: return 0;
    }
}

bool StartMelee(const OW::c_entity& local, bool dryRun)
{
    std::lock_guard lock(stateMutex);
    return melee.Start(local, kActionMask, kHoldMs, dryRun);
}

PulseUpdate UpdateMelee(bool keepHolding)
{
    std::lock_guard lock(stateMutex);
    const bool wasOwned = melee.pulse.owned;
    const bool ok = melee.Update(keepHolding);
    return {ok, wasOwned, melee.pulse.owned};
}

bool MeleeOwned()
{
    std::lock_guard lock(stateMutex);
    return melee.pulse.owned;
}

bool StartSkill(const OW::c_entity& local, uint32_t mask, uint64_t holdMs, bool dryRun)
{
    std::lock_guard lock(stateMutex);
    return skill.Start(local, mask, holdMs, dryRun);
}

bool UpdateSkill(bool keepHolding)
{
    std::lock_guard lock(stateMutex);
    return skill.Update(keepHolding);
}

#ifdef UNLEASHED_SELF_TESTS
void Testing::SeedRetiredMeleePulse(uint64_t nowMs)
{
    std::lock_guard lock(stateMutex);
    melee = {};
    melee.pulse.address = 0x10000;
    melee.pulse.startedMs = nowMs;
    melee.pulse.owned = true;
}
#endif

}
