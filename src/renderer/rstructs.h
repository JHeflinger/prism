#ifndef RSTRUCTS_H
#define RSTRUCTS_H

#include "data/profile.h"
#include "data/strings.h"
#include "renderer/vulkan/vconfig.h"
#include "renderer/rmath.h"
#include <vulkan/vulkan.h>

typedef uint32_t MaterialID;
typedef uint32_t TriangleID;
typedef uint32_t LightID;

DECLARE_ARRLIST(MaterialID);
DECLARE_ARRLIST(TriangleID);
DECLARE_ARRLIST(LightID);
DECLARE_ARRLIST(Vector3);

#define PREVIEW_PIPELINE_FLAGS   0b110011111111
#define PATHTRACE_PIPELINE_FLAGS 0b111101111111
#define HEADLESS_PIPELINE_FLAGS  0b001101111111
#define BVH_PIPELINE_FLAGS       0b000001111111
#define CENTROID_SHADER_FLAG     0b1
#define HISTOGRAM_SHADER_FLAG    0b10
#define HISTORY_SHADER_FLAG      0b100
#define SCATTER_SHADER_FLAG      0b1000
#define LEAVES_SHADER_FLAG       0b10000
#define BVH_SHADER_FLAG          0b100000
#define REBIND_SHADER_FLAG       0b1000000
#define DEFAULT_SHADER_FLAG      0b10000000
#define PATHTRACE_SHADER_FLAG    0b100000000
#define TONEMAP_SHADER_FLAG      0b1000000000
#define ANALYZE_SHADER_FLAG      0b10000000000
#define OVERLAY_SHADER_FLAG      0b100000000000

typedef uint32_t PipelineFlags;

typedef struct {
    vec3 position;
    vec3 look;
    vec3 up;
	float fov;
    float aperature;
    float focus;
} SimpleCamera;

typedef struct {
    alignas(4) uint32_t a;
    alignas(4) uint32_t b;
    alignas(4) uint32_t c;
    alignas(4) uint32_t an;
    alignas(4) uint32_t bn;
    alignas(4) uint32_t cn;
    alignas(4) MaterialID material;
} Triangle;
DECLARE_ARRLIST(Triangle);

typedef struct {
    alignas(16) vec3 min;
    alignas(16) vec3 max;
} AxisAlignedBoundingBox;

typedef struct {
    alignas(16) vec3 min;
    alignas(16) vec3 max;
    alignas(4) uint32_t left;
    alignas(4) uint32_t right;
    alignas(4) uint32_t parent;
    alignas(4) uint32_t counter;
} BVHNode;

typedef struct {
    alignas(16) vec3 emission;
    alignas(16) vec3 ambient;
    alignas(16) vec3 diffuse;
    alignas(16) vec3 specular;
    alignas(16) vec3 absorbtion;
    alignas(16) vec3 dispersion;
    alignas(4) float ior;
    alignas(4) float shiny;
    alignas(4) uint32_t model;
} SurfaceMaterial;
DECLARE_ARRLIST(SurfaceMaterial);

typedef struct {
    alignas(4) uint32_t tid;
} RayGenerator;

typedef struct {
    uint32_t value;
    BOOL exists;
} Schrodingnum;

typedef struct {
	RenderTexture2D target[CPUSWAP_LENGTH];
	size_t index;
    void* reference;
} CPUSwap;

typedef struct {
    size_t max_normals;
    size_t max_vertices;
    size_t max_triangles;
    size_t max_emissives;
    size_t max_materials;
    size_t max_lights;
    BOOL update_normals;
    BOOL update_vertices;
    BOOL update_triangles;
    BOOL update_materials;
    BOOL update_lights;
    size_t update_bvh;
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

typedef struct {
    void* halfedge;
    uint32_t index;
} ManifoldVertex;
DECLARE_ARRLIST(ManifoldVertex);

typedef struct {
    void* halfedge;
} ManifoldEdge;
DECLARE_ARRLIST(ManifoldEdge);

typedef struct {
    void* halfedge;
} ManifoldFace;
DECLARE_ARRLIST(ManifoldFace);

typedef struct {
    void* twin;
    void* next;
    ManifoldVertex* vertex;
    ManifoldEdge* edge;
    ManifoldFace* face;
} ManifoldHalfEdge;
DECLARE_ARRLIST(ManifoldHalfEdge);

typedef struct {
    ARRLIST_ManifoldVertex vertices;
    ARRLIST_ManifoldEdge edges;
    ARRLIST_ManifoldFace faces;
    ARRLIST_ManifoldHalfEdge halfedges;
} ManifoldMesh;

typedef struct {
    ARRLIST_Vector3 vertices;
    ARRLIST_Vector3 normals;
    ARRLIST_PointLight lights;
    ARRLIST_LightID lids;
    ARRLIST_Triangle triangles;
    ARRLIST_TriangleID tids;
    ARRLIST_TriangleID emissives;
    ARRLIST_SurfaceMaterial materials;
    ARRLIST_DynamicString materialnames;
    float lightarea;
    ChangeSet changes;
    Vector3 minBB;
    Vector3 maxBB;
} Geometry;

typedef struct {
    float whitepoint;
    float gamma;
    BOOL normals;
    BOOL direct;
    BOOL grid;
    BOOL async;
    BOOL showdof;
    BOOL directonly;
    PipelineFlags flags;
} RendererConfig;

#endif
