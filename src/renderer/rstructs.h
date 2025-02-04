#ifndef RSTRUCTS_H
#define RSTRUCTS_H

#include "data/config.h"
#include <easyobjects.h>
#include <raylib.h>
#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

typedef uint64_t TriangleID;
DECLARE_ARRLIST(TriangleID);

typedef struct {
    Vector3 position;
    Vector3 look;
    Vector3 up;
	float fov;
} SimpleCamera;

typedef struct {
    alignas(16) vec3 a;
    alignas(16) vec3 b;
    alignas(16) vec3 c;
} Triangle;
DECLARE_ARRLIST(Triangle);

#endif
