// Shared lexical step of entity_thread.
    DWORD lastTopologyRescanRequestTick = 0;
    DWORD lastRespawnTopologyRescanRequestTick = 0;
    auto requestTopologyRescan = [&](DWORD requestTick,
                                     bool bypassCooldown = false) -> bool {
        if (!coldTopologyScanEnabled)
            return false;
        if (!bypassCooldown && lastTopologyRescanRequestTick != 0 &&
            requestTick - lastTopologyRescanRequestTick <
                OW::kEntityTopologyRescanCooldownMs) {
            return false;
        }

        uint64_t requestCount = 0;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (OW::entity_topology_rescan_request_count !=
                    OW::entity_topology_rescan_completed_request_count ||
                (OW::entity_fast_scan_until_tick != 0 &&
                 requestTick < OW::entity_fast_scan_until_tick)) {
                return false;
            }
            requestCount = ++OW::entity_topology_rescan_request_count;
            OW::entity_fast_scan_until_tick =
                requestTick + OW::kEntityFastRescanWindowMs;
        }
        lastTopologyRescanRequestTick = requestTick;
        Diagnostics::Trace(
            "Entity topology rescan requested by processor: bypass_cooldown=%d request=%llu.",
            bypassCooldown ? 1 : 0,
            static_cast<unsigned long long>(requestCount));
        return true;
    };

