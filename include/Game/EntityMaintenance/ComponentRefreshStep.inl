                const bool resumableCn = OW::offset::Active().componentTransform ==
                    OW::offset::ComponentTransformMode::Cn155284;
                if(resumableCn) {
                    auto& pending=cacheIt->second->componentRefresh;
                    const auto currentIdentity=[&] {
                        uint64_t revision=0,absence=0;
                        {
                            std::lock_guard<std::mutex> lock(OW::entityBodyValidityMutex);
                            if(const auto it=OW::entityBodyAuthorities.find(ComponentParent);it!=OW::entityBodyAuthorities.end())
                                revision=it->second.revision;
                            if(const auto it=OW::entityBodyAbsenceRevisions.find(ComponentParent);it!=OW::entityBodyAbsenceRevisions.end())
                                absence=it->second;
                        }
                        return OW::EntityMaintenance::RefreshIdentity{{ComponentParent,LinkParent,
                            OW::ProcessConnection::ConnectionEpoch(),OW::EntitySceneGeneration(),processRegistry.generation,
                            static_cast<uint64_t>(OW::offset::ActiveProfile()),SDK->dwGameBase.load(),
                            cacheIt->second->entity.actor_generation,revision,absence}};
                    };
                    if(!OW::AcceptEntityRegistry(processRegistry)) { pending={};continue; }
                    if(!pending.Active()) {
                        OW::EntityMaintenance::RefreshHeader body{},controller{};
                        if(!componentSnapshot || !linkSnapshot ||
                            !componentSnapshot->range.ReadAddress(ComponentParent+OW::kEntityHeaderSnapshotOffset,body.bytes) ||
                            !linkSnapshot->range.ReadAddress(LinkParent+OW::kEntityHeaderSnapshotOffset,controller.bytes))continue;
                        const auto& profile=OW::offset::Active();
                        const std::array<uint32_t,13> types{profile.typeHealth,profile.typeLink,profile.typeTeam,
                            OW::TYPE_TRANSFORM,OW::TYPE_VELOCITY,profile.typeHeroId,OW::TYPE_BONE,profile.typeRotation,
                            profile.typeSkill,OW::TYPE_P_VISIBILITY,profile.typePlayerController,OW::TYPE_ANGLE,OW::TYPE_STAT};
                        pending.Begin(currentIdentity(),body,controller,types,
                            SDK->dwGameBase+profile.componentKeyByteRva,SDK->dwGameBase+profile.componentKeySecondByteRva,
                            OW::offset::Entity_MatchId,GetTickCount());
                        // Header/classification is one resumable visit. Never append
                        // all component reads to the same already-expensive visit.
                        continue;
                    }
                    if(!maintenanceBudget.CanStart(RuntimeTrace::NowUs()))continue;
                    auto exactBatch=[&](const std::vector<OW::EntityMaintenance::RefreshRequest>& requests) {
                        struct ScatterOwner {
                            VMMDLL_SCATTER_HANDLE handle=mem.CreateScatterHandle();
                            ~ScatterOwner(){if(handle)mem.CloseScatterHandle(handle);}
                        } owner;
                        if(!owner.handle)return false;
                        // DWORD and uint32_t can be different C++ types on Windows.
                        std::vector<DWORD> counts(requests.size(),0);
                        std::vector<bool> prepared(requests.size(),false);
                        for(size_t n=0;n<requests.size();++n) {
                            const auto& request=requests[n];
                            prepared[n]=mem.AddScatterReadRequest(owner.handle,request.address,request.output,request.size,&counts[n]);
                        }
                        const bool executed=mem.ExecuteReadScatter(owner.handle);
                        for(size_t n=0;n<requests.size();++n)*requests[n].actual=prepared[n]?counts[n]:0;
                        // Close while PrepareEx's byte-count destinations still exist,
                        // including an Execute failure before its normal Clear.
                        mem.CloseScatterHandle(owner.handle);owner.handle=nullptr;
                        return executed;
                    };
                    const auto stage=pending.Step(currentIdentity(),GetTickCount(),exactBatch,
                        [](uint64_t encoded,uint8_t a,uint8_t b){return OW::Cn155284::Component(encoded,a,b);});
                    if(!OW::AcceptEntityRegistry(processRegistry) || !(currentIdentity()==pending.identity)) {
                        pending={};continue;
                    }
                    if(stage!=OW::EntityMaintenance::ComponentRefresh::Stage::Ready)continue;
                }
