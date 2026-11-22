#ifndef DEFINITIONS_H
#define DEFINITIONS_H

#include "game/declarations.h"
#include "data/strings.h"
#include <ui/ui.h>

typedef struct {
    ARRLIST_StaticString scripts;
    ARRLIST_StaticString sounds;
    ARRLIST_StaticString musics;
    ARRLIST_StaticString images;
} PackNames;

typedef struct {
    ARRLIST_StaticString scripts;
    ARRLIST_StaticString sounds;
    ARRLIST_StaticString musics;
    ARRLIST_StaticString images;
} PackPaths;

typedef struct {
    ARRLIST_StaticString scripts;
} PackDescriptions;

struct GamePack {
    ARRLIST_Script scripts;
    ARRLIST_Sound sounds;
    ARRLIST_Music musics;
    ARRLIST_Texture2D images;
    PackNames names;
    PackPaths paths;
    PackDescriptions descriptions;
};

struct GameScene {
    const char* name;
    ARRLIST_GameWorldPtr worlds;
    SceneInitializeFunction init;
    SceneCleanFunction clean;
    GamePack assets;
};

struct GameWorld {
    const char* name;
    WorldDrawFunction draw;
    WorldUpdateFunction update;
    WorldKeyEventFunction key;
    WorldMouseButtonEventFunction mousebutton;
    WorldMouseScrollEventFunction mousescroll;
    WorldMouseMoveFunction mousemove;
    WorldCleanFunction clean;
    Registry* registry;
    ARRLIST_GameSystemPtr systems;
    ARRLIST_EntityID removal;
    HASHMAP_Parents parents;
    HASHMAP_Children children;
    GameScene* parent;
};

struct GameSystem {
    SystemDrawFunction draw;
    SystemUpdateFunction update;
    SystemKeyEventFunction key;
    SystemMouseButtonEventFunction mousebutton;
    SystemMouseScrollEventFunction mousescroll;
    SystemMouseMoveFunction mousemove;
    SystemCleanFunction clean;
    GameWorld* context;
};

struct ComponentStorage {
    ARRLIST_voidPtr dense;
    ARRLIST_EntityID entities;
    HASHMAP_EntityMap sparse;
};

struct Registry {
    EntityID next;
    HASHMAP_StorageMap storage;
    ARRLIST_ComponentStoragePtr dense;
    ARRLIST_size_t types;
};

#endif
