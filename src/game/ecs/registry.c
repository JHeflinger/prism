#include "registry.h"
#include "game/definitions.h"

Registry* GenerateRegistry() {
    Registry* registry = EZ_ALLOC(1, sizeof(Registry));
    registry->next = 1;
    return registry;
}

EntityID RegistryCreateEntity(Registry* registry) {
    return registry->next++;
}

void RegistryEraseEntity(Registry* registry, EntityID entity) {
    for (size_t i = 0; i < registry->types.size; i++)
        RegistryRemoveComponent(registry, entity, registry->types.data[i]);
}

void* RegistryEmplaceComponent(Registry* registry, EntityID entity, size_t type, void* component, size_t size) {
    EZ_ASSERT(!RegistryHasComponent(registry, entity, type), "Entity already has component");
    if (!HASHMAP_StorageMap_has(&registry->storage, type)) {
        ComponentStorage* cs = EZ_ALLOC(1, sizeof(ComponentStorage));
        HASHMAP_StorageMap_set(&registry->storage, type, cs);
        ARRLIST_ComponentStoragePtr_add(&registry->dense, cs);
        ARRLIST_size_t_add(&registry->types, type);
    }
    ComponentStorage* cs = HASHMAP_StorageMap_get(&registry->storage, type);
    void* heapcomponent = EZ_ALLOC(1, size);
    memcpy(heapcomponent, component, size);
    ARRLIST_voidPtr_add(&cs->dense, heapcomponent);
    ARRLIST_EntityID_add(&cs->entities, entity);
    HASHMAP_EntityMap_set(&cs->sparse, entity, cs->dense.size - 1);
    return RegistryGetComponent(registry, entity, type);
}

void* RegistryGetComponent(Registry* registry, EntityID entity, size_t type) {
    if (HASHMAP_StorageMap_has(&registry->storage, type)) {
        ComponentStorage* cs = HASHMAP_StorageMap_get(&registry->storage, type);
        if (HASHMAP_EntityMap_has(&cs->sparse, entity))
            return cs->dense.data[HASHMAP_EntityMap_get(&cs->sparse, entity)];
    }
    return NULL;
}

BOOL RegistryHasComponent(Registry* registry, EntityID entity, size_t type) {
    if (HASHMAP_StorageMap_has(&registry->storage, type)) {
        ComponentStorage* cs = HASHMAP_StorageMap_get(&registry->storage, type);
        return HASHMAP_EntityMap_has(&cs->sparse, entity);
    }
    return FALSE;
}

void RegistryRemoveComponent(Registry* registry, EntityID entity, size_t type) {
    if (!RegistryHasComponent(registry, entity, type)) return;
    ComponentStorage* cs = HASHMAP_StorageMap_get(&registry->storage, type);
    HASHMAP_EntityMap_remove(&cs->sparse, entity);
    for (size_t i = 0; i < cs->entities.size; i++) {
        if (cs->entities.data[i] == entity) {
            ARRLIST_EntityID_remove(&cs->entities, i);
            break;
        }
    }
}

ARRLIST_EntityID* RegistryGetEntities(Registry* registry, size_t type) {
    if (!HASHMAP_StorageMap_has(&registry->storage, type))
        return NULL;
    return &(HASHMAP_StorageMap_get(&registry->storage, type)->entities);
}

static void CleanComponents(Registry* registry) {
    // TODO: add any override cleans here
}

void DestroyRegistry(Registry* registry) {
    CleanComponents(registry);
    HASHMAP_StorageMap_clear(&registry->storage);
    for (size_t i = 0; i < registry->dense.size; i++) {
        for (size_t j = 0; j < registry->dense.data[i]->dense.size; j++)
            EZ_FREE(registry->dense.data[i]->dense.data[j]);
        ARRLIST_voidPtr_clear(&registry->dense.data[i]->dense);
        ARRLIST_EntityID_clear(&registry->dense.data[i]->entities);
        HASHMAP_EntityMap_clear(&registry->dense.data[i]->sparse);
        EZ_FREE(registry->dense.data[i]);
    }
    ARRLIST_ComponentStoragePtr_clear(&registry->dense);
    ARRLIST_size_t_clear(&registry->types);
    EZ_FREE(registry);
}
