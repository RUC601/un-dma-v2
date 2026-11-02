// Shared lexical step of entity_thread.
                if (hotReads.scatterExecuted) {
                    dmaFieldWindow.hotScatterExecute++;
                    if (!hotReads.scatterOk)
                        dmaFieldWindow.hotScatterExecuteFail++;
                }

                velo_compo = hotReads.velocity;
                velocityRead = hotReads.VelocityRead();
                if (entity.VelocityBase) {
                    dmaFieldWindow.velocityRequested++;
                    if (velocityRead) {
                        dmaFieldWindow.velocityScatterHit++;
                        pipelineStats.hotScatterSuccessCount++;
                    } else if (hotReads.velocityRequested && hotReads.scatterOk) {
                        dmaFieldWindow.velocityScatterPartial++;
                        pipelineStats.hotScatterPartialCount++;
                    }
                }
                if (entity.VelocityBase && !velocityRead) {
                    dmaFieldWindow.velocityFallback++;
                    pipelineStats.hotScatterFallbackReadCount++;
                    entity.read_started_tick_ms = entityAdmissionNow();
                    velocityRead =
                        SDK->read_range(entity.VelocityBase + 0x50, &velo_compo.velocity, 12) &&
                        SDK->read_range(entity.VelocityBase + 0x200, &velo_compo.location, 12);
                    entity.read_completed_tick_ms = entityAdmissionNow();
                    if (!velocityRead)
                        dmaFieldWindow.velocityReadFail++;
                }
