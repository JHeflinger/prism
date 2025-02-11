#ifndef RSTRUCTS_H
#define RSTRUCTS_H

#include "data/config.h"
#include "data/profile.h"
#include "renderer/vulkan/vconfig.h"
#include <easyobjects.h>
#include <raylib.h>
#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

typedef uint32_t MaterialID;
typedef uint64_t TriangleID;
typedef uint64_t SDFID;
DECLARE_ARRLIST(TriangleID);
DECLARE_ARRLIST(SDFID);

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

typedef enum {
    SDF_SPHERE = 0,
} SDFType;

typedef struct {
    alignas(4) uint32_t type;
    alignas(16) vec3 origin;
    alignas(4) float scale;
} SDFPrimitive;
DECLARE_ARRLIST(SDFPrimitive);

typedef struct {
    vec3 min;
    vec3 max;
    vec3 centroid;
} TriangleBB;
DECLARE_ARRLIST(TriangleBB);

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

#define BVH_LEAF 0
#define BVH_LEFT_ONLY 1
#define BVH_RIGHT_ONLY 2
#define BVH_BOTH 3

typedef const char* StaticString;
DECLARE_ARRLIST(StaticString);

typedef struct {
    alignas(4) uint32_t x;
    alignas(4) uint32_t y;
	alignas(4) float time;
} RayGenerator;

typedef struct {
    uint32_t value;
    BOOL exists;
} Schrodingnum;

typedef struct {
    alignas(16) vec3 min;
    alignas(16) vec3 max;
    alignas(4) uint32_t branch_config;
    alignas(4) uint32_t left;
    alignas(4) uint32_t right;
    // branches[0] describes: 0 = leaf, 1 = left tree, 2 = right tree, 3 = both
    // branches[1] is left tree ind
    // branches[2] is right tree ind
} NodeBVH;
DECLARE_ARRLIST(NodeBVH);

typedef struct {
	RenderTexture2D targets[CPUSWAP_LENGTH];
	size_t index;
    void* reference;
} CPUSwap;

typedef struct {
    size_t max_triangles;
    size_t max_bvh;
    size_t max_materials;
    size_t max_sdfs;
    BOOL update_triangles;
    BOOL update_materials;
    BOOL update_sdfs;
} ChangeSet;

typedef struct {
    Profiler profile;
} RendererStats;

typedef struct {
    ARRLIST_Triangle triangles;
    ARRLIST_TriangleID tids;
    ARRLIST_TriangleBB tbbs;
    ARRLIST_SurfaceMaterial materials;
    ARRLIST_NodeBVH bvh;
    ARRLIST_SDFPrimitive sdfs;
    ARRLIST_SDFID sdfids;
    ChangeSet changes;
} Geometry;

typedef struct {
    float frameless;
	BOOL shadows;
	BOOL reflections;
	BOOL lighting;
    BOOL raytrace;
    BOOL sdf;
    float sdfsmooth;
    uint32_t maxmarches;
} RendererConfig;

#endif
