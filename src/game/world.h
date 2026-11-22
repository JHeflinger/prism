#ifndef WORLD_H
#define WORLD_H

#include "game/declarations.h"

#define GetEntities(world, component) WorldGetEntities(world, (size_t)component##_TYPE)

GameWorld* GenerateWorld(
        const char* name,
        WorldDrawFunction draw,
        WorldUpdateFunction update,
        WorldKeyEventFunction key,
        WorldMouseButtonEventFunction mousebutton,
        WorldMouseScrollEventFunction mousescroll,
        WorldMouseMoveFunction mousemove,
        WorldCleanFunction clean);

Entity CreateEntity(GameWorld* world);

Entity CreateEntityP(GameWorld* world, float x, float y, float z);

Entity CreateEntityN(GameWorld* world, const char* name);

Entity CreateEntityNP(GameWorld* world, const char* name, float x, float y, float z);

void AddSystem(GameWorld* world, GameSystem* system);

void DestroyWorld(GameWorld* world);

ARRLIST_EntityID* WorldGetEntities(GameWorld* world, size_t type);

void DestroyEntity(Entity e);

void FlushRemovalQueue(GameWorld* world);

BOOL HasParent(Entity e);

BOOL HasChild(Entity parent, Entity child);

void OrphanChild(Entity e);

Entity GetParent(Entity e);

ARRLIST_EntityID* GetChildren(Entity e);

void LinkFamily(Entity parent, Entity child);

#endif
