#ifndef REGISTRY_H
#define REGISTRY_H

#include "game/declarations.h"

Registry* GenerateRegistry();

EntityID RegistryCreateEntity(Registry* registry);

void RegistryEraseEntity(Registry* registry, EntityID entity);

void* RegistryEmplaceComponent(Registry* registry, EntityID entity, size_t type, void* component, size_t size);

void* RegistryGetComponent(Registry* registry, EntityID entity, size_t type);

BOOL RegistryHasComponent(Registry* registry, EntityID entity, size_t type);

void RegistryRemoveComponent(Registry* registry, EntityID entity, size_t type);

ARRLIST_EntityID* RegistryGetEntities(Registry* registry, size_t type);

void DestroyRegistry(Registry* registry);

#endif
