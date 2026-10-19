#ifndef SUBSURFACE_H
#define SUBSURFACE_H

// Subsurface Gaussian clouds ("SSS splats"): trains a cloud of gaussians for one object with a subsurface material
// (see sssfit.h for what is learnt), links it to that object, and keeps every linked cloud packed in the
// SubsurfaceSSBOIn buffer that path.comp samples instead of running the random walk.
//
// All state lives here rather than in Geometry; SavePRISM / LoadPRISM store the clouds after the geometry.

#include "renderer/vulkan/vstructs.h"
#include "renderer/sssfit.h"

typedef enum {
    SUBSURFACE_IDLE = 0,
    SUBSURFACE_TRAINING,
    SUBSURFACE_DONE,
    SUBSURFACE_FAILED
} SubsurfaceTrainPhase;

typedef struct {
    BOOL enabled;      // use trained clouds in the path tracer (FALSE = always random walk, for A/B comparisons)
    BOOL exitlight;    // next event estimation at the exit point of a cloud sampled path
} SubsurfaceSettings;

typedef struct {
    BOOL linked;
    BOOL enabled;
    uint32_t triangles;
    uint32_t surface_gaussians;
    uint32_t lobes;
    uint32_t internal_gaussians;
    uint32_t bytes;
    float sigma;       // world space free flight density it was trained for
    float anisotropy;
    float scale;       // object scale it was trained at
    MaterialID material;
    BOOL stale;        // the object or its material changed in a way the cloud does not account for
    char reason[128];
} SubsurfaceCloudInfo;

SubsurfaceSettings* SubsurfaceSettingsRef();

SSSFitConfig SubsurfaceDefaultConfig();

// Starts training a cloud for mesh descriptor `mesh` (time sliced, see SubsurfaceTrainTick).
BOOL SubsurfaceTrainStart(size_t mesh, const SSSFitConfig* config);

// Advances training for about `seconds`. Returns TRUE while there is more work to do.
BOOL SubsurfaceTrainTick(double seconds);

void SubsurfaceTrainCancel();

SubsurfaceTrainPhase SubsurfaceTrainPhaseNow();

size_t SubsurfaceTrainMesh();

float SubsurfaceTrainProgress();

const char* SubsurfaceTrainStatus();

// statistics of the last finished training (NULL if there was none)
const SSSFitStats* SubsurfaceLastStats();

BOOL SubsurfaceCloudInfoFor(size_t mesh, SubsurfaceCloudInfo* info);

void SubsurfaceSetCloudEnabled(size_t mesh, BOOL enabled);

BOOL SubsurfaceRemoveCloud(size_t mesh);

// Adds the internal cloud of `mesh` to the scene as ordinary (visible) gaussian splats, coloured by the expected
// excess path length, as a debugging aid.
BOOL SubsurfacePreviewCloud(size_t mesh);

// scene bookkeeping
void SubsurfaceClear();

void SubsurfaceMeshDeleted(size_t mesh);

size_t SubsurfaceCloudCount();

// Session persistence (the clouds live inside the .prism file). Serialize returns an EZ_ALLOC'd word array.
uint32_t* SubsurfaceSerialize(size_t* words);

// Replaces every cloud with the serialized ones; call after the session geometry is in place.
BOOL SubsurfaceDeserialize(const uint32_t* data, size_t words);

// GPU side: packed buffer bound as SubsurfaceSSBOIn
ShaderBuffer* SubsurfaceBuffer();

#endif
