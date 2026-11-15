// All actors advance one initialization stage in one shared I/O execution.
// This does not admit a body; normal life/team/identity publication still runs.
        auto runBootstrapBatch = [&] {
            namespace M = OW::EntityMaintenance;
            if(offset::Active().componentTransform!=offset::ComponentTransformMode::Cn155284 ||
               !maintenanceBudget.CanStart(RuntimeTrace::NowUs()) || !OW::AcceptEntityRegistry(processRegistry))return;
            struct Work {
                uint64_t address=0,link=0;
                std::shared_ptr<RosterEntry> record;
                M::RefreshIdentity identity;
                M::RefreshHeader bodyHeader{},linkHeader{};
                uint32_t bodyBytes=0,linkBytes=0;
                bool headers=false;
            };
            // Bound request storage; rotate additional actors through later visits.
            constexpr size_t maxActors=32;
            std::vector<std::unique_ptr<Work>> work;
            std::vector<M::ComponentRefresh*> pending;
            std::vector<M::RefreshRequest> headerReads;
            auto identityFor=[&](const Work& item) {
                uint64_t revision=0,absence=0;
                { std::lock_guard lock(OW::entityBodyValidityMutex);
                  if(auto it=OW::entityBodyAuthorities.find(item.address);it!=OW::entityBodyAuthorities.end())revision=it->second.revision;
                  if(auto it=OW::entityBodyAbsenceRevisions.find(item.address);it!=OW::entityBodyAbsenceRevisions.end())absence=it->second; }
                return M::RefreshIdentity{{item.address,item.link,OW::ProcessConnection::ConnectionEpoch(),
                    OW::EntitySceneGeneration(),processRegistry.generation,uint64_t(offset::ActiveProfile()),
                    SDK->dwGameBase.load(),item.record->entity.actor_generation,revision,absence}};
            };
            const size_t start=raw_entities.empty()?0:bootstrapCursor%raw_entities.size();
            for(size_t n=0;n<raw_entities.size() && work.size()<maxActors;++n) {
                const size_t index=(start+n)%raw_entities.size();
                const auto [address,link]=raw_entities[index];
                if(!address || !link || !cycleDiscoveries.Find(address,link,GetTickCount()) || dynamicEntityCache.contains(address))continue;
                auto found=componentBaseCache.find(address);
                if(found!=componentBaseCache.end() && found->second->baseUpdateTick)continue;
                if(found==componentBaseCache.end())found=componentBaseCache.emplace(address,std::make_shared<RosterEntry>()).first;
                auto item=std::make_unique<Work>();item->address=address;item->link=link;item->record=found->second;
                auto& refresh=item->record->componentRefresh;
                item->identity=identityFor(*item);
                if((refresh.Active() || refresh.stage==M::ComponentRefresh::Stage::Ready) &&
                   (!(refresh.identity==item->identity) || uint32_t(GetTickCount()-refresh.startedTick)>5000))refresh={};
                if(refresh.ReadyFresh(GetTickCount(),OW::kEntityDeadComponentRefreshMs))continue;
                item->headers=!refresh.Active();
                if(item->headers) {
                    headerReads.push_back({address+M::RefreshHeader::offset,item->bodyHeader.bytes.data(),M::RefreshHeader::size,&item->bodyBytes});
                    if(address!=link)headerReads.push_back({link+M::RefreshHeader::offset,item->linkHeader.bytes.data(),M::RefreshHeader::size,&item->linkBytes});
                }
                pending.push_back(item->headers?nullptr:&refresh);
                work.push_back(std::move(item));
                bootstrapCursor=(index+1)%raw_entities.size();
            }
            if(work.empty())return;
            const uint64_t batchStartedUs=RuntimeTrace::NowUs();
            size_t requestsRead=0,bytesRead=0;
            bool executed=false;
            const auto tick=GetTickCount();
            M::AdvanceRefreshBatch(pending,[&](size_t i){return identityFor(*work[i]);},[] {return GetTickCount();},
                [&](const std::vector<M::RefreshRequest>& requests) {
                    requestsRead=requests.size();for(const auto& request:requests)bytesRead+=request.size;
                    executed=M::ReadRefreshBatch(mem,requests);return executed;
                },[](uint64_t value,uint8_t a,uint8_t b){return OW::Cn155284::Component(value,a,b);},headerReads);
            const auto& profile=offset::Active();
            const std::array<uint32_t,13> types{profile.typeHealth,profile.typeLink,profile.typeTeam,
                OW::TYPE_TRANSFORM,OW::TYPE_VELOCITY,profile.typeHeroId,OW::TYPE_BONE,profile.typeRotation,
                profile.typeSkill,OW::TYPE_P_VISIBILITY,profile.typePlayerController,OW::TYPE_ANGLE,OW::TYPE_STAT};
            const bool contextCurrent=OW::RuntimeWorkersRunning() && OW::ProcessConnection::IsConnected() &&
                OW::ProcessConnection::ConnectionEpoch()==connectionEpoch && OW::EntitySceneGeneration()==processSceneGeneration &&
                offset::ActiveProfile()==runtimeProfile && OW::AcceptEntityRegistry(processRegistry);
            for(auto& item:work) {
                auto& refresh=item->record->componentRefresh;
                if(!contextCurrent || !(identityFor(*item)==item->identity) ||
                   !cycleDiscoveries.Find(item->address,item->link,GetTickCount())) {refresh={};continue;}
                if(!item->headers)continue;
                if(!executed || item->bodyBytes!=M::RefreshHeader::size ||
                   (item->address!=item->link && item->linkBytes!=M::RefreshHeader::size))continue;
                if(item->address==item->link)item->linkHeader=item->bodyHeader;
                refresh.Begin(item->identity,item->bodyHeader,item->linkHeader,types,
                    SDK->dwGameBase+profile.componentKeyByteRva,SDK->dwGameBase+profile.componentKeySecondByteRva,
                    offset::Entity_MatchId,tick);
            }
            const size_t ready=std::count_if(work.begin(),work.end(),[](const auto& item){
                return item->record->componentRefresh.stage==M::ComponentRefresh::Stage::Ready;});
            static DWORD lastReport=0;
            if(ready || !lastReport || uint32_t(GetTickCount()-lastReport)>=1000) {
                lastReport=GetTickCount();
                Diagnostics::Info("[V2-INITIALIZE] actors=%zu ready=%zu requests=%zu bytes=%zu batch_ok=%d elapsed_us=%llu",
                    work.size(),ready,requestsRead,bytesRead,executed?1:0,
                    static_cast<unsigned long long>(RuntimeTrace::NowUs()-batchStartedUs));
            }
        };
