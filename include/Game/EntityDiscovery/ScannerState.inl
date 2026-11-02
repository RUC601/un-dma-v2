// Scanner owner local state and diagnostics, shared verbatim with offline driver.
    const bool scanLatestWinsEnabled = OW::ScanLatestWinsEnabled();
    const bool coldTopologyScanEnabled = OW::ColdTopologyScanEnabled();
    Diagnostics::Info("Entity scan thread started. scan_interval_ms=%lu empty_interval_ms=%lu latest_wins=%d cold_topology_scan=%d adaptive_topology_rescan=%d.",
        static_cast<unsigned long>(OW::kEntityScanIntervalMs),
        static_cast<unsigned long>(OW::kEntityEmptyScanIntervalMs),
        scanLatestWinsEnabled ? 1 : 0,
        coldTopologyScanEnabled ? 1 : 0,
        coldTopologyScanEnabled ? 1 : 0);
    OW::SetEntityScanNextDueTick(static_cast<DWORD>(OW::EntityRegistryNowMs()));
    size_t lastLoggedScanCount = static_cast<size_t>(-1);
    DWORD lastScanLogTick = 0;
    DWORD lastScanTick = 0;
    DWORD lastScanCycleTick = 0;
    double entityScanHz = 0.0;
    std::unordered_map<uint64_t, std::pair<std::pair<uint64_t, uint64_t>, DWORD>> cnStableScanPairs{};
    uint64_t scanOwnerEpoch = 0;
    uint64_t scanOwnerScene = 0;
    uint64_t scanRegistryGeneration=0;
    auto scanOwnerProfile = OW::offset::ActiveProfile();
    uint64_t scanLoopCount = 0;
    uint64_t scanDueCount = 0;
    uint64_t scanSkipPendingCount = 0;
    uint64_t scanSkipNotDueCount = 0;
    uint64_t scanSkipStableTopologyCount = 0;
    uint64_t scanStartedCount = 0;
    uint64_t scanCompletedCount = 0;
    uint64_t ownerScanCompletedCount = 0;
    uint64_t scanFailedCount = 0;
    uint64_t scanPublishAttemptCount = 0;
    uint64_t scanPublishSuccessCount = 0;
    uint64_t scanOverwrittenCount = 0;
    uint64_t scanGeneration = 0;
    uint64_t scanTopologyRescanRequestCount = 0;
    uint64_t scanTopologyCountProbeCount = 0;
    uint64_t scanTopologyCountProbeChangeCount = 0;
    size_t scanTopologyCandidateCount = 0;
    uint64_t previousScanLoopCount = 0;
    uint64_t previousScanDueCount = 0;
    uint64_t previousScanStartedCount = 0;
    uint64_t previousScanCompletedCount = 0;
    DWORD scanRateTick = static_cast<DWORD>(OW::EntityRegistryNowMs());
    DWORD scanPendingSinceTick = 0;
    DWORD lastScanSuccessTick = 0;
    uint64_t scanPendingGeneration = 0;
    double scanLoopHz = 0.0;
    double scanDueHz = 0.0;
    double scanStartedHz = 0.0;
    double scanCompletedHz = 0.0;
    double scanGetOwEntitiesMs = 0.0;
    size_t scanResultRawCount = 0;
    double scanMaxGetOwEntitiesMs = 0.0;
    uint64_t scanMaxGetOwEntitiesGeneration = 0;
    Diagnostics::EntityScanDetailStats scanMaxGetOwEntitiesDetail{};
    DWORD lastTopologyCountProbeTick = 0;
    std::vector<uint64_t> lastTopologyMembers;
    uint64_t lastTopologyRoot=0;
    bool hasTopologyCandidateBaseline = false;

    auto readDwordEnv = [](const char* name, DWORD fallback, DWORD minValue, DWORD maxValue) {
        char buffer[32] = {};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return fallback;

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(buffer, &end, 10);
        if (end == buffer || (end && *end != '\0'))
            return fallback;

        const DWORD value = static_cast<DWORD>(parsed);
        return std::clamp(value, minValue, maxValue);
    };
    const DWORD topologyCountProbeIntervalMs =
        readDwordEnv("UN_DMA_TOPOLOGY_COUNT_PROBE_MS", 5000, 0, 60000);
    const DWORD topologyCountProbeDeltaThreshold =
        readDwordEnv("UN_DMA_TOPOLOGY_COUNT_PROBE_DELTA", 128, 1, 4096);

    auto publishScanPipelineStats = [&]() {
        const DWORD statsNow = static_cast<DWORD>(OW::EntityRegistryNowMs());
        const DWORD elapsed = statsNow - scanRateTick;
        if (elapsed >= 1000) {
            scanLoopHz = static_cast<double>(scanLoopCount - previousScanLoopCount) * 1000.0 /
                static_cast<double>(elapsed);
            scanDueHz = static_cast<double>(scanDueCount - previousScanDueCount) * 1000.0 /
                static_cast<double>(elapsed);
            scanStartedHz = static_cast<double>(scanStartedCount - previousScanStartedCount) * 1000.0 /
                static_cast<double>(elapsed);
            scanCompletedHz = static_cast<double>(scanCompletedCount - previousScanCompletedCount) * 1000.0 /
                static_cast<double>(elapsed);
            previousScanLoopCount = scanLoopCount;
            previousScanDueCount = scanDueCount;
            previousScanStartedCount = scanStartedCount;
            previousScanCompletedCount = scanCompletedCount;
            scanRateTick = statsNow;
        }

        Diagnostics::EntityPipelineScanStats stats{};
        stats.loopCount = scanLoopCount;
        stats.dueCount = scanDueCount;
        stats.skipPendingCount = scanSkipPendingCount;
        stats.skipNotDueCount = scanSkipNotDueCount;
        stats.skipStableTopologyCount = scanSkipStableTopologyCount;
        stats.startedCount = scanStartedCount;
        stats.completedCount = scanCompletedCount;
        stats.failedCount = scanFailedCount;
        stats.publishAttemptCount = scanPublishAttemptCount;
        stats.publishSuccessCount = scanPublishSuccessCount;
        stats.overwrittenCount = scanOverwrittenCount;
        stats.generation = scanGeneration;
        stats.loopHz = scanLoopHz;
        stats.dueHz = scanDueHz;
        stats.startedHz = scanStartedHz;
        stats.completedHz = scanCompletedHz;
        stats.getOwEntitiesMs = scanGetOwEntitiesMs;
        stats.maxGetOwEntitiesMs = scanMaxGetOwEntitiesMs;
        stats.maxGetOwEntitiesGeneration = scanMaxGetOwEntitiesGeneration;
        stats.maxGetOwEntitiesRecords = scanMaxGetOwEntitiesDetail.records;
        stats.maxGetOwEntitiesPairs = scanMaxGetOwEntitiesDetail.totalPairs;
        stats.maxGetOwEntitiesRecordBuildMs =
            scanMaxGetOwEntitiesDetail.scanRecordBuildMs;
        stats.maxGetOwEntitiesMatchLinkMs =
            scanMaxGetOwEntitiesDetail.scanMatchLinkMs;
        stats.maxGetOwEntitiesTargetMapMs =
            scanMaxGetOwEntitiesDetail.scanCnTargetMapMs;
        stats.maxGetOwEntitiesComponentValidationMs =
            scanMaxGetOwEntitiesDetail.scanComponentOnlyValidationMs;
        stats.maxGetOwEntitiesDmaReadsDelta =
            scanMaxGetOwEntitiesDetail.scanDmaReadsDelta;
        stats.maxGetOwEntitiesDmaFailDelta =
            scanMaxGetOwEntitiesDetail.scanDmaFailDelta;
        stats.maxGetOwEntitiesDmaRangeDiagEnabled =
            scanMaxGetOwEntitiesDetail.scanDmaRangeDiagEnabled;
        stats.maxGetOwEntitiesDmaRangeReads =
            scanMaxGetOwEntitiesDetail.scanDmaRangeReads;
        stats.maxGetOwEntitiesDmaRangeFailed =
            scanMaxGetOwEntitiesDetail.scanDmaRangeFailed;
        stats.maxGetOwEntitiesDmaRangeMaxLatencyUs =
            scanMaxGetOwEntitiesDetail.scanDmaRangeMaxLatencyUs;
        stats.maxGetOwEntitiesDmaRangeMaxCallsite =
            scanMaxGetOwEntitiesDetail.scanDmaRangeMaxCallsite;
        stats.maxGetOwEntitiesDmaRangeScannerReads =
            scanMaxGetOwEntitiesDetail.scanDmaRangeScannerReads;
        stats.maxGetOwEntitiesDmaRangeScannerMaxLatencyUs =
            scanMaxGetOwEntitiesDetail.scanDmaRangeScannerMaxLatencyUs;
        stats.maxGetOwEntitiesDmaRangeScannerMaxCallsite =
            scanMaxGetOwEntitiesDetail.scanDmaRangeScannerMaxCallsite;
        stats.maxGetOwEntitiesDmaRangeForeignReads =
            scanMaxGetOwEntitiesDetail.scanDmaRangeForeignReads;
        stats.maxGetOwEntitiesDmaRangeForeignMaxLatencyUs =
            scanMaxGetOwEntitiesDetail.scanDmaRangeForeignMaxLatencyUs;
        stats.maxGetOwEntitiesDmaRangeForeignMaxCallsite =
            scanMaxGetOwEntitiesDetail.scanDmaRangeForeignMaxCallsite;
        stats.maxGetOwEntitiesDmaRangeRootMaxUs =
            scanMaxGetOwEntitiesDetail.scanDmaRangeRootMaxUs;
        stats.maxGetOwEntitiesDmaRangeListReadMaxUs =
            scanMaxGetOwEntitiesDetail.scanDmaRangeListReadMaxUs;
        stats.maxGetOwEntitiesDmaRangeRecordHeaderMaxUs =
            scanMaxGetOwEntitiesDetail.scanDmaRangeRecordHeaderMaxUs;
        stats.maxGetOwEntitiesDmaRangeRecordPoolIdMaxUs =
            scanMaxGetOwEntitiesDetail.scanDmaRangeRecordPoolIdMaxUs;
        stats.maxGetOwEntitiesDmaRangeTargetMapMaxUs =
            scanMaxGetOwEntitiesDetail.scanDmaRangeTargetMapMaxUs;
        stats.maxGetOwEntitiesDmaRangeComponentValidationMaxUs =
            scanMaxGetOwEntitiesDetail.scanDmaRangeComponentValidationMaxUs;
        stats.maxGetOwEntitiesDmaRangeViewMatrixMaxUs =
            scanMaxGetOwEntitiesDetail.scanDmaRangeViewMatrixMaxUs;
        stats.maxGetOwEntitiesRootCacheHitCount =
            scanMaxGetOwEntitiesDetail.cnEntityListRootCacheHitCount;
        stats.maxGetOwEntitiesRootCacheReadCount =
            scanMaxGetOwEntitiesDetail.cnEntityListRootCacheReadCount;
        stats.maxGetOwEntitiesRootCacheStoreCount =
            scanMaxGetOwEntitiesDetail.cnEntityListRootCacheStoreCount;
        stats.maxGetOwEntitiesRootCacheExpiredCount =
            scanMaxGetOwEntitiesDetail.cnEntityListRootCacheExpiredCount;
        stats.maxGetOwEntitiesRootCacheStaleHitCount =
            scanMaxGetOwEntitiesDetail.cnEntityListRootCacheStaleHitCount;
        stats.maxGetOwEntitiesListReadSkippedCount =
            scanMaxGetOwEntitiesDetail.listReadSkippedCount;
        stats.maxGetOwEntitiesListReadNegativeCacheHitCount =
            scanMaxGetOwEntitiesDetail.cnEntityListReadNegativeCacheHitCount;
        stats.maxGetOwEntitiesListReadNegativeCacheStoreCount =
            scanMaxGetOwEntitiesDetail.cnEntityListReadNegativeCacheStoreCount;
        stats.maxGetOwEntitiesListReadNegativeCacheExpiredCount =
            scanMaxGetOwEntitiesDetail.cnEntityListReadNegativeCacheExpiredCount;
        stats.maxGetOwEntitiesListReadNegativeCacheStaleHitCount =
            scanMaxGetOwEntitiesDetail.cnEntityListReadNegativeCacheStaleHitCount;
        stats.maxGetOwEntitiesListReadCacheHitCount =
            scanMaxGetOwEntitiesDetail.cnEntityListReadCacheHitCount;
        stats.maxGetOwEntitiesListReadCacheStoreCount =
            scanMaxGetOwEntitiesDetail.cnEntityListReadCacheStoreCount;
        stats.maxGetOwEntitiesListReadCacheExpiredCount =
            scanMaxGetOwEntitiesDetail.cnEntityListReadCacheExpiredCount;
        stats.maxGetOwEntitiesListReadCacheStaleHitCount =
            scanMaxGetOwEntitiesDetail.cnEntityListReadCacheStaleHitCount;
        stats.maxGetOwEntitiesRecordMatchIdDirectReadCount =
            scanMaxGetOwEntitiesDetail.recordMatchIdDirectReadCount;
        stats.maxGetOwEntitiesRecordMatchIdDirectZeroCount =
            scanMaxGetOwEntitiesDetail.recordMatchIdDirectZeroCount;
        stats.maxGetOwEntitiesRecordMatchIdHeaderHitCount =
            scanMaxGetOwEntitiesDetail.recordMatchIdHeaderHitCount;
        stats.maxGetOwEntitiesRecordMatchIdHeaderMissCount =
            scanMaxGetOwEntitiesDetail.recordMatchIdHeaderMissCount;
        stats.maxGetOwEntitiesRecordMatchIdHeaderMatchCount =
            scanMaxGetOwEntitiesDetail.recordMatchIdHeaderMatchCount;
        stats.maxGetOwEntitiesRecordMatchIdHeaderMismatchCount =
            scanMaxGetOwEntitiesDetail.recordMatchIdHeaderMismatchCount;
        stats.maxGetOwEntitiesRecordMatchIdHeaderUseCount =
            scanMaxGetOwEntitiesDetail.recordMatchIdHeaderUseCount;
        stats.maxGetOwEntitiesPersistentRefreshCount =
            scanMaxGetOwEntitiesDetail.cnMapCandidatePersistentCacheRefreshCount;
        stats.maxGetOwEntitiesPersistentStaleHitCount =
            scanMaxGetOwEntitiesDetail.cnMapCandidatePersistentCacheStaleHitCount;
        stats.resultRawCount = scanResultRawCount;
        stats.pendingAgeMs = scanPendingSinceTick != 0 ? statsNow - scanPendingSinceTick : 0;
        stats.lastSuccessAgeMs = lastScanSuccessTick != 0 ? statsNow - lastScanSuccessTick : 0;
        stats.coldTopologyScanEnabled = coldTopologyScanEnabled;
        stats.topologyRescanRequestCount = scanTopologyRescanRequestCount;
        stats.topologyCountProbeCount = scanTopologyCountProbeCount;
        stats.topologyCountProbeChangeCount = scanTopologyCountProbeChangeCount;
        stats.topologyCandidateCount = scanTopologyCandidateCount;
        Diagnostics::SetEntityPipelineScanStats(stats);
    };

    auto scanPairKey = [](const std::pair<uint64_t, uint64_t>& pair) {
        const uint64_t first = pair.first >> 4;
        const uint64_t second = pair.second >> 4;
        return first ^ (second << 17) ^ (second >> 13);
    };

    auto appendScanPairUnique = [&](std::vector<std::pair<uint64_t, uint64_t>>& pairs,
                                    const std::pair<uint64_t, uint64_t>& pair) {
        if (!pair.first || !pair.second)
            return false;
        if (std::find(pairs.begin(), pairs.end(), pair) != pairs.end())
            return false;
        pairs.push_back(pair);
        return true;
    };

    auto updateScanPendingAge = [&](DWORD observedNow) {
        bool pending = false;
        uint64_t pendingGeneration = 0;
        if (scanLatestWinsEnabled) {
            std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
            pending = OW::latest_raw_scan_snapshot.valid &&
                OW::latest_raw_scan_snapshot.generation !=
                    OW::last_consumed_raw_scan_generation;
            pendingGeneration = pending ? OW::latest_raw_scan_snapshot.generation : 0;
        } else {
            std::lock_guard<std::mutex> lock(g_mutex);
            pending = OW::abletotread != 0;
            pendingGeneration = pending ? scanGeneration : 0;
        }

        if (pending) {
            if (scanPendingSinceTick == 0 || scanPendingGeneration != pendingGeneration) {
                scanPendingSinceTick = observedNow;
                scanPendingGeneration = pendingGeneration;
            }
        } else {
            scanPendingSinceTick = 0;
            scanPendingGeneration = 0;
        }
    };

