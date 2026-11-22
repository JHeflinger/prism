#include "world.h"
#include "game/definitions.h"
#include "game/ecs/entity.h"
#include "game/ecs/components.h"
#include "game/ecs/registry.h"
#include "game/systems/system.h"
#include "game/systems/draw.h"
#include "game/systems/audio.h"
#include <raymath.h>

GameWorld* GenerateWorld(
        const char* name,
        WorldDrawFunction draw,
        WorldUpdateFunction update,
        WorldKeyEventFunction key,
        WorldMouseButtonEventFunction mousebutton,
        WorldMouseScrollEventFunction mousescroll,
        WorldMouseMoveFunction mousemove,
        WorldCleanFunction clean) {
    GameWorld* world = EZ_ALLOC(1, sizeof(GameWorld));
    world->name = name;
    world->draw = draw;
    world->update = update;
    world->key = key;
    world->mousebutton = mousebutton;
    world->mousescroll = mousescroll;
    world->mousemove = mousemove;
    world->clean = clean;
    world->registry = GenerateRegistry();
    //AddSystem(world, GenerateDrawSystem());
    AddSystem(world, GenerateAudioSystem());
    return world;
}

Entity CreateEntity(GameWorld* world) {
    Entity e = (Entity){ RegistryCreateEntity(world->registry), world };
    AddComponent(e, TransformComponent, {0,0,0}, {0,0,0}, {1,1,1});
    AddComponent(e, TagComponent, "Untitled Entity", FALSE);
    return e;
}

Entity CreateEntityP(GameWorld* world, float x, float y, float z) {
    Entity e = CreateEntity(world);
    *(EntityPosition(e)) = (Vector3){ x, y, z };
    return e;
}

Entity CreateEntityN(GameWorld* world, const char* name) {
    Entity e = CreateEntity(world);
    TagComponent* tc = GetComponent(e, TagComponent);
    tc->tag = name;
    return e;
}

Entity CreateEntityNP(GameWorld* world, const char* name, float x, float y, float z) {
    Entity e = CreateEntityP(world, x, y, z);
    TagComponent* tc = GetComponent(e, TagComponent);
    tc->tag = name;
    return e;
}

void AddSystem(GameWorld* world, GameSystem* system) {
    ARRLIST_GameSystemPtr_add(&world->systems, system);
    system->context = world;
}

void DestroyWorld(GameWorld* world) {
    for (size_t i = 0; i < world->children.capacity; i++) {
        if (world->children.entries[i].used == 1) {
            ARRLIST_EntityID_clear(world->children.entries[i].value);
            EZ_FREE(world->children.entries[i].value);
        }
    }
    HASHMAP_Children_clear(&(world->children));
    HASHMAP_Parents_clear(&(world->parents));
    ARRLIST_EntityID* scripts = GetEntities(world, ScriptComponent);
    ARRLIST_EntityID* texts = GetEntities(world, TextComponent);
    if (scripts) {
        for (size_t i = 0; i < scripts->size; i++) {
            Entity e = (Entity){ scripts->data[i], world };
            Script* sc = EntityScript(e);
            if (sc && sc->clean) sc->clean(e);
        }
    }
    if (texts) {
        for (size_t i = 0; i < texts->size; i++) {
            Entity e = (Entity){ texts->data[i], world };
            TextComponent* tc = GetComponent(e, TextComponent);
            if (tc->capacity > 0) EZ_FREE(tc->text);
        }
    }
    if (world->clean) world->clean(world);
    for (size_t i = 0; i < world->systems.size; i++)
        DestroySystem(world->systems.data[i]);
    ARRLIST_GameSystemPtr_clear(&world->systems);
    DestroyRegistry(world->registry);
    EZ_FREE(world);
}

ARRLIST_EntityID* WorldGetEntities(GameWorld* world, size_t type) {
    return RegistryGetEntities(world->registry, type);
}

void DestroyEntity(Entity e) {
    ARRLIST_EntityID_add(&(e.context->removal), e.id);
    Script* sc = EntityScript(e);
    if (sc && sc->clean) sc->clean(e);
}

void FlushRemovalQueue(GameWorld* world) {
    for (size_t i = 0; i < world->removal.size; i++) {
        Entity e = (Entity){ world->removal.data[i], world };
        if (HasComponent(e, TextComponent)) {
            TextComponent* tc = GetComponent(e, TextComponent);
            if (tc->capacity > 0) EZ_FREE(tc->text);
        }
        RegistryEraseEntity(world->registry, world->removal.data[i]);
    }
    ARRLIST_EntityID_clear(&(world->removal));
}

BOOL HasParent(Entity e) {
    return HASHMAP_Parents_has(&(e.context->parents), e.id);
}

BOOL HasChild(Entity parent, Entity child) {
    ARRLIST_EntityID* children = GetChildren(parent);
    for (size_t i = 0; i < children->size; i++) {
        if (children->data[i] == child.id) return TRUE;
    }
    return FALSE;
}

void OrphanChild(Entity e) {
    EZ_ASSERT(HasParent(e), "This child does not have a parent");
    Entity parent = GetParent(e);
    ARRLIST_EntityID* children = GetChildren(parent);
    HASHMAP_Parents_remove(&(e.context->parents), e.id);
    for (size_t i = 0; i < children->size; i++) {
        if (children->data[i] == e.id) {
            ARRLIST_EntityID_remove(children, i);
            return;
        }
    }
    EZ_ASSERT(TRUE, "Broken parent-child link detected");
}

Entity GetParent(Entity e) {
    EZ_ASSERT(HasParent(e), "Cannot get a parent if entity does not have one");
    return (Entity){ HASHMAP_Parents_get(&(e.context->parents), e.id), e.context };
}

ARRLIST_EntityID* GetChildren(Entity e) {
    if (HASHMAP_Children_has(&(e.context->children), e.id)) {
        return HASHMAP_Children_get(&(e.context->children), e.id);
    }
    ARRLIST_EntityID* newarr = EZ_ALLOC(1, sizeof(ARRLIST_EntityID));
    HASHMAP_Children_set(&(e.context->children), e.id, newarr);
    return newarr;
}

void LinkFamily(Entity parent, Entity child) {
    EZ_ASSERT(!HasChild(parent, child), "This parent already has this child");
    EZ_ASSERT(!HasParent(child), "This child already a parent");
    ARRLIST_EntityID* children = GetChildren(parent);
    ARRLIST_EntityID_add(children, child.id);
    HASHMAP_Parents_set(&(child.context->parents), child.id, parent.id);
}
