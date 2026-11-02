// Shared health refresh decision; no read is a distinct state.
            refreshHealth =
                entity.HealthBase &&
                (!componentCache.healthValid ||
                 processLoopTick - componentCache.healthUpdateTick >= OW::kEntityHealthIntervalMs);
