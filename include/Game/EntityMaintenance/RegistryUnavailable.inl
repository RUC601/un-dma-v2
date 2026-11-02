// Shared lexical step of entity_thread; no I/O or new observations.
        if(!processRegistryState.available) {
            for(auto& [key,entry]:entityRoster)sanitizeStaleRosterEntity(entry->entity);
            for(auto& value:maintainedEntities)sanitizeStaleRosterEntity(value);
        }
