// Requests point into owner-thread buffers until ExecuteReadScatter clears them.
        auto recordHotScatterPrepare = [&](uint64_t address, size_t size, bool prepared) {
            ++pipelineStats.hotScatterPrepareRequestedCount;
            pipelineStats.hotScatterBytesRequested += size;
            if (prepared) ++pipelineStats.hotScatterPrepareSuccessCount;
            else ++pipelineStats.hotScatterPrepareFailCount;
            hotScatterPages.insert(address & kScatterPageMask);
            hotScatterPages.insert((address + size - 1) & kScatterPageMask);
            pipelineStats.hotScatterEstimatedUniquePages = hotScatterPages.size();
        };
        auto prepareHotFields = [&](VMMDLL_SCATTER_HANDLE handle, EntityHotFieldReads& reads,
                uint64_t velocityBase, bool readVelocity, uint64_t healthBase, bool readHealth,
                uint64_t heroBase, bool readHero, uint64_t visibilityAddress, bool readVisibility) {
            if (!handle) return false;
            auto request = [&](uint64_t at, void* destination, size_t size, DWORD& bytes) {
                ++pipelineStats.hotScatterRequestedCount;
                const bool ok = mem.AddScatterReadRequest(handle, at, destination, size, &bytes);
                recordHotScatterPrepare(at, size, ok);
            };
            if (readVelocity && velocityBase) {
                reads.velocityRequested = true;
                request(velocityBase + 0x50, &reads.velocity.velocity, 12, reads.velocityBytes[0]);
                request(velocityBase + 0x200, &reads.velocity.location, 12, reads.velocityBytes[1]);
                request(velocityBase + 0x8B0, &reads.velocity.bonedata, 8, reads.velocityBytes[2]);
            }
            if (readHealth && healthBase) {
                reads.healthRequested = true;
                const bool cn = offset::Active().componentTransform == offset::ComponentTransformMode::Cn154504;
                request(healthBase + 0xDC, reads.health.vital.data(), 32, reads.healthBytes[0]);
                request(healthBase + (cn ? 0x25C : 0x21C), &reads.health.armor, 8, reads.healthBytes[1]);
                request(healthBase + (cn ? 0x3DC : 0x35C), &reads.health.barrier, 8, reads.healthBytes[2]);
                request(healthBase + (cn ? 0x568 : 0x4A8), reads.health.flags.data(), 2, reads.healthBytes[3]);
            }
            if (readHero && heroBase) {
                reads.heroRequested = true;
                request(heroBase + 0xD0, &reads.hero.heroid, 8, reads.heroBytes);
            }
            if (readVisibility && visibilityAddress) {
                reads.visibilityRequested = true;
                request(visibilityAddress, &reads.visibilityRaw, 8, reads.visibilityBytes);
            }
            return reads.velocityRequested || reads.healthRequested || reads.heroRequested || reads.visibilityRequested;
        };
        auto countHotFieldRequests = [](const EntityHotFieldReads& reads) {
            return size_t(reads.velocityRequested) * 3 + size_t(reads.healthRequested) * 4 +
                size_t(reads.heroRequested) + size_t(reads.visibilityRequested);
        };
        auto recordHotScatterReadResults = [&](const EntityHotFieldReads& reads) {
            auto field = [&](DWORD actual, DWORD expected) {
                pipelineStats.hotScatterBytesRead += actual;
                if (actual != expected) ++pipelineStats.hotScatterShortReadCount;
            };
            if (reads.velocityRequested) { field(reads.velocityBytes[0],12); field(reads.velocityBytes[1],12); field(reads.velocityBytes[2],8); }
            if (reads.healthRequested) { field(reads.healthBytes[0],32); field(reads.healthBytes[1],8); field(reads.healthBytes[2],8); field(reads.healthBytes[3],2); }
            if (reads.heroRequested) field(reads.heroBytes,8);
            if (reads.visibilityRequested) field(reads.visibilityBytes,8);
        };
        auto readHotFields = [&](VMMDLL_SCATTER_HANDLE handle, uint64_t velocityBase, bool readVelocity,
                uint64_t healthBase, bool readHealth, uint64_t heroBase, bool readHero,
                uint64_t visibilityAddress, bool readVisibility) {
            EntityHotFieldReads reads{};
            if (!prepareHotFields(handle, reads, velocityBase, readVelocity, healthBase, readHealth,
                    heroBase, readHero, visibilityAddress, readVisibility)) return reads;
            reads.scatterExecuted = true;
            {
                ScopedPipelinePhaseTimer timer(pipelineStats.phase.hotScatterExecuteMs);
                reads.scatterOk = mem.ExecuteReadScatter(handle);
            }
            ++pipelineStats.hotScatterExecuteCount;
            if (!reads.scatterOk) ++pipelineStats.hotScatterExecuteFailCount;
            recordHotScatterReadResults(reads);
            return reads;
        };
        auto readHealthFields = [&](uint64_t base, OW::health_fields_t& health) {
            const bool cn = offset::Active().componentTransform == offset::ComponentTransformMode::Cn154504;
            return SDK->read_range(base+0xDC,health.vital.data(),32) &&
                SDK->read_range(base+(cn?0x25C:0x21C),&health.armor,8) &&
                SDK->read_range(base+(cn?0x3DC:0x35C),&health.barrier,8) &&
                SDK->read_range(base+(cn?0x568:0x4A8),health.flags.data(),2);
        };
