// Shared hot batch health validity/application boundary.
            if(item.reads.healthRequested) {
                if(!item.reads.HealthRead()){rejectHot(9);continue;}
                OW::HealthPayloadSummary hp{};
                if(!OW::TryParseHealthPayload(item.reads.health,hp)){rejectHot(10);continue;}
                const float health=hp.total;
                const float maximum=hp.total_max;
                cached.playerHealth=health;cached.playerHealthMax=maximum;cached.alive=health>0;
                cached.healthValid=true;cached.healthUpdateTick=processLoopTick;
                cached.imort=hp.isImmortal;cached.barrprot=hp.isBarrierProjected;
                next.PlayerHealth=health;next.PlayerHealthMax=maximum;next.Alive=cached.alive;
                next.imort=cached.imort;next.barrprot=cached.barrprot;
                cached.minHealth=hp.health;cached.maxHealth=hp.health_max;
                cached.minArmorHealth=hp.armor;cached.maxArmorHealth=hp.armor_max;
                cached.minBarrierHealth=hp.barrier;cached.maxBarrierHealth=hp.barrier_max;
                next.MinHealth=hp.health;next.MaxHealth=hp.health_max;
                next.MinArmorHealth=hp.armor;next.MaxArmorHealth=hp.armor_max;
                next.MinBarrierHealth=hp.barrier;next.MaxBarrierHealth=hp.barrier_max;
            }
