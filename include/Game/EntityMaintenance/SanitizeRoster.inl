// Shared lexical step of entity_thread.
    auto sanitizeStaleRosterEntity = [](OW::c_entity& entity) {
        entity.body_valid = false;
        OW::InvalidateEntityBody(entity.address,entity.actor_generation,entity.body_authority_revision);
        entity.Alive = false;
        entity.Vis = false;
        entity.Trg = false;
        entity.velocity = Vector3(0, 0, 0);
    };
