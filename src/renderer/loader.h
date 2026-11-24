#ifndef LOADER_H
#define LOADER_H

#include <easybool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    PBJ_CAMERA = 0,
    PBJ_MATERIAL,
    PBJ_LIGHT,
    PBJ_MESH,
    PBJ_FORCE,
    PBJ_SOURCE,
    PBJ_TYPE_COUNT
} PBJType;

const char* GetOpenFile(const char* type);

const char* GetSaveFile(const char* type);

BOOL LoadOBJ(const char* filepath);

BOOL LoadFBX(const char* filepath);

BOOL LoadPLY(const char* filepath);

BOOL LoadPRISM(const char* filepath);

BOOL LoadPBJ(const char* filepath, PBJType* loaded_type, size_t* loaded_index);

BOOL SavePRISM(const char* filepath);

BOOL SavePBJ(PBJType type, size_t index, const char* filepath);

#endif
