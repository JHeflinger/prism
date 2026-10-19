#ifndef SSSFIT_H
#define SSSFIT_H

// Subsurface Gaussian cloud fitting (CPU core).
//
// Learns, for one closed mesh with a homogeneous scattering medium, a compact set of gaussians that replaces the
// random walk of the path tracer:
//   - surface gaussians: anchored on the mesh surface, looked up in O(1) through a per triangle candidate table.
//     Each one stores (a) a small mixture of 3D gaussian lobes describing where a walk that enters under it leaves
//     the surface, relative to the entry point, and (b) a cosine power lobe describing the direction a walk leaves
//     with when it exits under it.
//   - internal gaussians: a volumetric cloud inside the mesh. Ray marching the chord between the entry and the exit
//     point through the cloud blends per gaussian coefficients (weighted by each gaussian's line integral) into the
//     parameters of a log normal distribution over the extra path length the walk travelled.
// Everything is fitted to geometric random walks (free flight with density sigma, Henyey-Greenstein phase g) traced
// against the mesh itself, in the mesh's local (object) space. Absorption is not part of the fit: the renderer turns
// the sampled path length into throughput, so the material colours can change without retraining.
//
// The file has no renderer dependencies so it can be built and tested on its own (see SSSFIT_STANDALONE).
// sssfit_sample() is a line by line C mirror of the sampler in shaders/path.comp and is used for validation.

#include <stdint.h>
#include <stddef.h>

// ---- packed format (uint32 words, floats stored as raw bits) -------------------------------------------------------
// One object is a self contained blob; every offset inside it is relative to the start of the blob.
#define SSSG_MAGIC 0x47535353u // "SSSG"
#define SSSG_VERSION 1u
#define SSSG_HEADER_WORDS 32u
#define SSSG_TABLE_STRIDE 4u   // per triangle: 3 candidate surface gaussians (0xFFFFFFFF = none), flags
#define SSSG_SURF_STRIDE 16u
#define SSSG_LOBE_STRIDE 16u
#define SSSG_GAUSS_STRIDE 20u
#define SSSG_NODE_STRIDE 8u
#define SSSG_TRI_FLIP 1u       // table flag: the outward normal is -cross(b - a, c - a)
#define SSSG_LEAF 0x80000000u  // node word 7: leaf flag | gaussian count, word 3 = first gaussian
#define SSSG_NONE 0xFFFFFFFFu

// header words
enum {
    SSSG_H_MAGIC = 0, SSSG_H_VERSION, SSSG_H_TCOUNT, SSSG_H_TABLE,
    SSSG_H_SURF, SSSG_H_NSURF, SSSG_H_LOBE, SSSG_H_NLOBE,
    SSSG_H_GAUSS, SSSG_H_NGAUSS, SSSG_H_NODE, SSSG_H_NNODE,
    SSSG_H_SIGMA, SSSG_H_G, SSSG_H_MFP, SSSG_H_EPS,
    SSSG_H_BG,                      // 7 background coefficients of the path length field (16..22)
    SSSG_H_BGW = SSSG_H_BG + 7, SSSG_H_DMIN, SSSG_H_TSTART, SSSG_H_MATERIAL, SSSG_H_SCALE,
    SSSG_H_EVK, SSSG_H_EVC,         // calibrated event count model (see SSSFitStats.albedo_*)
    SSSG_H_WORDS                    // 30, must stay <= SSSG_HEADER_WORDS
};
// surface gaussian words: 0-2 centre, 3 tangential sigma, 4-6 outward normal, 7 normal sigma,
//                         8-10 exit direction axis, 11 exit direction exponent, 12 first lobe, 13 lobe count,
//                         14 training samples (entries), 15 training samples (exits)
// lobe words:             0-2 mean offset from the entry point, 3 cumulative weight within its surface gaussian,
//                         4-12 lower cholesky factor of the covariance (column major), 13-15 projection axis
// internal gaussian words: 0-2 mean, 3 unused, 4-12 whitening matrix W = S^-1 R^T (column major),
//                         13-19 path length field coefficients (see CloudAcc in sssfit.c)

typedef struct {
    uint32_t surface_gaussians;  // number of surface gaussians
    uint32_t internal_gaussians; // number of gaussians in the internal cloud
    uint32_t walks;              // reference random walks traced for training
    uint32_t lobes;              // max exit lobes per surface gaussian
    uint32_t max_steps;          // scattering events before a walk is abandoned
    uint32_t epochs;             // optimisation passes for the path length field
    uint32_t validation;         // samples drawn from the fitted model for the accuracy report (0 = skip)
    uint32_t seed;
} SSSFitConfig;

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

typedef struct {
    // reference walks
    uint32_t walks_kept;
    uint32_t walks_lost;       // leaked out of a non watertight mesh or hit the step limit
    double mean_steps;
    double events_per_length;  // sum (events - 1) / sum (sigma * L), ~1 for the poisson throughput model
    double albedo_true[3];     // E[a^events] over the walks for a = 0.5, 0.9, 0.99 (exact per walk throughput)
    double albedo_model[3];    // E[a^(1 + c) exp(-k sigma (1 - a) L)], the calibrated length only model the renderer uses
    double event_k, event_c;   // calibration of that model
    // fit
    double nll_initial, nll_final;
    double r2_mean;            // fraction of log excess path length variance explained by the cloud
    // validation (model vs held out walks)
    uint32_t validation_samples;
    uint32_t projection_failures;
    double walk_dist_q[4], model_dist_q[4]; // exit distance quantiles 25 / 50 / 75 / 90 %, in mean free paths
    double walk_len_q[4], model_len_q[4];   // path length quantiles, in mean free paths
    double walk_atten[3], model_atten[3];   // E[exp(-k L)] for k = 0.01, 0.1, 1 times sigma
    double walk_cos, model_cos;             // mean cosine between exit direction and outward normal
} SSSFitStats;

typedef struct SSSFitter SSSFitter;

typedef double (*SSSClockFn)(void);

SSSFitConfig SSSFitDefaultConfig();

// Copies the mesh. Returns NULL when the input is unusable (no triangles, sigma <= 0).
SSSFitter* SSSFitCreate(const SSSFitInput* input, const SSSFitConfig* config);

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
