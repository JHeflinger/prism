#ifndef SSSFIT_H
#define SSSFIT_H

#include <stdint.h>
#include <stddef.h>

// ---- packed format (uint32 words, floats stored as raw bits) -------------------------------------------------------
// One object is a self contained blob; every offset inside it is relative to the start of the blob.


// header words

// surface gaussian words: 0-2 centre, 3 tangential sigma, 4-6 outward normal, 7 normal sigma,
//                         8-10 exit direction axis, 11 exit direction exponent, 12 first lobe, 13 lobe count,
//                         14 training samples (entries), 15 training samples (exits)
// lobe words:             0-2 mean offset from the entry point, 3 cumulative weight within its surface gaussian,
//                         4-12 lower cholesky factor of the covariance (column major), 13-15 projection axis
// internal gaussian words: 0-2 mean, 3 unused, 4-12 whitening matrix W = S^-1 R^T (column major),
//                         13-19 path length field coefficients (see CloudAcc in sssfit.c)

typedef struct {
    const float* positions; // 3 floats per vertex, object space
    const uint32_t* indices; // 3 vertex indices per triangle
    uint32_t vertex_count;
    uint32_t triangle_count;
    float sigma; // free flight density in object space units (world sigma * object scale)
    float g;     // Henyey-Greenstein anisotropy
} SSSFitInput;

typedef enum {
    SSSFIT_SETUP = 0,
    SSSFIT_SURFACE,
    SSSFIT_WALKS,
    SSSFIT_LOBES,
    SSSFIT_CLOUD,
    SSSFIT_OPTIMISE,
    SSSFIT_VALIDATE,
    SSSFIT_DONE,
    SSSFIT_FAILED
} SSSFitPhase;

typedef enum {
    SSSG_H_MAGIC = 0,
    SSSG_H_VERSION,
    SSSG_H_TCOUNT,
    SSSG_H_TABLE,
    SSSG_H_SURF,
    SSSG_H_NSURF,
    SSSG_H_LOBE,
    SSSG_H_NLOBE,
    SSSG_H_GAUSS,
    SSSG_H_NGAUSS,
    SSSG_H_NODE,
    SSSG_H_NNODE,
    SSSG_H_SIGMA,
    SSSG_H_G,
    SSSG_H_MFP,
    SSSG_H_EPS,
    SSSG_H_BG, // 7 background coefficients of the path length field (16..22)
    SSSG_H_BGW = SSSG_H_BG + 7,
    SSSG_H_DMIN,
    SSSG_H_TSTART,
    SSSG_H_MATERIAL,
    SSSG_H_SCALE,
    SSSG_H_WORDS // 28, must stay <= SSSG_HEADER_WORDS (28..31 are zero / unused)
} CloudHeaderMarks;

typedef struct {
    uint32_t surface_gaussians;
    uint32_t internal_gaussians;
    uint32_t walks;
    uint32_t lobes;
    uint32_t max_steps;
    uint32_t epochs;
    uint32_t validation;
    uint32_t seed;
} GSSSConfig;

typedef struct {
    // reference walks
    uint32_t walks_kept;
    uint32_t walks_lost;       // leaked out of a non watertight mesh or hit the step limit
    double mean_steps;
    double events_per_length;  // sum (scatters) / sum (sigma * L), ~1 when scattering behaves like a poisson process
    // fit
    double nll_initial, nll_final;
    double r2_mean;            // fraction of log excess path length variance explained by the cloud
    // validation (model vs held out walks)
    uint32_t validation_samples;
    uint32_t projection_failures;
    double walk_dist_q[4], model_dist_q[4]; // exit distance quantiles 25 / 50 / 75 / 90 %, in mean free paths
    double walk_len_q[4], model_len_q[4];   // path length quantiles, in mean free paths
    // mean throughput at albedo a = 0.5, 0.9, 0.99 (SSS_TP_ALBEDOS), in the units the renderer uses:
    //   tp_walk:    a^scatters of the held out walks (what the random walk computes, the ground truth)
    //   tp_length:  exp(-(1 - a) L) with the walks' own path lengths L in mfp (the length only formula, exact lengths)
    //   tp_model:   exp(-(1 - a) L) with the cloud's sampled path lengths (what the cloud path computes)
    double tp_walk[3], tp_length[3], tp_model[3];
    double walk_cos, model_cos;             // mean cosine between exit direction and outward normal
} SSSFitStats;

typedef struct SSSFitter SSSFitter;

typedef double (*SSSClockFn)(void);

// Copies the mesh. Returns NULL when the input is unusable (no triangles, sigma <= 0).
SSSFitter* SSSFitCreate(const SSSFitInput* input, const GSSSConfig* config);

// Advances the fit for roughly `budget` seconds (as measured by `clock`). Returns the phase it stopped in.
SSSFitPhase SSSFitStep(SSSFitter* f, double budget, SSSClockFn clock);

SSSFitPhase SSSFitPhaseNow(const SSSFitter* f);

float SSSFitProgress(const SSSFitter* f);

const char* SSSFitStatus(const SSSFitter* f);

const SSSFitStats* SSSFitGetStats(const SSSFitter* f);

// Valid once the phase is SSSFIT_DONE. The blob is owned by the fitter; copy it before SSSFitDestroy.
const uint32_t* SSSFitBlob(const SSSFitter* f, uint32_t* words);

void SSSFitDestroy(SSSFitter* f);

// Debugging / research: held out reference walks (after SSSFIT_DONE) and a model sample from the same entry point.
uint32_t SSSFitHeldOut(const SSSFitter* f);
int SSSFitCompare(SSSFitter* f, uint32_t i, float* walk_length, uint32_t* walk_events, float* model_length);

// ---- reference sampler (C mirror of shaders/path.comp) ------------------------------------------------------------
// Ray caster used by the sampler for the exit point projection: closest hit of the object's own triangles.
typedef struct {
    // returns the hit distance (< 0 when nothing was hit within tmax) and the triangle in *tri
    float (*trace)(void* user, const float* origin, const float* dir, float tmax, uint32_t* tri);
    void* user;
    const float* positions;
    const uint32_t* indices;
} SSSCaster;

typedef struct {
    float exit[3];      // object space exit point
    uint32_t exit_tri;
    float normal[3];    // outward normal at the exit
    float dir[3];       // exit direction
    float length;       // path length, object space units
    float chord;
    int projected;      // 0 when the exit point projection failed and the walk exits where it entered
} SSSSample;

typedef float (*SSSRandomFn)(void* state);

// entry point `p` on triangle `tri` (object space)
void sssfit_sample(const uint32_t* blob, const SSSCaster* caster, const float* p, uint32_t tri,
    SSSRandomFn rnd, void* rstate, SSSSample* out);

// Decodes internal gaussian i of a blob: mean, principal axes (column major, right handed), standard deviations and
// the cloud's typical log excess path length at that gaussian (for visualisation). Returns 0 if i is out of range.
int SSSBlobGaussian(const uint32_t* blob, uint32_t i, float* mu, float* axes, float* scales, float* log_excess);

// Validates a blob and returns its total word count (0 when malformed).
uint32_t SSSBlobWords(const uint32_t* blob, uint32_t available);

#endif

#ifndef SUBSURFACE_H
#define SUBSURFACE_H

#include "renderer/vulkan/vstructs.h"

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
