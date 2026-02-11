#ifndef RSTRUCTS_H
#define RSTRUCTS_H

#include "data/profile.h"
#include "data/strings.h"
#include "renderer/vulkan/vconfig.h"
#include <raylib.h>
#include <vulkan/vulkan.h>
#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

typedef uint32_t MaterialID;
typedef uint32_t TriangleID;
typedef uint32_t SDFID;
typedef uint32_t LightID;
DECLARE_ARRLIST(MaterialID);
DECLARE_ARRLIST(TriangleID);
DECLARE_ARRLIST(SDFID);
DECLARE_ARRLIST(LightID);

#define SETVEC3(v, x, y, z) {v[0] = x; v[1] = y; v[2] = z;}
#define SETVEC(v1, v2) {v1[0] = v2[0]; v1[1] = v2[1]; v1[2] = v2[2];}

#define PREVIEW_PIPELINE_FLAGS 0b11001
#define PATHTRACE_PIPELINE_FLAGS 0b11110
#define HEADLESS_PIPELINE_FLAGS 0b00110
#define DEFAULT_SHADER_FLAG 0b1
#define PATHTRACE_SHADER_FLAG 0b10
#define TONEMAP_SHADER_FLAG 0b100
#define ANALYZE_SHADER_FLAG 0b1000
#define OVERLAY_SHADER_FLAG 0b10000

typedef uint32_t PipelineFlags;

typedef struct {
    vec3 position;
    vec3 look;
    vec3 up;
	float fov;
} SimpleCamera;

typedef struct {
    alignas(16) vec3 a;
    alignas(16) vec3 b;
    alignas(16) vec3 c;
    alignas(16) vec3 an;
    alignas(16) vec3 bn;
    alignas(16) vec3 cn;
    alignas(4) MaterialID material;
} Triangle;
DECLARE_ARRLIST(Triangle);

typedef enum {
    SDF_SPHERE = 0,
    SDF_JULIA = 1,
    SDF_MANDELBULB = 2,
	SDF_BOX = 3,
	SDF_CLOUD = 4
} SDFType;

typedef struct {
    alignas(4) uint32_t type;
    alignas(16) vec3 origin;
    alignas(4) float scale;
	alignas(16) vec3 dim;
} SDFPrimitive;
DECLARE_ARRLIST(SDFPrimitive);

typedef struct {
    vec3 min;
    vec3 max;
    vec3 centroid;
} TriangleBB;
DECLARE_ARRLIST(TriangleBB);

typedef struct {
    alignas(16) vec3 emission;
    alignas(16) vec3 ambient;
    alignas(16) vec3 diffuse;
    alignas(16) vec3 specular;
    alignas(4) float ior;
    alignas(4) float shiny;
    alignas(4) uint32_t model;
} SurfaceMaterial;
DECLARE_ARRLIST(SurfaceMaterial);

#define BVH_LEAF 0
#define BVH_LEFT_ONLY 1
#define BVH_RIGHT_ONLY 2
#define BVH_BOTH 3

typedef struct {
	alignas(4) float time;
    alignas(4) uint32_t tid;
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
	RenderTexture2D target[CPUSWAP_LENGTH];
	size_t index;
    void* reference;
} CPUSwap;

typedef struct {
    size_t max_triangles;
    size_t max_emissives;
    size_t max_bvh;
    size_t max_materials;
    size_t max_sdfs;
    size_t max_lights;
    BOOL update_triangles;
    BOOL update_materials;
    BOOL update_sdfs;
    BOOL update_lights;
} ChangeSet;

typedef struct {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT heap_budget;
    VkPhysicalDeviceMemoryProperties2 heap_props;
    double update_interval;
    double update_timer;
	BOOL available;
} GPUStatCache;

typedef struct {
    Profiler profile;
    GPUStatCache cache;
} RendererStats;

typedef struct {
    alignas(16) vec3 position;
    alignas(16) vec3 ambient;
    alignas(16) vec3 diffuse;
    alignas(16) vec3 specular;
} PointLight;
DECLARE_ARRLIST(PointLight);

#define MAX_MATERIAL_NAME_SIZE 256

typedef struct {
    ARRLIST_PointLight lights;
    ARRLIST_LightID lids;
    ARRLIST_Triangle triangles;
    ARRLIST_TriangleID tids;
    ARRLIST_TriangleBB tbbs;
    ARRLIST_TriangleID emissives;
    ARRLIST_SurfaceMaterial materials;
    ARRLIST_DynamicString materialnames;
    ARRLIST_NodeBVH bvh;
    ARRLIST_SDFPrimitive sdfs;
    ARRLIST_SDFID sdfids;
    ChangeSet changes;
} Geometry;

typedef struct {
    float time;
    float frameless;
    float smoothen;
    float roulette;
    float whitepoint;
    float gamma;
    uint32_t marches;
    BOOL direct;
    BOOL autoframeless;
    BOOL grid;
    BOOL async;
    PipelineFlags flags;
} RendererConfig;

#endif
