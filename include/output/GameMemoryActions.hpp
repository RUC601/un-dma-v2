#pragma once

#include <cstdint>

namespace OW { class c_entity; }

namespace V2::Output::GameActions {

enum class Action { Ability1, Ability2, Ability3, Ultimate, Melee };
uint32_t ActionMask(Action action) noexcept;

struct PulseUpdate {
    bool ok = true;
    bool wasOwned = false;
    bool owned = false;
};

bool StartMelee(const OW::c_entity& local, bool dryRun);
PulseUpdate UpdateMelee(bool keepHolding);
bool MeleeOwned();
bool StartSkill(const OW::c_entity& local, uint32_t actionMask, uint64_t holdMs, bool dryRun);
bool UpdateSkill(bool keepHolding);

#ifdef UNLEASHED_SELF_TESTS
namespace Testing {
void SeedRetiredMeleePulse(uint64_t nowMs);
}
#endif

}
