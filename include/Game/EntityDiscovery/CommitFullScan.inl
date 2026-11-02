// Full scanner result commit boundary; generator remains get_ow_entities.
                        std::lock_guard<std::mutex> runningLock(OW::runtimeWorkerWaitMutex);
                        std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
                        if (OW::RuntimeWorkersRunning() && OW::ProcessConnection::IsConnected() &&
                            OW::EntitySceneGeneration()==scanScene &&
                            OW::ProcessConnection::ConnectionEpoch() == scanConnectionEpoch &&
                            OW::offset::ActiveProfile() == scanRuntimeProfile && OW::AcceptEntityRegistry(registry)) {
                            // A newer topology observation makes this an older
                            // partial audit; it cannot retire or overwrite new discoveries.
                            if(!OW::EntityRosterPolicy::ScanCanRetire(snapshot.complete,scanTopologyRevision,OW::observed_topology_revision)) {
                                snapshot.complete=false;
                            }
                            OW::MergeRecentRawDiscovery(snapshot);
                            snapshot.generation=++OW::raw_scan_publication_sequence;
                            if (OW::latest_raw_scan_snapshot.valid &&
                                OW::latest_raw_scan_snapshot.generation !=
                                    OW::last_consumed_raw_scan_generation) {
                                ++scanOverwrittenCount;
                            }
                            OW::latest_raw_scan_snapshot = std::move(snapshot);
                            published = true;
                            scanCommitted=OW::latest_raw_scan_snapshot.complete;
                        }
