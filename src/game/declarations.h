#ifndef DECLARATIONS_H
#define DECLARATIONS_H

#include <raylib.h>
#include <stdint.h>
#include <easyobjects.h>
#include <easybasics.h>

#define DECLARE(name) struct name; typedef struct name name;

#define INVALID_ENTITY 0

typedef uint64_t EntityID;

typedef enum {
    INPUTPRESS,
    INPUTRELEASE,
    INPUTDOWN,
} InputAction;

DECLARE(GameScene);
DECLARE(GameWorld);
DECLARE(GameSystem);
DECLARE(Entity);
DECLARE(ComponentStorage);
DECLARE(Registry);
DECLARE(Script);
DECLARE(GamePack);

typedef void (*SceneInitializeFunction)(GameScene* scene);
typedef void (*SceneCleanFunction)(GameScene* scene);

typedef void (*WorldDrawFunction)(GameWorld* world);
typedef void (*WorldUpdateFunction)(GameWorld* world, float dt);
typedef void (*WorldKeyEventFunction)(GameWorld* world, int key, InputAction action);
typedef void (*WorldMouseButtonEventFunction)(GameWorld* world, int key, InputAction action);
typedef void (*WorldMouseScrollEventFunction)(GameWorld* world, Vector2 offset);
typedef void (*WorldMouseMoveFunction)(GameWorld* world, Vector2 position);
typedef void (*WorldCleanFunction)(GameWorld* world);

typedef void (*SystemDrawFunction)(GameSystem* system);
typedef void (*SystemUpdateFunction)(GameSystem* system, float dt);
typedef void (*SystemKeyEventFunction)(GameSystem* system, int key, InputAction action);
typedef void (*SystemMouseButtonEventFunction)(GameSystem* system, int key, InputAction action);
typedef void (*SystemMouseScrollEventFunction)(GameSystem* system, Vector2 offset);
typedef void (*SystemMouseMoveFunction)(GameSystem* system, Vector2 position);
typedef void (*SystemCleanFunction)(GameSystem* system);

typedef void (*ScriptComponentInitializeFunction)(const Entity e);
typedef void (*ScriptComponentDrawFunction)(const Entity e);
typedef void (*ScriptComponentUpdateFunction)(const Entity e, float dt);
typedef void (*ScriptComponentCleanFunction)(const Entity e);
typedef void (*ScriptComponentKeyEventFunction)(const Entity e, int key, InputAction action);
typedef void (*ScriptComponentMouseButtonEventFunction)(const Entity e, int key, InputAction action);
typedef void (*ScriptComponentMouseScrollEventFunction)(const Entity e, Vector2 offset);
typedef void (*ScriptComponentMouseMoveFunction)(const Entity e, Vector2 position);

DECLARE_ARRLIST(EntityID);
DECLARE_ARRLIST(Script);
DECLARE_ARRLIST(Entity);
DECLARE_ARRLIST(Sound);
DECLARE_ARRLIST(Music);
DECLARE_ARRLIST(Texture2D);
DECLARE_ARRLIST_NAMED(GameScenePtr, GameScene*);
DECLARE_ARRLIST_NAMED(GameWorldPtr, GameWorld*);
DECLARE_ARRLIST_NAMED(ComponentStoragePtr, ComponentStorage*);
DECLARE_ARRLIST_NAMED(GameSystemPtr, GameSystem*);
DECLARE_HASHMAP(int, BOOL, KeyMap);
DECLARE_HASHMAP(EntityID, size_t, EntityMap);
DECLARE_HASHMAP(size_t, ComponentStorage*, StorageMap);
DECLARE_HASHMAP(EntityID, EntityID, Parents);
DECLARE_HASHMAP(EntityID, ARRLIST_EntityID*, Children);

#undef DECLARE

struct Script {
    ScriptComponentInitializeFunction init;
    ScriptComponentUpdateFunction update;
    ScriptComponentDrawFunction draw;
    ScriptComponentCleanFunction clean;
    ScriptComponentKeyEventFunction key;
    ScriptComponentMouseButtonEventFunction mousebutton;
    ScriptComponentMouseMoveFunction mousemove;
    ScriptComponentMouseScrollEventFunction mousescroll;
    void* arbitrary;
    BOOL initialized;
};

#endif
