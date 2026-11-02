// Shared lexical step of entity_thread; requires its surrounding cycle state.
            // ---- Health ----
            {
            ScopedPipelinePhaseTimer phaseTimer(pipelineStats.phase.healthMs);
            if (entity.HealthBase) {
                if (refreshHealth) {
                    dmaFieldWindow.healthRequested++;
                    auto health_compo = hotReads.health;
                    bool healthRead = hotReads.HealthRead();
                    if (healthRead) {
                        dmaFieldWindow.healthScatterHit++;
                        pipelineStats.hotScatterSuccessCount++;
                    } else if (hotReads.healthRequested && hotReads.scatterOk) {
                        dmaFieldWindow.healthScatterPartial++;
                        pipelineStats.hotScatterPartialCount++;
                    }
                    if (!healthRead) {
                        dmaFieldWindow.healthFallback++;
                        pipelineStats.hotScatterFallbackReadCount++;
                        healthRead =
                            readHealthFields(entity.HealthBase, health_compo);
                    }
                    if (!healthRead) {
                        dmaFieldWindow.healthReadFail++;
                        requestTopologyRescan(processLoopTick);
                        if (!refreshHealthBaseAfterTransportFailure(entity, processLoopTick)) {

                            componentBaseCache.erase(ComponentParent);
                        }
                        processStats.healthBaseFail++;
                        processStats.healthReadFail++;
                        recordHealthFailureSample(entity, ComponentParent, LinkParent, false);
                        Diagnostics::RecordInvalidEntity();
                        continue;
                    }
                    OW::HealthPayloadSummary parsedHealth{};
                    const bool plausibleHealthLayout = OW::TryParseHealthPayload(health_compo, parsedHealth);
                    if (!plausibleHealthLayout) {
                        dmaFieldWindow.healthLayoutFail++;
                        requestTopologyRescan(processLoopTick);

                        componentBaseCache.erase(ComponentParent);
                        processStats.healthBaseFail++;
                        processStats.healthReadFail++;
                        recordHealthFailureSample(entity, ComponentParent, LinkParent, true);
                        Diagnostics::RecordInvalidEntity();
                        continue;
                    }
                    componentCache.playerHealth = parsedHealth.total;
                    componentCache.playerHealthMax = parsedHealth.total_max;
                    componentCache.minHealth = parsedHealth.health;
                    componentCache.maxHealth = parsedHealth.health_max;
                    componentCache.minArmorHealth = parsedHealth.armor;
                    componentCache.maxArmorHealth = parsedHealth.armor_max;
                    componentCache.minBarrierHealth = parsedHealth.barrier;
                    componentCache.maxBarrierHealth = parsedHealth.barrier_max;
                    componentCache.alive = componentCache.playerHealth > 0.f;
                    componentCache.imort = parsedHealth.isImmortal;
                    componentCache.barrprot = parsedHealth.isBarrierProjected;
                    componentCache.healthUpdateTick = processLoopTick;
                    componentCache.healthValid = true;

                }
                entity.PlayerHealth = componentCache.playerHealth;
                entity.PlayerHealthMax = componentCache.playerHealthMax;
                entity.MinHealth = componentCache.minHealth;
                entity.MaxHealth = componentCache.maxHealth;
                entity.MinArmorHealth = componentCache.minArmorHealth;
                entity.MaxArmorHealth = componentCache.maxArmorHealth;
                entity.MinBarrierHealth = componentCache.minBarrierHealth;
                entity.MaxBarrierHealth = componentCache.maxBarrierHealth;
                entity.Alive = componentCache.alive;
                entity.imort = componentCache.imort;
                entity.barrprot = componentCache.barrprot;
            } else {
                dmaFieldWindow.healthBaseMissing++;
                requestTopologyRescan(processLoopTick);
                processStats.healthBaseFail++;
                processStats.healthBaseMissing++;
                recordHealthFailureSample(entity, ComponentParent, LinkParent, false);
                Diagnostics::RecordInvalidEntity();
                continue;
            }
            }
