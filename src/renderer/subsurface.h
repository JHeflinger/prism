#ifndef SUBSURFACE_H
#define SUBSURFACE_H

#include "renderer/vulkan/vstructs.h"
#include <stdint.h>

typedef struct {
    uint32_t surface_gaussians;
    uint32_t internal_gaussians;
    uint32_t walks;
    uint32_t lobes;
    uint32_t max_steps; // free flights before a reference walk is abandoned
    uint32_t epochs;
    uint32_t validation;
    uint32_t seed;
    int gpu_index; // Vulkan device used for training, -1 = pick the best one
} GSSSConfig;

typedef enum {
    SUBSURFACE_IDLE = 0,
    SUBSURFACE_TRAINING,
    SUBSURFACE_DONE,
    SUBSURFACE_FAILED
} SubsurfaceTrainPhase;

typedef struct {
    BOOL linked;
    BOOL enabled;
    uint32_t channels; // 1, or 3 for per channel clouds (coloured scattering)
    uint32_t triangles;
    uint32_t surface_gaussians;
    uint32_t lobes;
    uint32_t internal_gaussians;
    uint32_t bytes;
    float sigma; // world space free flight density it was trained for (red channel for per channel clouds)
    float anisotropy;
    float scale; // object scale it was trained at
    MaterialID material;
    BOOL stale;
    char reason[128];
} SubsurfaceCloudInfo;

GSSSConfig SubsurfaceDefaultConfig();

size_t SubsurfaceCloudCount();

BOOL SubsurfaceTrainStart(size_t mesh, const GSSSConfig* config);

BOOL SubsurfaceTrainTick(double seconds);

void SubsurfaceTrainCancel();

SubsurfaceTrainPhase SubsurfaceTrainPhaseNow();

size_t SubsurfaceTrainMesh();

float SubsurfaceTrainProgress();

const char* SubsurfaceTrainStatus();

BOOL QuerySubsurfaceCloudInfo(size_t mesh, SubsurfaceCloudInfo* info);

void SubsurfaceSetCloudEnabled(size_t mesh, BOOL enabled);

BOOL SubsurfaceRemoveCloud(size_t mesh);

void SubsurfaceClear();

void SubsurfaceMeshDeleted(size_t mesh);

BOOL SubsurfacePreviewCloud(size_t mesh);

uint32_t* SubsurfaceSerialize(size_t* words);

BOOL SubsurfaceDeserialize(const uint32_t* data, size_t words);

BOOL SubsurfaceNeedsUpdate();

void ResolveSubsurfaceUpdate();

#endif
