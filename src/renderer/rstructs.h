#ifndef RSTRUCTS_H
#define RSTRUCTS_H

#include "data/config.h"
#include <easyobjects.h>
#include <raylib.h>
#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

typedef uint64_t TriangleID;
DECLARE_ARRLIST(TriangleID);

typedef uint32_t TextureID;

typedef struct {
    Vector3 position;
    Vector3 look;
    Vector3 up;
	float fov;
} SimpleCamera;

typedef struct {
    vec3 position;
    vec3 color;
    vec2 texcoord;
    TextureID texid;
} Vertex;
DECLARE_ARRLIST(Vertex);

typedef struct {
    Vertex vertices[3];
} Triangle;

#endif
