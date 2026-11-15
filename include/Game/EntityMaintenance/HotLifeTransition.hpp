#pragma once
#include "Game/Entity.hpp"

namespace OW::HotLifeTransition {

inline bool NeedsCurrentLifeRead(const c_entity& previous) noexcept {
    return previous.roster_state != EntityRosterState::Fresh ||
        !previous.Alive || !previous.body_valid;
}

// Missing hides a sample; only an observed death ends the body's lifetime.
// This selects the hot recovery route, not admission: the caller must still
// validate discovery/membership, the observation window and AdmitEntityBodyObservation.
inline bool CanResumeMissingBody(const c_entity& previous, const c_entity& current,
    bool completeCurrentHealth, EntityBodyObservation observation,
    uint64_t epoch, uint64_t scene, uint32_t now) {
    if (previous.roster_state != EntityRosterState::Missing || previous.Alive ||
        !previous.death_known || previous.is_dead || !previous.death_sample_tick_ms ||
        !completeCurrentHealth || !current.Alive || !current.HasFreshLifeState(now) ||
        !std::isfinite(current.PlayerHealth) || current.PlayerHealth <= 0 ||
        !previous.address || !previous.LinkParent || !previous.match_id || !previous.HeroID ||
        !previous.actor_generation || previous.address != current.address ||
        previous.LinkParent != current.LinkParent || previous.match_id != current.match_id ||
        previous.HeroID != current.HeroID || previous.actor_generation != current.actor_generation ||
        !epoch || previous.connection_epoch != epoch || current.connection_epoch != epoch ||
        !scene || previous.scene_generation != scene || current.scene_generation != scene ||
        previous.body_absence_revision != current.body_absence_revision ||
        previous.body_absence_revision != observation.absence)
        return false;

    // Only visibility invalidation is recoverable here. A real revocation,
    // death or replacement still needs full reconciliation. Admission repeats
    // the fence/context checks atomically before publication.
    std::lock_guard<std::mutex> lock(entityBodyValidityMutex);
    const auto found = entityBodyAuthorities.find(previous.address);
    if (found == entityBodyAuthorities.end()) return false;
    const auto& authority = found->second;
    return authority.admitted && authority.alive &&
        authority.revision == previous.body_authority_revision &&
        authority.generation == previous.actor_generation &&
        authority.epoch == epoch && authority.scene == scene &&
        authority.absence == observation.absence && authority.revision <= observation.fence &&
        authority.identity.link == previous.LinkParent && authority.identity.match == previous.match_id &&
        authority.identity.hero == previous.HeroID;
}

inline bool RequiresFullReconciliation(const c_entity& previous, const c_entity& current,
    bool completeCurrentHealth, EntityBodyObservation observation,
    uint64_t epoch, uint64_t scene, uint32_t now, bool heroRead, uint64_t observedHero,
    bool heroRequired = false) {
    if (NeedsCurrentLifeRead(previous) && heroRequired && (!heroRead || !observedHero)) return true;
    if (heroRead && observedHero != 0 && observedHero != current.HeroID) return true;
    return current.Alive && !previous.Alive &&
        !CanResumeMissingBody(previous, current, completeCurrentHealth, observation, epoch, scene, now);
}

}
