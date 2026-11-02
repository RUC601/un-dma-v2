// Shared lexical step of entity_thread; requires its surrounding cycle state.
    std::unordered_map<uint64_t, std::shared_ptr<RosterEntry>> entityRoster{};
    entityRoster.reserve(128);
    size_t maintenanceCursor = 0;
    std::unordered_map<uint64_t,DWORD> discoveryAttemptTicks;
    bool missingTransitionNeedsScan=false;
    uint64_t actorGenerationSequence = 0, entityPublishSequence = 0;
    struct RetiredActor {uint32_t matchId=0;uint64_t link=0;bool absent=false;uint64_t absenceRevision=0;};
    std::unordered_map<uint64_t,RetiredActor> retiredActors;
    std::unordered_set<uint64_t> latestCompleteActorMembers;
    bool completeActorMembershipKnown=false;


    constexpr uint64_t kRosterMatchPrefix = 0x1000000000000000ull;
    constexpr uint64_t kRosterLinkPrefix = 0x2000000000000000ull;
    constexpr uint64_t kRosterComponentPrefix = 0x3000000000000000ull;
    constexpr uint64_t kRosterParticipantPrefix =
        OW::EntityRosterPolicy::kParticipantRosterPrefix;

    auto makeRosterKey = [&](uint32_t matchId, uint64_t linkParent, uint64_t componentParent) -> uint64_t {
        if (matchId != 0)
            return kRosterMatchPrefix | static_cast<uint64_t>(matchId);
        if (linkParent != 0)
            return kRosterLinkPrefix ^ linkParent;
        return kRosterComponentPrefix ^ componentParent;
    };

    enum class RosterKeyKind {
        Unknown,
        Match,
        Link,
        Component,
        Participant,
    };

    auto rosterKeyKind = [&](uint64_t rosterKey) {
        constexpr uint64_t kRosterPrefixMask = 0xF000000000000000ull;
        switch (rosterKey & kRosterPrefixMask) {
        case kRosterMatchPrefix:
            return RosterKeyKind::Match;
        case kRosterLinkPrefix:
            return RosterKeyKind::Link;
        case kRosterComponentPrefix:
            return RosterKeyKind::Component;
        case kRosterParticipantPrefix:
            return RosterKeyKind::Participant;
        default:
            return RosterKeyKind::Unknown;
        }
    };

