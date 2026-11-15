// Shared lexical step of entity_thread; requires its surrounding cycle state.
                ComponentBaseCache cache{};
                cache.linkParent = LinkParent;
                cache.baseUpdateTick = processLoopTick;
                cache.layoutCheckTick = processLoopTick;
                cache.layoutsKnown = componentSnapshot && linkSnapshot &&
                    componentSnapshot->ReadComponentLayout(cache.componentLayout) &&
                    linkSnapshot->ReadComponentLayout(cache.linkLayout);
                const bool resumedCn = cacheIt->second->componentRefresh.stage ==
                    OW::EntityMaintenance::ComponentRefresh::Stage::Ready;
                if(resumedCn) {
                    const auto& completed=cacheIt->second->componentRefresh;
                    cache.baseUpdateTick=completed.verifiedTick;
                    cache.layoutCheckTick=completed.verifiedTick;
                    const auto& values=completed.values;
                    cache.componentLayout=completed.component.Layout();cache.linkLayout=completed.link.Layout();
                    cache.layoutsKnown=true;
                    cache.health=values[0];cache.link=values[1];cache.team=values[2];cache.transform=values[3];
                    cache.velocity=values[4];cache.hero=values[5];cache.bone=values[6];cache.rotation=values[7];
                    cache.skill=values[8];cache.visibility=values[9];cache.angle=values[10];cache.enemyAngle=values[11];cache.stat=values[12];
                    cache.rotationLeaf=cache.rotation==cacheIt->second->rotation ? cacheIt->second->rotationLeaf : 0;
                } else {
                cache.health     = decryptBase(ComponentParent, OW::offset::Active().typeHealth, componentSnapshot);
                cache.link       = decryptBase(LinkParent, OW::offset::Active().typeLink, linkSnapshot);
                cache.team       = decryptBase(ComponentParent, OW::offset::Active().typeTeam, componentSnapshot);
                cache.transform  = decryptBase(ComponentParent, OW::TYPE_TRANSFORM, componentSnapshot);
                cache.velocity   = decryptBase(ComponentParent, OW::TYPE_VELOCITY, componentSnapshot);
                cache.hero       = decryptBase(LinkParent, OW::offset::Active().typeHeroId, linkSnapshot);
                cache.bone       = decryptBase(ComponentParent, OW::TYPE_BONE, componentSnapshot);
                cache.rotation   = decryptBase(ComponentParent, OW::offset::Active().typeRotation, componentSnapshot);
                cache.skill      = decryptBase(ComponentParent, OW::offset::Active().typeSkill, componentSnapshot);
                cache.visibility = decryptBase(LinkParent, OW::TYPE_P_VISIBILITY, linkSnapshot);
                cache.angle      = decryptBase(LinkParent, OW::offset::Active().typePlayerController, linkSnapshot);
                cache.enemyAngle = decryptBase(ComponentParent, OW::TYPE_ANGLE, componentSnapshot);
                cache.stat       = decryptBase(LinkParent, OW::TYPE_STAT, linkSnapshot);
                if (LinkParent != ComponentParent) {
                    if (!cache.health)
                        cache.health = decryptBase(LinkParent, OW::offset::Active().typeHealth, linkSnapshot, true);
                    if (!cache.team)
                        cache.team = decryptBase(LinkParent, OW::offset::Active().typeTeam, linkSnapshot, true);
                    if (!cache.transform)
                        cache.transform = decryptBase(LinkParent, OW::TYPE_TRANSFORM, linkSnapshot, true);
                    if (!cache.velocity)
                        cache.velocity = decryptBase(LinkParent, OW::TYPE_VELOCITY, linkSnapshot, true);
                    if (!cache.bone)
                        cache.bone = decryptBase(LinkParent, OW::TYPE_BONE, linkSnapshot, true);
                    if (!cache.rotation)
                        cache.rotation = decryptBase(LinkParent, OW::offset::Active().typeRotation, linkSnapshot, true);
                    if (!cache.skill)
                        cache.skill = decryptBase(LinkParent, OW::offset::Active().typeSkill, linkSnapshot, true);
                    if (!cache.enemyAngle)
                        cache.enemyAngle = decryptBase(LinkParent, OW::TYPE_ANGLE, linkSnapshot, true);
                }
                if (criticalBatchFusionActive && cache.rotation) {
                    cache.rotationLeaf = SDK->RPM<uint64_t>(
                        cache.rotation + offset::RotationBase_Sub1);
                }
                }
                auto& record=componentBaseCache[ComponentParent];
                if(!record)record=std::make_shared<RosterEntry>();
                cache.matchIdValid = componentSnapshot &&
                    componentSnapshot->ReadParentOffset(OW::offset::Entity_MatchId, cache.matchId);
                if(resumedCn) {
                    cache.matchId=record->componentRefresh.component.At<uint32_t>(OW::offset::Entity_MatchId);
                    cache.matchIdValid=cache.matchId!=0;
                }
                const auto bases = [](const ComponentBaseCache& value) {
                    return std::array{value.health, value.link, value.team, value.transform,
                        value.velocity, value.hero, value.bone, value.rotation, value.rotationLeaf,
                        value.skill, value.visibility, value.angle, value.enemyAngle, value.stat};
                };
                const bool sameComponents = cache.layoutsKnown && record->layoutsKnown &&
                    cache.linkParent == record->linkParent && cache.matchIdValid && record->matchIdValid &&
                    cache.matchId == record->matchId && bases(cache) == bases(*record);
                if (sameComponents) {
                    // Renew addresses without discarding accepted samples or advancing their timestamps.
                    record->componentLayout = cache.componentLayout;
                    record->linkLayout = cache.linkLayout;
                    record->baseUpdateTick = cache.baseUpdateTick;
                    record->layoutCheckTick = cache.layoutCheckTick;
                } else {
                    // Keep the pair-bound team lock through moved or unreadable components.
                    // A validated paired body may reuse it; a new pair needs an exact team read.
                    cache.confirmedTeam = record->confirmedTeam;
                    cache.confirmedTeam.known = false;
                    if (record->entity.body_valid && (cache.health != record->health || cache.velocity != record->velocity ||
                        cache.hero != record->hero || cache.team != record->team || cache.linkParent != record->linkParent)) {
                        OW::RevokeEntityBodyObservation(record->entity.address, record->entity.actor_generation,
                            record->entity.body_authority_revision);
                        record->entity.body_valid = false;
                    }
                    static_cast<ComponentBaseCache&>(*record)=std::move(cache);
                }
                cacheIt=componentBaseCache.find(ComponentParent);
                record->componentRefresh={};
