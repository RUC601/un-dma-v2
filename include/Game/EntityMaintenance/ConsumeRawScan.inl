// Original raw handoff consumption, called only by the entity owner.
            if (scanLatestWinsEnabled) {
                std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
                if (OW::latest_raw_scan_snapshot.valid &&
                    OW::latest_raw_scan_snapshot.scene_generation==processSceneGeneration &&
                    OW::latest_raw_scan_snapshot.connection_epoch==connectionEpoch &&
                    OW::AcceptEntityRegistry(OW::latest_raw_scan_snapshot.registry) &&
                    OW::latest_raw_scan_snapshot.generation !=
                        OW::last_consumed_raw_scan_generation) {
                    consumedLatestRawEntities = OW::latest_raw_scan_snapshot.raw_entities;
                    processRegistry=OW::latest_raw_scan_snapshot.registry;
                    consumedActualMembers=OW::latest_raw_scan_snapshot.actual_members;
                    consumedScanSource=OW::latest_raw_scan_snapshot.source;
                    completeActorMembershipKnown=OW::latest_raw_scan_snapshot.membership_known;
                    cycleMemberPairs=OW::latest_raw_scan_snapshot.membership_entities;
                    latestCompleteActorMembers.clear();
                    for(const auto& pair:OW::latest_raw_scan_snapshot.membership_entities)
                        latestCompleteActorMembers.insert(pair.first);
                    OW::last_consumed_raw_scan_generation =
                        OW::latest_raw_scan_snapshot.generation;
                    consumedLatestRawScan = true;
                    consumedRawScanThisCycle = true;
                    consumedCompleteScan = OW::latest_raw_scan_snapshot.complete;
                }
            } else {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (OW::abletotread) {
                    if(OW::ow_entities_scene==processSceneGeneration && OW::AcceptEntityRegistry(OW::ow_entities_scan_registry)) {
                        OW::ow_entities = OW::ow_entities_scan;
                        OW::ow_entities_registry=OW::ow_entities_scan_registry;
                        consumedCompleteScan=OW::ow_entities_scan_complete;
                        consumedRawScanThisCycle = true;
                    }
                    OW::abletotread = 0;
                }
            }
