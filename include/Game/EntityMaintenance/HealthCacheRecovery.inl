    auto refreshHealthBaseAfterTransportFailure = [&](OW::c_entity& failed, DWORD now) {
        const auto cached=componentBaseCache.find(failed.address);
        if(cached==componentBaseCache.end())return false;
        const auto* discovery=cycleDiscoveries.Find(failed.address,failed.LinkParent,now);
        if(!discovery || discovery->match!=failed.match_id)return false;
        OW::EntityHeaderSnapshot header;
        if(!header.Read(failed.address))return false;
        const auto health=OW::DecryptComponent(failed.address,OW::offset::Active().typeHealth,&header,true);
        if(!health)return false;
        std::array<uint64_t,6> layout{};
        // A moved health component can accompany moved team/position components.
        // Rebuild that whole cache instead of mixing addresses from two layouts.
        if (health != cached->second->health || !cached->second->layoutsKnown ||
            !header.ReadComponentLayout(layout) || layout != cached->second->componentLayout) {
            auto& previous=cached->second->entity;
            OW::RevokeEntityBodyObservation(previous.address,previous.actor_generation,previous.body_authority_revision);
            previous.body_valid=false;
            return false;
        }
        cached->second->health=health;cached->second->healthValid=false;
        failed.HealthBase=health;return true;
    };
