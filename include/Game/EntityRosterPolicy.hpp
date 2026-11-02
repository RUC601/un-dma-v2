#pragma once

#include <cstdint>
#include <string_view>
#include <algorithm>
#include <utility>
#include <vector>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace OW::EntityRosterPolicy {

inline constexpr uint32_t kRetentionMs = 13000;

inline void PrioritizeDiscoveryLinks(std::vector<uint64_t>& pending,
    const std::unordered_set<uint64_t>& knownLinks) {
    for(auto parent:knownLinks)
        if(std::find(pending.begin(),pending.end(),parent)==pending.end())pending.push_back(parent);
    // Preserve rotation among waiting players while new ordinary entities arrive.
    std::stable_partition(pending.begin(),pending.end(),[&](auto parent){return knownLinks.contains(parent);});
}

struct Discovery {
    uint64_t address = 0, link = 0, hero = 0;
    uint32_t match = 0, tick = 0;
};

inline bool ValidTeamKey(uint32_t key) noexcept {
    constexpr uint32_t mask = 0x0F800000;
    return key && !(key & ~mask) && !(key & (key - 1));
}

struct TeamConfirmation {
    uint32_t key = 0;
    uint64_t linkParent = 0;
    uint32_t matchId = 0;
    bool known = false;
    bool ConfirmIdentity(uint64_t link, uint32_t match) noexcept {
        known = ValidTeamKey(key) && link && match && linkParent==link && matchId==match;
        return known;
    }
    bool Observe(uint32_t observed, uint64_t link, uint32_t match) noexcept {
        known = false;
        if (!link || !match) return false;
        // A rescan or component relocation does not create a new player pair.
        if (linkParent != link || matchId != match) {
            key=0; linkParent=link; matchId=match;
        }
        // Failed or contradictory live bytes cannot erase this pair's confirmed team.
        ConfirmIdentity(link,match);
        if (!ValidTeamKey(observed) || (key && key != observed)) return false;
        key=observed;
        known=true;
        return known;
    }
};

inline bool AuthoritativeRoster(bool readComplete, bool bindingsComplete,
    const std::vector<Discovery>& proofs) noexcept {
    return readComplete && bindingsComplete && !proofs.empty();
}

inline bool Expired(uint32_t observed, uint32_t now) noexcept {
    // A producer may finish an observation after the consumer captured now.
    return int32_t(now - observed) >= int32_t(kRetentionMs);
}

// Owned by the scan exchange mutex; observations survive latest-wins replacement.
struct DiscoveryBook {
    uint64_t connection = 0, registry = 0, scene = 0;
    std::unordered_map<uint64_t, Discovery> actors;
    void SetContext(uint64_t c, uint64_t r, uint64_t s) {
        if (connection != c || registry != r || scene != s) actors.clear();
        connection = c; registry = r; scene = s;
    }
    void Observe(const Discovery& value) {
        auto found = actors.find(value.address);
        if (found == actors.end() || int32_t(value.tick - found->second.tick) > 0)
            actors[value.address] = value;
    }
    const Discovery* Find(uint64_t address, uint64_t link, uint32_t now) const {
        const auto found = actors.find(address);
        return found != actors.end() && found->second.link == link && !Expired(found->second.tick, now)
            ? &found->second : nullptr;
    }
    void Prune(uint32_t now) {
        std::erase_if(actors, [&](const auto& item) { return Expired(item.second.tick, now); });
    }
};

enum class SceneAudit : uint8_t { Unknown, Present, Empty };
inline SceneAudit ClassifySceneMember(bool headerRead,bool heroComponent,bool knownHero,
    bool actorComponents,bool knownDynamic) {
    if(!headerRead)return SceneAudit::Unknown;
    if(knownDynamic)return SceneAudit::Empty;
    if(heroComponent)return knownHero?SceneAudit::Present:SceneAudit::Unknown;
    return actorComponents && !knownDynamic?SceneAudit::Unknown:SceneAudit::Empty;
}
struct EmptySceneWatch {
    uint64_t connection = 0, registry = 0, scene = 0;
    uint32_t since = 0, latestTick = 0;
    bool pending = false, ready = false, cleared = false, hasSample = false;
    void Observe(uint64_t c, uint64_t r, uint64_t s, SceneAudit audit, uint32_t tick) {
        if (connection != c || registry != r || scene != s) {
            *this = {}; connection = c; registry = r; scene = s;
        }
        if (hasSample && int32_t(tick - latestTick) <= 0) return;
        latestTick = tick; hasSample = true;
        if (audit != SceneAudit::Empty) {
            pending = false; ready = false;
            if (audit == SceneAudit::Present) cleared = false;
            return;
        }
        if (cleared) return;
        if (!pending) { since = tick; pending = true; }
        ready = Expired(since, tick);
    }
    void MarkCleared(uint64_t nextScene) {
        scene = nextScene; pending = ready = false; cleared = true;
    }
};

inline bool BodyUsable(bool valid, bool alive, bool freshState, uint32_t sample,
                       uint32_t now, uint64_t scene, uint64_t currentScene) noexcept {
    return valid && alive && freshState && scene == currentScene && sample != 0 && uint32_t(now - sample) <= 100;
}

inline constexpr uint64_t kParticipantRosterPrefix = 0x4000000000000000ull;
inline constexpr uint64_t kRosterPayloadMask = 0x0FFFFFFFFFFFFFFFull;

inline uint64_t ParticipantRosterKey(std::string_view participantId) noexcept
{
    if (participantId.size() < 2 || participantId.size() > 63)
        return 0;

    bool hasVisibleByte = false;
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char value : participantId) {
        if (value == 0 || value < 0x20 || value == 0x7F)
            return 0;
        hasVisibleByte = true;
        hash ^= static_cast<uint64_t>(value);
        hash *= 1099511628211ull;
    }

    if (!hasVisibleByte)
        return 0;
    return kParticipantRosterPrefix | (hash & kRosterPayloadMask);
}

enum class SameParticipantObservationAction : uint8_t {
    ReplaceExisting,
    KeepExisting,
    UseActorKey,
};

inline bool CanReturnRetiredActor(bool sameSignature,bool absenceObserved,bool actualMember) noexcept {
    return !sameSignature || (absenceObserved && actualMember);
}

inline bool ScanCanRetire(bool complete,uint64_t startedRevision,uint64_t currentRevision) noexcept {
    return complete && startedRevision==currentRevision;
}

inline std::vector<uint64_t> AddedTopologyMembers(const std::vector<uint64_t>& previous,
    const std::vector<uint64_t>& current,bool sameRoot) {
    if(!sameRoot)return current;
    std::vector<uint64_t> added;
    std::set_difference(current.begin(),current.end(),previous.begin(),previous.end(),std::back_inserter(added));
    return added;
}

inline bool ReliableParticipantId(std::string_view id) noexcept {
    const auto separator=id.rfind('#');
    if(separator==std::string_view::npos || separator==0 || id.size()-separator<3)return false;
    for(size_t i=separator+1;i<id.size();++i)if(id[i]<'0'||id[i]>'9')return false;
    return ParticipantRosterKey(id)!=0;
}

inline void MergePartialScan(std::vector<std::pair<uint64_t,uint64_t>>& incoming,
    const std::vector<std::pair<uint64_t,uint64_t>>& previous,bool complete) {
    if(complete)return;
    for(const auto& pair:previous)if(std::find(incoming.begin(),incoming.end(),pair)==incoming.end())incoming.push_back(pair);
}

enum class UltimateRosterFilter : uint8_t {
    All = 0,
    Enemy = 1,
    Ally = 2,
};

enum class RespawnRescanDue : uint8_t {
    None = 0,
    First,
    Retry,
};

inline SameParticipantObservationAction ResolveSameParticipantObservation(
    bool existingSeenThisCycle,
    bool sameActor,
    bool existingFresh,
    bool incomingFresh,
    bool membershipKnown = false,
    bool existingMember = true,
    bool incomingMember = true) noexcept
{
    if (membershipKnown && !sameActor && !existingMember && incomingMember && incomingFresh)
        return SameParticipantObservationAction::ReplaceExisting;
    if (!existingSeenThisCycle || sameActor)
        return SameParticipantObservationAction::ReplaceExisting;
    if (incomingFresh && !existingFresh)
        return SameParticipantObservationAction::ReplaceExisting;
    if (!incomingFresh && existingFresh)
        return SameParticipantObservationAction::KeepExisting;
    if (incomingFresh)
        return SameParticipantObservationAction::UseActorKey;
    return SameParticipantObservationAction::KeepExisting;
}

inline constexpr bool ShouldRenderWorldHeroAvatar(bool isEnemy) noexcept
{
    return isEnemy;
}

inline constexpr RespawnRescanDue ResolveRespawnRescanDue(
    bool dead,
    uint32_t watchStartedTick,
    bool firstScanRequested,
    uint32_t lastScanRequestTick,
    uint32_t nowTick,
    uint32_t firstDelayMs,
    uint32_t retryMs,
    uint32_t watchMs) noexcept
{
    if (!dead || watchStartedTick == 0)
        return RespawnRescanDue::None;
    const uint32_t deadAge = nowTick - watchStartedTick;
    if (deadAge < firstDelayMs)
        return RespawnRescanDue::None;
    if (!firstScanRequested)
        return RespawnRescanDue::First;
    if (lastScanRequestTick == 0 ||
        nowTick - lastScanRequestTick >= (deadAge <= watchMs ? retryMs : 1000)) {
        return RespawnRescanDue::Retry;
    }
    return RespawnRescanDue::None;
}

inline constexpr bool ShouldRenderUltimateRosterEntry(int filter,
                                                       bool isEnemy) noexcept
{
    switch (static_cast<UltimateRosterFilter>(filter)) {
    case UltimateRosterFilter::All:
        return true;
    case UltimateRosterFilter::Ally:
        return !isEnemy;
    case UltimateRosterFilter::Enemy:
    default:
        return isEnemy;
    }
}

} // namespace OW::EntityRosterPolicy
