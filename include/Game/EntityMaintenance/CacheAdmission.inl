// Check the allocation/layout as well as the parent address; live components can move.
            auto cacheIt=componentBaseCache.find(ComponentParent);
            if(cacheIt==componentBaseCache.end())
                cacheIt=componentBaseCache.emplace(ComponentParent,std::make_shared<RosterEntry>()).first;
            bool componentCacheHit=cacheIt->second->baseUpdateTick!=0 && !cacheIt->second->componentRefresh.Active();
            if(componentCacheHit) {
                auto& record=*cacheIt->second;
                const DWORD refreshInterval=record.alive?OW::kEntityLiveComponentRefreshMs:OW::kEntityDeadComponentRefreshMs;
                const bool bindingChanged=record.linkParent!=LinkParent;
                const bool refreshDue=!record.baseUpdateTick ||
                    processLoopTick-record.baseUpdateTick>=refreshInterval;
                bool layoutChanged = false;
                bool layoutReadable = true;
                bool matchChanged = false;
                if (!bindingChanged && (!record.layoutCheckTick ||
                    processLoopTick-record.layoutCheckTick >= OW::kEntityDeadComponentRefreshMs)) {
                    std::array<uint64_t,6> componentLayout{}, linkLayout{};
                    uint32_t match = 0;
                    const bool read = componentHeader.Read(ComponentParent) &&
                        (ComponentParent == LinkParent ? (linkHeader=componentHeader, true) : linkHeader.Read(LinkParent)) &&
                        componentHeader.ReadComponentLayout(componentLayout) && linkHeader.ReadComponentLayout(linkLayout) &&
                        componentHeader.ReadParentOffset(OW::offset::Entity_MatchId, match);
                    layoutReadable = read;
                    if (read) {
                        matchChanged = record.matchIdValid && record.matchId != match;
                        layoutChanged = !record.layoutsKnown || componentLayout != record.componentLayout ||
                            linkLayout != record.linkLayout || matchChanged;
                        record.layoutCheckTick = processLoopTick;
                    }
                }
                if(bindingChanged || layoutChanged || (refreshDue && layoutReadable)) {
                    if(bindingChanged)++lifecycleStats.componentCacheInvalidateLinkChangeCount;
                    else ++lifecycleStats.componentCacheInvalidateIntervalCount;
                    if (bindingChanged || matchChanged) {
                        OW::RevokeEntityBodyObservation(record.entity.address, record.entity.actor_generation,
                            record.entity.body_authority_revision);
                        record.entity.body_valid = false;
                        record.entity.team_known = false;
                        record.confirmedTeam.known = false;
                    }
                    // Routine renewal retains the accepted hot cache while the
                    // replacement transaction is incomplete. Identity drift does not.
                    if(bindingChanged || layoutChanged)record.baseUpdateTick=0;
                    componentCacheHit=false;
                }
            }
