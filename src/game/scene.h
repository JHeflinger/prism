#ifndef SCENE_H
#define SCENE_H

#include "game/declarations.h"

GameScene* GenerateScene(const char* name, SceneInitializeFunction init, SceneCleanFunction clean);

void WipeScene(GameScene* scene);

void DestroyScene(GameScene* scene);

void ResetScene(GameScene* scene);

void AddWorld(GameScene* scene, GameWorld* world);

GameWorld* FindWorld(GameScene* scene, const char* name);

size_t PackScript(GameScene* scene, Script script, const char* name, const char* description);

size_t FindScript(GameScene* scene, const char* name);

size_t PackImage(GameScene* scene, const char* path, const char* name);

size_t FindImage(GameScene* scene, const char* name);

size_t PackSound(GameScene* scene, const char* path, const char* name);

size_t FindSound(GameScene* scene, const char* name);

size_t PackMusic(GameScene* scene, const char* path, const char* name);

size_t FindMusic(GameScene* scene, const char* name);

void RefreshGameAssets(GameScene* scene);

#endif
