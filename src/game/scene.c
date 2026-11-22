#include "scene.h"
#include "game/definitions.h"
#include "util/logger.h"
#include "game/world.h"

GameScene* GenerateScene(const char* name, SceneInitializeFunction init, SceneCleanFunction clean) {
    GameScene* scene = EZ_ALLOC(1, sizeof(GameScene));
    scene->name = name;
    scene->init = init;
    scene->clean = clean;
    if (scene->init) scene->init(scene);
    return scene;
}

void WipeScene(GameScene* scene) {
    for (size_t i = 0; i < scene->worlds.size; i++) DestroyWorld(scene->worlds.data[i]);
    ARRLIST_GameWorldPtr_clear(&scene->worlds);
    ARRLIST_StaticString_clear(&(scene->assets.names.scripts));
    ARRLIST_StaticString_clear(&(scene->assets.descriptions.scripts));
    ARRLIST_Script_clear(&(scene->assets.scripts));
    ARRLIST_StaticString_clear(&(scene->assets.names.images));
    ARRLIST_StaticString_clear(&(scene->assets.names.sounds));
    ARRLIST_StaticString_clear(&(scene->assets.names.musics));
    ARRLIST_StaticString_clear(&(scene->assets.paths.images));
    ARRLIST_StaticString_clear(&(scene->assets.paths.sounds));
    ARRLIST_StaticString_clear(&(scene->assets.paths.musics));
    for (size_t i = 0; i < scene->assets.images.size; i++) UnloadTexture(scene->assets.images.data[i]);
    for (size_t i = 0; i < scene->assets.sounds.size; i++) {
        StopSound(scene->assets.sounds.data[i]);
        UnloadSound(scene->assets.sounds.data[i]);
    }
    for (size_t i = 0; i < scene->assets.musics.size; i++) {
        StopMusicStream(scene->assets.musics.data[i]);
        UnloadMusicStream(scene->assets.musics.data[i]);
    }
    ARRLIST_Texture2D_clear(&(scene->assets.images));
    ARRLIST_Sound_clear(&(scene->assets.sounds));
    ARRLIST_Music_clear(&(scene->assets.musics));
}

void DestroyScene(GameScene* scene) {
    if (scene->clean) scene->clean(scene);
    WipeScene(scene);
    EZ_FREE(scene);
}

void ResetScene(GameScene* scene) {
    if (scene->clean) scene->clean(scene);
    if (scene->init) scene->init(scene);
}

void AddWorld(GameScene* scene, GameWorld* world) {
    EZ_ASSERT(!FindWorld(scene, world->name), "A world with this name already exists!");
    EZ_ASSERT(world->parent == NULL, "World already is linked to an existing scene");
    world->parent = scene;
    ARRLIST_GameWorldPtr_add(&scene->worlds, world);
}

GameWorld* FindWorld(GameScene* scene, const char* name) {
    for (size_t i = 0; i < scene->worlds.size; i++) {
        if (strcmp(scene->worlds.data[i]->name, name) == 0) return scene->worlds.data[i];
    }
    return NULL;
}

size_t PackScript(GameScene* scene, Script script, const char* name, const char* description) {
    EZ_ASSERT(FindScript(scene, name) == (size_t)-1, "A script with this name (%s) has already been packed into this scene", name);
    ARRLIST_StaticString_add(&(scene->assets.names.scripts), name);
    ARRLIST_StaticString_add(&(scene->assets.descriptions.scripts), description);
    ARRLIST_Script_add(&(scene->assets.scripts), script);
    return scene->assets.scripts.size - 1;
}

size_t FindScript(GameScene* scene, const char* name) {
    for (size_t i = 0; i < scene->assets.names.scripts.size; i++)
        if (strcmp(scene->assets.names.scripts.data[i], name) == 0) return i;
    return (size_t)-1;
}

size_t PackImage(GameScene* scene, const char* path, const char* name) {
    EZ_ASSERT(FindImage(scene, name) == (size_t)-1, "An image with this name (%s) has already been packed into this scene", name);
    ARRLIST_StaticString_add(&(scene->assets.names.images), name);
    ARRLIST_StaticString_add(&(scene->assets.paths.images), path);
    ARRLIST_Texture2D_add(&(scene->assets.images), LoadTexture(path));
    return scene->assets.images.size - 1;
}

size_t FindImage(GameScene* scene, const char* name) {
    for (size_t i = 0; i < scene->assets.names.images.size; i++)
        if (strcmp(scene->assets.names.images.data[i], name) == 0) return i;
    return (size_t)-1;
}

size_t PackSound(GameScene* scene, const char* path, const char* name) {
    EZ_ASSERT(FindSound(scene, name) == (size_t)-1, "A sound with this name (%s) has already been packed into this scene", name);
    ARRLIST_StaticString_add(&(scene->assets.names.sounds), name);
    ARRLIST_StaticString_add(&(scene->assets.paths.sounds), path);
    ARRLIST_Sound_add(&(scene->assets.sounds), LoadSound(path));
    return scene->assets.sounds.size - 1;
}

size_t FindSound(GameScene* scene, const char* name) {
    for (size_t i = 0; i < scene->assets.names.sounds.size; i++)
        if (strcmp(scene->assets.names.sounds.data[i], name) == 0) return i;
    return (size_t)-1;
}

size_t PackMusic(GameScene* scene, const char* path, const char* name) {
    EZ_ASSERT(FindMusic(scene, name) == (size_t)-1, "A stream with this name (%s) has already been packed into this scene", name);
    ARRLIST_StaticString_add(&(scene->assets.names.musics), name);
    ARRLIST_StaticString_add(&(scene->assets.paths.musics), path);
    ARRLIST_Music_add(&(scene->assets.musics), LoadMusicStream(path));
    return scene->assets.musics.size - 1;
}

size_t FindMusic(GameScene* scene, const char* name) {
    for (size_t i = 0; i < scene->assets.names.musics.size; i++)
        if (strcmp(scene->assets.names.musics.data[i], name) == 0) return i;
    return (size_t)-1;
}

void RefreshGameAssets(GameScene* scene) {
    logtrace("Refreshing assets for game scene \"%s\"", scene->name);
    for (size_t i = 0; i < scene->assets.images.size; i++) {
        UnloadTexture(scene->assets.images.data[i]);
        scene->assets.images.data[i] = LoadTexture(scene->assets.paths.images.data[i]);
        while (!IsTextureValid(scene->assets.images.data[i])) {}
    }
    for (size_t i = 0; i < scene->assets.sounds.size; i++) {
        UnloadSound(scene->assets.sounds.data[i]);
        scene->assets.sounds.data[i] = LoadSound(scene->assets.paths.sounds.data[i]);
        while (!IsSoundValid(scene->assets.sounds.data[i])) {}
    }
    for (size_t i = 0; i < scene->assets.musics.size; i++) {
        UnloadMusicStream(scene->assets.musics.data[i]);
        scene->assets.musics.data[i] = LoadMusicStream(scene->assets.paths.musics.data[i]);
        while (!IsMusicValid(scene->assets.musics.data[i])) {}
    }
    loginfo("Successfuly refreshed assets for game scene \"%s\"", scene->name);
}
