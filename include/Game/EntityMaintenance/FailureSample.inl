// Shared lexical step of entity_thread.
        auto recordHealthFailureSample = [&](const OW::c_entity& failedEntity,
                                             uint64_t componentParent,
                                             uint64_t linkParent,
                                             bool healthReadOk) {
            if (processStats.sampleHealthFailComponentParent)
                return;
            processStats.sampleHealthFailComponentParent = componentParent;
            processStats.sampleHealthFailLinkParent = linkParent;
            processStats.sampleHealthFailHealthBase = failedEntity.HealthBase;
            processStats.sampleHealthFailLinkBase = failedEntity.LinkBase;
            processStats.sampleHealthFailVelocityBase = failedEntity.VelocityBase;
            processStats.sampleHealthFailHeroBase = failedEntity.HeroBase;
            processStats.sampleHealthFailTeamBase = failedEntity.TeamBase;
            processStats.sampleHealthFailBoneBase = failedEntity.BoneBase;
            processStats.sampleHealthFailReadOk = healthReadOk ? 1 : 0;
        };
