    auto recordRosterLinkChangeKeyKind =
        [&](uint64_t rosterKey, Diagnostics::EntityLifecycleStats& lifecycleStats) {
            switch (rosterKeyKind(rosterKey)) {
            case RosterKeyKind::Match:
                ++lifecycleStats.entityRecordLinkChangedMatchKeyCount;
                break;
            case RosterKeyKind::Link:
                ++lifecycleStats.entityRecordLinkChangedLinkKeyCount;
                break;
            case RosterKeyKind::Component:
                ++lifecycleStats.entityRecordLinkChangedComponentKeyCount;
                break;
            case RosterKeyKind::Participant:
                break;
            case RosterKeyKind::Unknown:
            default:
                break;
            }
        };

