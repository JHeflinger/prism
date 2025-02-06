#ifndef RSTRUCTS_H
#define RSTRUCTS_H

#include "data/config.h"
#include <easyobjects.h>
#include <raylib.h>
#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

typedef uint32_t MaterialID;
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
    alignas(4) MaterialID material;
} Triangle;
DECLARE_ARRLIST(Triangle);

typedef struct {
    alignas(16) vec3 ambient;
    alignas(16) vec3 diffuse;
    alignas(16) vec3 specular;
    alignas(4) float reflect;
    alignas(4) float refract;
    alignas(4) float rindex;
    alignas(4) float transparency;
    alignas(4) float shiny;
    alignas(4) float glossy;
} SurfaceMaterial;
DECLARE_ARRLIST(SurfaceMaterial);

typedef struct {
    alignas(16) vec3 min;
    alignas(16) vec3 max;
    alignas(16) uint32_t branches[3]; // [0] is leaf or no, [1] is left tree ind, [2] is right tree ind
} NodeBVH;
DECLARE_ARRLIST(NodeBVH);

#endif
