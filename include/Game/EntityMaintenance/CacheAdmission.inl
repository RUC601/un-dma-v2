// Check the allocation/layout as well as the parent address; live components can move.
            auto cacheIt=componentBaseCache.find(ComponentParent);
            bool componentCacheHit=cacheIt!=componentBaseCache.end();
            if(componentCacheHit) {
                auto& record=*cacheIt->second;
                const DWORD refreshInterval=record.alive?OW::kEntityLiveComponentRefreshMs:OW::kEntityDeadComponentRefreshMs;
                const bool bindingChanged=record.linkParent!=LinkParent;
                const bool refreshDue=!record.baseUpdateTick ||
                    processLoopTick-record.baseUpdateTick>=refreshInterval;
                bool layoutChanged = false;
                if (!bindingChanged && (!record.layoutCheckTick ||
                    processLoopTick-record.layoutCheckTick >= OW::kEntityDeadComponentRefreshMs)) {
                    OW::EntityHeaderSnapshot componentCheck{}, linkCheck{};
                    std::array<uint64_t,6> componentLayout{}, linkLayout{};
                    uint32_t match = 0;
                    const bool read = componentCheck.Read(ComponentParent) &&
                        (ComponentParent == LinkParent ? (linkCheck=componentCheck, true) : linkCheck.Read(LinkParent)) &&
                        componentCheck.ReadComponentLayout(componentLayout) && linkCheck.ReadComponentLayout(linkLayout) &&
                        componentCheck.ReadParentOffset(OW::offset::Entity_MatchId, match);
                    layoutChanged = !read || !record.layoutsKnown || componentLayout != record.componentLayout ||
                        linkLayout != record.linkLayout || (record.matchIdValid && record.matchId != match);
                    record.layoutCheckTick = processLoopTick;
                }
                if(bindingChanged || layoutChanged || refreshDue) {
                    if(bindingChanged)++lifecycleStats.componentCacheInvalidateLinkChangeCount;
                    else ++lifecycleStats.componentCacheInvalidateIntervalCount;
                    if (bindingChanged || layoutChanged) {
                        OW::RevokeEntityBodyObservation(record.entity.address, record.entity.actor_generation,
                            record.entity.body_authority_revision);
                        record.entity.body_valid = false;
                        record.entity.team_known = false;
                        record.confirmedTeam.known = false;
                    }
                    record.baseUpdateTick=0;
                    componentCacheHit=false;
                }
            }
