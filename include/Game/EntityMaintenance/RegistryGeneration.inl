// Entity-owner generation transition; no memory I/O.
        if(OW::DynamicEntityRegistryEnabled() && processRegistryGeneration!=processRegistry.generation) {
            processRegistryGeneration=processRegistry.generation;
            componentBaseCache.clear();dynamicEntityCache.clear();discoveryAttemptTicks.clear();

            latestCompleteActorMembers.clear();completeActorMembershipKnown=false;
            cycleMemberPairs.clear();cycleDiscoveries={};
            for(auto& [key,entry]:entityRoster)sanitizeStaleRosterEntity(entry->entity);
            {
                std::lock_guard<std::mutex> rawLock(OW::raw_scan_mutex);
                std::lock_guard<std::mutex> bodyLock(OW::entityBodyValidityMutex);
                OW::ClearEntityBodyAuthoritiesLocked();OW::entityBodyDiscoveries.clear();
                // A producer may already have committed this generation. Preserve its original evidence clocks.
                if(OW::entity_discoveries.connection==connectionEpoch &&
                    OW::entity_discoveries.registry==processRegistry.generation &&
                    OW::entity_discoveries.scene==processSceneGeneration)
                    OW::entityBodyDiscoveries=OW::entity_discoveries.actors;
            }
        }
