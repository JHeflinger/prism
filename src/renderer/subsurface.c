#include "subsurface.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include <util/logger.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <math.h>
#include <tgpu.h>

#define SSS_RECORD_WORDS 8u
#define SSS_BUFFER_HEADER 4u // words: object count, 3 reserved
#define SSS_ENTRY_WORDS 8u // per object: blob base per channel (3), mesh index, flags, triangle start, channels, 0
#define SSS_SPECTRAL_SPREAD 0.05f // relative spread of sigma_t across rgb above which one cloud per channel is trained
#define SSS_KERNEL_PATH "shaders/gsss.comp.spv"
#define SSS_GROUP 256u
#define SSS_BATCH_FIRST 4096u // items in the first submission of a phase; later ones adapt to SSS_SUBMIT_SECONDS
#define SSS_BATCH_MAX 262144u
#define SSS_SUBMIT_SECONDS 0.014

// cloud blob
#define SSSG_MAGIC 0x47535353u // literally the string "SSSG"
#define SSSG_VERSION 1u
#define SSSG_HEADER_WORDS 32u
#define SSSG_TABLE_STRIDE 4u // per triangle: 3 candidate surface gaussians (0xFFFFFFFF = none), flags
#define SSSG_SURF_STRIDE 16u
#define SSSG_LOBE_STRIDE 16u
#define SSSG_GAUSS_STRIDE 20u
#define SSSG_NODE_STRIDE 8u
#define SSSG_TRI_FLIP 1u // table flag: the outward normal is -cross(b - a, c - a)
#define SSSG_LEAF 0x80000000u // node word 7: leaf flag | gaussian count, word 3 = first gaussian
#define SSSG_NONE 0xFFFFFFFFu

// fit
#define SSS_INTERIOR_CAP 200000u
#define SSS_EM_MAX_POINTS 2048u
#define SSS_SURFACE_SAMPLES_PER 24u
#define SSS_BG_WEIGHT 0.05f // line density of the background term, relative to the peak of a gaussian
#define SSS_EPS_FRACTION 0.1f // excess length floor, in mean free paths
#define SSS_DMIN_FRACTION 0.01f // chords shorter than this (in mean free paths) are evaluated as a point
#define SSS_Q 7

typedef enum {
    SSS_MODE_ORIENT = 0,
    SSS_MODE_WALKS = 1,
    SSS_MODE_CHORD_COUNT = 2,
    SSS_MODE_CHORD_WRITE = 3,
    SSS_MODE_VALIDATE = 4
} SSSMode;

typedef enum { // descriptor bindings of gsss.comp
    SSS_BIND_TRIANGLES = 0,
    SSS_BIND_VERTICES,
    SSS_BIND_NODES,
    SSS_BIND_NORMALS,
    SSS_BIND_BLOB,
    SSS_BIND_CDF,
    SSS_BIND_RECORDS,
    SSS_BIND_FLIPS,
    SSS_BIND_CHORD_META,
    SSS_BIND_CHORD_ENTRIES,
    SSS_BIND_VALIDATION,
    SSS_BIND_COUNT
} SSSDescriptorBinding;

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
} CloudHeaderMark;

typedef struct {
    const vec3* positions; // per vertex, object space
    const uint32_t* indices; // 3 vertex indices per triangle
    uint32_t vertex_count;
    uint32_t triangle_count;
    float sigma; // free flight density in object space units (world sigma * object scale)
    float anisotropy; // Henyey-Greenstein anisotropy
    TGPU* gpu;
    const TGPUKernel* kernel;
} SSSFitInput;

typedef enum {
    SSSFIT_SETUP = 0,
    SSSFIT_ORIENT,
    SSSFIT_SURFACE,
    SSSFIT_WALKS,
    SSSFIT_LOBES,
    SSSFIT_CLOUD,
    SSSFIT_CHORDS,
    SSSFIT_OPTIMISE,
    SSSFIT_VALIDATE,
    SSSFIT_DONE,
    SSSFIT_FAILED
} SSSFitPhase;

typedef enum {
    CRACK_ESCAPE = 0,
    SURFACE_REACH,
    STEP_LIMIT
} LostWalkReason;

typedef struct {
    uint32_t kept;
    uint32_t lost;
    uint32_t lostwhy[3];
    double distance_quantiles[4]; // exit distance Quantiles 25 / 50 / 75 / 90 %, in mean free paths
    double length_quantiles[4]; // path length Quantiles, in mean free paths
    double cosine; // mean cosine between exit direction and outward normal
} WalkStats;

typedef struct {
    double distance_quantiles[4]; // exit distance Quantiles 25 / 50 / 75 / 90 %, in mean free paths
    double length_quantiles[4]; // path length Quantiles, in mean free paths
    double cosine; // mean cosine between exit direction and outward normal
} ModelStats;

typedef struct {
    double walk[3]; // a^scatters of the held out walks (what the random walk computes, the ground truth)
    double length[3]; // exp(-(1 - a) L) with the walks' own path lengths L in mfp (the length only formula, exact lengths)
    double model[3]; // exp(-(1 - a) L) with the cloud's sampled path lengths (what the cloud path computes)
} ThroughputStats; // TLDR - walk vs length is inaccuracy that is faulty with our base logic, model vs length is inaccuracy in training

typedef struct {
    WalkStats walks;
    ModelStats model;
    ThroughputStats throughput;
    double mean_steps; // basically average # of scattering events minus 1
    double events_per_length; // sum (scatters) / sum (sigma * L), ~1 when scattering behaves like a poisson process (SHOULD BE BASICALLY 1, IF NOT, SOMETHING IS WRONG)
    double nll_initial; // negative log likelihood is basically a score of how well the model explains the behavior, less is better!
    double nll_final; // log bc easier/stable to differentiate due to log(ab) = log(a) + log(b), negative bc log maximizes, we want to minimize
    double r2_mean; // fraction of log excess path length variance explained by the cloud (closer to 1 is better, but close to 0 isn't exactly bad! negative is bad!)
    uint32_t validation_samples;
    uint32_t projection_failures;
} SSSFitStats;

typedef struct {
    float entry[4]; // xyz entry point
    float exit[4]; // xyz exit point (on the surface), w path length
    float direction[4]; // xyz exit direction
    float interior[4]; // xyz one of the walk's scattering locations, w 1 when the walk scattered at all
    uint32_t triangles[4]; // entry triangle, exit triangle, entry surface gaussian, exit surface gaussian
    uint32_t info[4]; // free flights (scattering events + 1), 1 when the walk left through the outside, otherwise why it was lost (1 escaped through a crack, 2 reached the surface from outside, 3 step limit)
} SSSWalkRecord;

typedef struct {
    float exit[4]; // xyz exit point, w 1 when the exit point projection succeeded
    float direction[4]; // xyz exit direction, w path length
    float normal[4]; // xyz outward normal at the exit, w chord length
} SSSValidationSample;

typedef struct {
    uint32_t triangles;
    uint32_t normals;
    uint32_t mode;
    uint32_t count;
    uint32_t offset;
    uint32_t seed;
    uint32_t max_steps;
    uint32_t pad0;
    float sigma;
    float anisotropy;
    float eps;
    float pad1;
} SSSTrainPC;

typedef struct {
    float min[3];
    float pad0;
    float max[3];
    uint32_t left;
    uint32_t right;
    uint32_t parent;
    uint32_t counter;
    uint32_t pad1;
} SSSGPUNode;

_Static_assert(sizeof(SSSGPUNode) == sizeof(BVHNode), "SSSGPUNode must match BVHNode");
_Static_assert(offsetof(SSSGPUNode, max) == offsetof(BVHNode, max), "SSSGPUNode must match BVHNode");
_Static_assert(offsetof(SSSGPUNode, left) == offsetof(BVHNode, left), "SSSGPUNode must match BVHNode");
_Static_assert(offsetof(SSSGPUNode, counter) == offsetof(BVHNode, counter), "SSSGPUNode must match BVHNode");
_Static_assert(sizeof(SSSWalkRecord) == 96, "SSSWalkRecord must match the std430 layout in gsss.comp");
_Static_assert(sizeof(SSSValidationSample) == 48, "SSSValidationSample must match the std430 layout in gsss.comp");
_Static_assert(sizeof(SSSTrainPC) <= TGPU_PUSH_SIZE, "push constants too large");

typedef struct {
    vec3 mean; // offset of the exit point from the entry point
    float weight;
    mat3 L; // lower cholesky factor of the covariance
    vec3 axis; // projection axis
} Lobe;

typedef struct {
    vec3 mu; // the center
    mat3 R; // columns = principal axes
    vec3 s; // standard deviations along them
    mat3 W; // whitening matrix S^-1 R^T
    float q[SSS_Q]; // path length field coefficients
} Gauss;

typedef struct {
    vec3 bmin;
    vec3 bmax;
    uint32_t left; // first item for leaves, left child otherwise
    uint32_t count; // items in a leaf, 0 for interior nodes
    uint32_t right;
} BuildNode;

typedef struct {
    float m; // momentum (smooth avg of gradient)
    float v; // smooth avg of gradient squared
} AdamSlot;

typedef double (*ClockFunction)(void);

typedef struct {
    TGPU* gpu;
    const TGPUKernel* kernel;
    TGPUBuffer buffers[SSS_BIND_COUNT];
    uint32_t batch; // items per submission, adapted to SSS_SUBMIT_SECONDS
    uint32_t cursor; // progress of the current GPU phase
} FitterGPU;

typedef struct {
    const vec3* positions;
    const uint32_t* indices;
    uint32_t vertex_count;
    uint32_t triangle_count;
    vec3* normals;
    uint8_t* flips;
    double* cdf; // (cumulative distribution function) area of trianlges from 0 to i combined
    double area; // entire surface area
    float sigma;
    float anisotropy;
    float mfp; // mean free path (1/sigma), the units we do things in basically
    float diagonal; // bounding box diagonal, basically the extents
    float epsilon; // mesh-specific epsilon, based on diagonal
} FitterMesh;

typedef struct {
    uint32_t count;
    vec3* centers;
    vec3* normals;
    float* tangent_spreads; // tangential sigma
    float* normal_spreads; // normal sigma
    uint32_t* table; // 3 candidates per triangle!
    vec3* exit_axies; // exit direction lobe axis
    float* exit_exponents; // exit direction lobe exponents
    uint32_t* first_lobes; // first lobe index for each gaussian
    uint32_t* lobe_counts; // how many lobes per gaussian
    uint32_t* walks_entered; // how many walks entered each surface gaussian
    uint32_t* walks_exited; // how many walks exited each surface gaussian
    Lobe* lobes;
    uint32_t lobe_count;
    uint32_t lobe_capacity;
    uint32_t* bucket_starts; // training buckets - start index
    uint32_t* bucket_sizes; // training buckets - each bucket size
    uint32_t cursor; // current gaussian being worked on
} FitterSurface;

typedef struct {
    vec3* centers;
    uint32_t* assignments;
    double* sums;
    float spacing;
    uint32_t iteration;
} EtherealCloud;

typedef struct {
    Gauss* gaussians;
    uint32_t count;
    BuildNode* nodes;
    uint32_t node_count;
    float background[SSS_Q];
} TangibleCloud;

typedef struct {
    EtherealCloud ethereal;
    TangibleCloud tangible;
} FitterCloud;

typedef struct {
    vec3* points;
    uint32_t count;
    uint32_t seen;
} ScatterRecord;

typedef struct {
    SSSWalkRecord* records;
    uint32_t records_count;
    uint32_t target;
    uint32_t attempts;
    uint32_t holdout; // records [holdout, nrec) are held out for validation
    double steps; // sums used for calculating mean steps and events per length
    double events;
    double length;
    ScatterRecord interior;
} FitterWalks;

typedef struct {
    uint32_t walkcount;
    uint32_t* starts;
    uint32_t* indices;
    float* integrals;
    float* background_weights;
    float* excess_length;
    float* log_length;
    uint32_t entries_used;
    uint32_t entry_capacity;
    float initial_fit[SSS_Q];
    AdamSlot* adam;
    float* gradient;
    uint32_t epoch;
} FitterOptimization;

typedef struct {
    uint32_t count;
    float* model_exit_distance;
    float* model_path_length;
    float* walk_exit_distance;
    float* walk_path_length;
    double model_cosine;
    double walk_cosine;
} FitterValidation;

typedef struct {
    uint32_t* blob;
    uint32_t words;
} FitterResult;

typedef struct SSSFitter {
    GSSSConfig cfg;
    SSSFitPhase phase;
    char status[192];
    SSSFitStats stats;
    LittleRNG rng;
    FitterGPU fgpu;
    FitterMesh mesh;
    FitterSurface surface;
    FitterCloud cloud;
    FitterWalks walks;
    FitterOptimization optimizer;
    FitterValidation validater;
    FitterResult result;
} SSSFitter;

typedef struct {
    uint32_t node;
    uint32_t start;
    uint32_t end;
} BuildTask;

typedef struct {
    const vec3* points;
    uint32_t count;
    vec3 origin;
    float length;
    int dimensions[3];
    uint32_t* starts; // cell -> first item (prefix sums), size cells + 1
    uint32_t* items;
} Grid;

typedef struct {
    float m1;
    float s1;
    float m2;
    float s2;
    float w1;
} LengthModel; // 2 normal distributions basically (mean + std dev for each, and weight w for one)

typedef struct {
    uint32_t mesh;
    BOOL enabled;
    uint32_t channels; // 1 or 3
    uint32_t* blob[3];
    uint32_t words[3];
    float sigma[3]; // world space density each blob was trained for
    uint32_t tstart; // triangle range the cloud was trained on (re-synced when meshes move)
    uint32_t tcount;
    MaterialID material;
    float anisotropy;
    float scale; // object scale it was trained at
} Cloud;

typedef struct {
    SubsurfaceTrainPhase phase;
    SSSFitter* fitter;
    size_t mesh;
    uint32_t tstart;
    uint32_t tcount;
    MaterialID material;
    float anisotropy;
    float scale;
    uint32_t channels;
    uint32_t channel; // channel currently being fitted
    float sigma[3];
    GSSSConfig config;
    vec3* positions; // object space mesh, kept for the per channel fits (the fitter borrows it)
    uint32_t* indices;
    uint32_t vertex_count;
    TGPU gpu; // compute context shared by the per channel fits (the fitter borrows it)
    TGPUKernel kernel;
    BOOL gpu_alive;
    uint32_t* blob[3];
    uint32_t words[3];
    char status[192];
    SSSFitStats stats;
    BOOL have_stats;
    double started;
} CloudTrainJob;

static const double SSS_TP_ALBEDOS[3] = { 0.5, 0.9, 0.99 };
static Cloud* g_clouds = NULL;
static size_t g_cloud_count = 0;
static CloudTrainJob g_job = { 0 };
static BOOL g_update_gsss = FALSE;
static float* g_sort_keys = NULL; // centroid keys for the SelectNth

static void SetStatus(SSSFitter* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(f->status, sizeof(f->status), fmt, ap);
    va_end(ap);
}

static void FailFitting(SSSFitter* f, const char* why) {
    SetStatus(f, "Failed: %s", why);
    f->phase = SSSFIT_FAILED;
}

static BOOL RecreateBuffer(SSSFitter* f, uint32_t binding, size_t bytes) {
    TGPUBuffer* b = &f->fgpu.buffers[binding];
    if (bytes < 16) bytes = 16;
    if (b->buffer && b->size >= bytes) return TRUE;
    tgpu_buffer_destroy(f->fgpu.gpu, b);
    if (tgpu_buffer_create(f->fgpu.gpu, b, bytes)) {
        FailFitting(f, "out of GPU memory");
        return FALSE;
    }
    tgpu_bind(f->fgpu.gpu, binding, b);
    return TRUE;
}

static BOOL UploadBuffer(SSSFitter* f, uint32_t binding, const void* data, size_t bytes) {
    if (!RecreateBuffer(f, binding, bytes)) return FALSE;
    if (bytes > 0 && tgpu_upload(f->fgpu.gpu, &f->fgpu.buffers[binding], 0, data, bytes)) {
        FailFitting(f, "GPU upload failed");
        return FALSE;
    }
    return TRUE;
}

static BOOL DownloadBuffer(SSSFitter* f, uint32_t binding, void* data, size_t bytes) {
    if (bytes > 0 && tgpu_download(f->fgpu.gpu, &f->fgpu.buffers[binding], 0, data, bytes)) {
        FailFitting(f, "GPU download failed");
        return FALSE;
    }
    return TRUE;
}

static BOOL SubmitKernel(SSSFitter* f, uint32_t mode, uint32_t offset, uint32_t count) {
    SSSTrainPC pc = {
        f->mesh.triangle_count, 0u, mode, count,
        offset, f->cfg.seed ^ F2U(f->mesh.sigma), f->cfg.max_steps, 0u,
        f->mesh.sigma, f->mesh.anisotropy, f->mesh.epsilon, 0.0f
    };
    tgpu_begin(f->fgpu.gpu);
    tgpu_dispatch(f->fgpu.gpu, f->fgpu.kernel, (count + SSS_GROUP - 1u) / SSS_GROUP, 1, 1, &pc, sizeof(pc));
    if (tgpu_submit(f->fgpu.gpu)) {
        FailFitting(f, "GPU submission failed");
        return FALSE;
    }
    return TRUE;
}

static void AdaptBatch(SSSFitter* f, double took) {
    if (took < 0.5 * SSS_SUBMIT_SECONDS && f->fgpu.batch < SSS_BATCH_MAX) f->fgpu.batch *= 2u;
    else if (took > 2.0 * SSS_SUBMIT_SECONDS && f->fgpu.batch > SSS_GROUP) f->fgpu.batch /= 2u;
}

static void SelectNth(uint32_t* a, int64_t lo, int64_t hi, int64_t nth) {
    while (hi > lo) {
        float pivot = g_sort_keys[a[(lo + hi) / 2]];
        int64_t i = lo, j = hi;
        while (i <= j) {
            while (g_sort_keys[a[i]] < pivot) i++;
            while (g_sort_keys[a[j]] > pivot) j--;
            if (i <= j) { uint32_t t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (nth <= j) hi = j;
        else if (nth >= i) lo = i;
        else return;
    }
}

static BuildNode* BuildBVH(uint32_t n, const vec3* bmins, const vec3* bmaxs, uint32_t leafsize, uint32_t* order, uint32_t* out_nodes) {
    BuildNode* nodes = EZ_MINALLOC((size_t)n * 2 + 1, sizeof(BuildNode));
    float* keys = EZ_MINALLOC(n, sizeof(float));
    BuildTask* stack = EZ_MINALLOC(128 + (size_t)n, sizeof(BuildTask));
    for (uint32_t i = 0; i < n; i++) order[i] = i;
    uint32_t sp = 0, used = 1;
    stack[sp++] = (BuildTask){ 0, 0, n };
    while (sp) {
        BuildTask t = stack[--sp];
        BuildNode* nd = &nodes[t.node];
        vec3 cmin = { FLT_MAX, FLT_MAX, FLT_MAX }, cmax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        glm_vec3_copy(cmin, nd->bmin);
        glm_vec3_copy(cmax, nd->bmax);
        for (uint32_t i = t.start; i < t.end; i++) {
            uint32_t p = order[i];
            vec3 c;
            glm_vec3_center((float*)bmins[p], (float*)bmaxs[p], c);
            glm_vec3_minv(nd->bmin, (float*)bmins[p], nd->bmin);
            glm_vec3_maxv(nd->bmax, (float*)bmaxs[p], nd->bmax);
            glm_vec3_minv(cmin, c, cmin);
            glm_vec3_maxv(cmax, c, cmax);
        }
        uint32_t count = t.end - t.start;
        if (count <= leafsize) {
            nd->left = t.start;
            nd->count = count;
            continue;
        }
        vec3 ext;
        glm_vec3_sub(cmax, cmin, ext);
        int axis = ext[1] > ext[0] ? 1 : 0;
        if (ext[2] > ext[axis]) axis = 2;
        for (uint32_t i = t.start; i < t.end; i++) {
            uint32_t p = order[i];
            keys[p] = 0.5f * (bmins[p][axis] + bmaxs[p][axis]);
        }
        g_sort_keys = keys;
        uint32_t mid = t.start + count / 2;
        SelectNth(order, t.start, (int64_t)t.end - 1, mid);
        uint32_t l = used++, r = used++;
        nd->left = l;
        nd->right = r;
        nd->count = 0;
        stack[sp++] = (BuildTask){ l, t.start, mid };
        stack[sp++] = (BuildTask){ r, mid, t.end };
    }
    g_sort_keys = NULL;
    EZ_FREE(keys);
    EZ_FREE(stack);
    *out_nodes = used;
    return nodes;
}

static void TriangleCorners(const SSSFitter* f, uint32_t t, vec3 a, vec3 b, vec3 c) {
    glm_vec3_copy((float*)f->mesh.positions[f->mesh.indices[t * 3]], a);
    glm_vec3_copy((float*)f->mesh.positions[f->mesh.indices[t * 3 + 1]], b);
    glm_vec3_copy((float*)f->mesh.positions[f->mesh.indices[t * 3 + 2]], c);
}

static SSSGPUNode* IsolatedBVH(const SSSFitter* f, uint32_t* count) {
    vec3* mins = EZ_MINALLOC(f->mesh.triangle_count, sizeof(vec3));
    vec3* maxs = EZ_MINALLOC(f->mesh.triangle_count, sizeof(vec3));
    for (uint32_t t = 0; t < f->mesh.triangle_count; t++) {
        vec3 a, b, c;
        TriangleCorners(f, t, a, b, c);
        glm_vec3_minv(a, b, mins[t]);
        glm_vec3_minv(mins[t], c, mins[t]);
        glm_vec3_maxv(a, b, maxs[t]);
        glm_vec3_maxv(maxs[t], c, maxs[t]);
    }
    uint32_t* order = EZ_MINALLOC(f->mesh.triangle_count, sizeof(uint32_t));
    uint32_t n = 0;
    BuildNode* nodes = BuildBVH(f->mesh.triangle_count, mins, maxs, 1, order, &n);
    SSSGPUNode* out = EZ_MINALLOC(n, sizeof(SSSGPUNode));
    for (uint32_t i = 0; i < n; i++) {
        glm_vec3_copy(nodes[i].bmin, out[i].min);
        glm_vec3_copy(nodes[i].bmax, out[i].max);
        if (nodes[i].count) {
            out[i].left = order[nodes[i].left];
            out[i].right = 0xFFFFFFFFu;
        } else {
            out[i].left = nodes[i].left;
            out[i].right = nodes[i].right;
        }
        out[i].parent = 0;
        out[i].counter = 0;
    }
    EZ_FREE(nodes);
    EZ_FREE(order);
    EZ_FREE(mins);
    EZ_FREE(maxs);
    *count = n;
    return out;
}

static void BuildGrid(Grid* g, const vec3* pts, uint32_t n, float h) {
    memset(g, 0, sizeof(*g));
    g->points = pts;
    g->count = n;
    vec3 bmin = { FLT_MAX, FLT_MAX, FLT_MAX }, bmax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (uint32_t i = 0; i < n; i++) {
        glm_vec3_minv(bmin, (float*)pts[i], bmin);
        glm_vec3_maxv(bmax, (float*)pts[i], bmax);
    }
    if (n == 0) {
        glm_vec3_zero(bmin);
        glm_vec3_one(bmax);
    }
    vec3 ext;
    glm_vec3_sub(bmax, bmin, ext);
    float maxext = glm_vec3_max(ext);
    if (!(h > 0.0f)) h = maxext > 0 ? maxext : 1.0f;
    for (;;) {
        double cells = 1.0;
        for (int k = 0; k < 3; k++) cells *= floor(ext[k] / h) + 1.0;
        if (cells <= 4.0 * (double)n + 64.0 && cells <= 16777216.0) break;
        h *= 1.25f;
    }
    g->length = h;
    glm_vec3_copy(bmin, g->origin);
    for (int k = 0; k < 3; k++) g->dimensions[k] = (int)floorf(ext[k] / h) + 1;
    size_t cells = (size_t)g->dimensions[0] * g->dimensions[1] * g->dimensions[2];
    g->starts = EZ_MINALLOC(cells + 1, sizeof(uint32_t));
    g->items = EZ_MINALLOC(n, sizeof(uint32_t));
    uint32_t* cellof = EZ_MINALLOC(n, sizeof(uint32_t));
    for (uint32_t i = 0; i < n; i++) {
        int c[3];
        for (int k = 0; k < 3; k++) c[k] = CLAMP((int)floorf((pts[i][k] - g->origin[k]) / h), 0, g->dimensions[k] - 1);
        cellof[i] = (uint32_t)(c[0] + g->dimensions[0] * (c[1] + g->dimensions[1] * c[2]));
        g->starts[cellof[i] + 1]++;
    }
    for (size_t c = 0; c < cells; c++) g->starts[c + 1] += g->starts[c];
    uint32_t* cursor = EZ_MINALLOC(cells, sizeof(uint32_t));
    for (uint32_t i = 0; i < n; i++) g->items[g->starts[cellof[i]] + cursor[cellof[i]]++] = i;
    EZ_FREE(cursor);
    EZ_FREE(cellof);
}

static void FreeGrid(Grid* g) {
    EZ_FREE(g->starts);
    EZ_FREE(g->items);
    memset(g, 0, sizeof(*g));
}

static uint32_t GridLocale(const Grid* g, const vec3 q, uint32_t k, uint32_t* out, float* outd2) {
    if (g->count == 0 || k == 0) return 0;
    int c[3];
    for (int a = 0; a < 3; a++) c[a] = CLAMP((int)floorf((q[a] - g->origin[a]) / g->length), 0, g->dimensions[a] - 1);
    uint32_t found = 0;
    int maxr = g->dimensions[0];
    if (g->dimensions[1] > maxr) maxr = g->dimensions[1];
    if (g->dimensions[2] > maxr) maxr = g->dimensions[2];
    for (int r = 0; r <= maxr; r++) {
        for (int z = c[2] - r; z <= c[2] + r; z++) {
            if (z < 0 || z >= g->dimensions[2]) continue;
            for (int y = c[1] - r; y <= c[1] + r; y++) {
                if (y < 0 || y >= g->dimensions[1]) continue;
                for (int x = c[0] - r; x <= c[0] + r; x++) {
                    if (x < 0 || x >= g->dimensions[0]) continue;
                    if (abs(x - c[0]) != r && abs(y - c[1]) != r && abs(z - c[2]) != r) continue; // shell only
                    size_t cell = (size_t)x + (size_t)g->dimensions[0] * ((size_t)y + (size_t)g->dimensions[1] * (size_t)z);
                    for (uint32_t i = g->starts[cell]; i < g->starts[cell + 1]; i++) {
                        uint32_t id = g->items[i];
                        float d2 = glm_vec3_distance2((float*)g->points[id], (float*)q);
                        if (found == k && d2 >= outd2[k - 1]) continue;
                        uint32_t j = found < k ? found++ : k - 1;
                        while (j > 0 && outd2[j - 1] > d2) { outd2[j] = outd2[j - 1]; out[j] = out[j - 1]; j--; }
                        outd2[j] = d2;
                        out[j] = id;
                    }
                }
            }
        }
        float reach = (float)r * g->length;
        if (found == k && outd2[k - 1] <= reach * reach) break;
    }
    return found;
}

static void SSSFitDestroy(SSSFitter* f) {
    if (!f) return;
    for (uint32_t b = 0; b < SSS_BIND_COUNT; b++) tgpu_buffer_destroy(f->fgpu.gpu, &f->fgpu.buffers[b]);
    EZ_FREE(f->mesh.normals);
    EZ_FREE(f->mesh.flips);
    EZ_FREE(f->mesh.cdf);
    EZ_FREE(f->surface.centers);
    EZ_FREE(f->surface.normals);
    EZ_FREE(f->surface.tangent_spreads);
    EZ_FREE(f->surface.normal_spreads);
    EZ_FREE(f->surface.table);
    EZ_FREE(f->surface.exit_axies);
    EZ_FREE(f->surface.exit_exponents);
    EZ_FREE(f->surface.first_lobes);
    EZ_FREE(f->surface.lobe_counts);
    EZ_FREE(f->surface.walks_entered);
    EZ_FREE(f->surface.walks_exited);
    EZ_FREE(f->surface.lobes);
    EZ_FREE(f->surface.bucket_starts);
    EZ_FREE(f->surface.bucket_sizes);
    EZ_FREE(f->cloud.ethereal.centers);
    EZ_FREE(f->cloud.ethereal.assignments);
    EZ_FREE(f->cloud.ethereal.sums);
    EZ_FREE(f->walks.records);
    EZ_FREE(f->walks.interior.points);
    EZ_FREE(f->cloud.tangible.gaussians);
    EZ_FREE(f->cloud.tangible.nodes);
    EZ_FREE(f->optimizer.starts);
    EZ_FREE(f->optimizer.indices);
    EZ_FREE(f->optimizer.integrals);
    EZ_FREE(f->optimizer.background_weights);
    EZ_FREE(f->optimizer.excess_length);
    EZ_FREE(f->optimizer.log_length);
    EZ_FREE(f->optimizer.adam);
    EZ_FREE(f->optimizer.gradient);
    EZ_FREE(f->validater.model_exit_distance);
    EZ_FREE(f->validater.model_path_length);
    EZ_FREE(f->validater.walk_exit_distance);
    EZ_FREE(f->validater.walk_path_length);
    EZ_FREE(f->result.blob);
    EZ_FREE(f);
}

static SSSFitter* SSSFitCreate(const SSSFitInput* in, const GSSSConfig* cfg) {
    if (!in || !cfg || in->triangle_count == 0 || in->vertex_count == 0 || !(in->sigma > 0.0f)) return NULL;
    if (!in->positions || !in->indices || !in->gpu || !in->kernel) return NULL;
    for (size_t i = 0; i < (size_t)in->triangle_count * 3; i++) if (in->indices[i] >= in->vertex_count) return NULL;
    SSSFitter* f = EZ_MINALLOC(1, sizeof(SSSFitter));
    f->cfg = *cfg;
    if (f->cfg.surface_gaussians < 4) f->cfg.surface_gaussians = 4;
    if (f->cfg.internal_gaussians < 4) f->cfg.internal_gaussians = 4;
    if (f->cfg.walks < 1000) f->cfg.walks = 1000;
    if (f->cfg.lobes < 1) f->cfg.lobes = 1;
    if (f->cfg.lobes > 8) f->cfg.lobes = 8;
    if (f->cfg.max_steps < 64) f->cfg.max_steps = 64;
    f->mesh.vertex_count = in->vertex_count;
    f->mesh.triangle_count = in->triangle_count;
    f->mesh.positions = in->positions;
    f->mesh.indices = in->indices;
    f->mesh.sigma = in->sigma;
    f->mesh.anisotropy = CLAMP(in->anisotropy, -0.99f, 0.99f);
    f->mesh.mfp = 1.0f / in->sigma;
    f->fgpu.gpu = in->gpu;
    f->fgpu.kernel = in->kernel;
    f->rng.seed = 0x853c49e6748fea9bull ^ ((uint64_t)f->cfg.seed * 0x9E3779B97F4A7C15ull);
    f->fgpu.batch = SSS_BATCH_FIRST;
    for (uint32_t b = 0; b < SSS_BIND_COUNT; b++) {
        if (!RecreateBuffer(f, b, 16)) {
            SSSFitDestroy(f);
            return NULL;
        }
    }
    f->phase = SSSFIT_SETUP;
    SetStatus(f, "Preparing mesh");
    return f;
}

static const char* SSSFitStatus(const SSSFitter* f) {
    return f ? f->status : "No fitter";
}

static const SSSFitStats* SSSFitGetStats(const SSSFitter* f) {
    return f ? &f->stats : NULL;
}

static const uint32_t* SSSFitBlob(const SSSFitter* f, uint32_t* words) {
    if (!f || f->phase != SSSFIT_DONE) { if (words) *words = 0; return NULL; }
    if (words) *words = f->result.words;
    return f->result.blob;
}

static float SSSFitProgress(const SSSFitter* f) {
    if (!f) return 0.0f;
    switch (f->phase) {
        case SSSFIT_SETUP: return 0.0f;
        case SSSFIT_ORIENT: return 0.01f + 0.02f * (float)f->fgpu.cursor / (float)(f->mesh.triangle_count ? f->mesh.triangle_count : 1);
        case SSSFIT_SURFACE: return 0.03f;
        case SSSFIT_WALKS: return 0.05f + 0.50f * (float)f->walks.records_count / (float)(f->walks.target ? f->walks.target : 1);
        case SSSFIT_LOBES: return 0.55f + 0.05f * (float)f->surface.cursor / (float)(f->surface.count ? f->surface.count : 1);
        case SSSFIT_CLOUD: return 0.60f + 0.05f * (float)f->cloud.ethereal.iteration / 10.0f;
        case SSSFIT_CHORDS: return 0.65f + 0.10f * (float)f->fgpu.cursor / (float)(f->optimizer.walkcount ? f->optimizer.walkcount : 1);
        case SSSFIT_OPTIMISE: return 0.75f + 0.20f * (float)f->optimizer.epoch / (float)(f->cfg.epochs ? f->cfg.epochs : 1);
        case SSSFIT_VALIDATE: return 0.95f + 0.05f * (float)f->fgpu.cursor / (float)(f->validater.count ? f->validater.count : 1);
        case SSSFIT_DONE: return 1.0f;
        default: return 0.0f;
    }
}

static void BarycentricPoint(const SSSFitter* f, uint32_t t, float r1, float r2, vec3 out) {
    vec3 a, b, c;
    TriangleCorners(f, t, a, b, c);
    float s = sqrtf(r1);
    glm_vec3_scale(a, 1.0f - s, out);
    glm_vec3_muladds(b, s * (1.0f - r2), out);
    glm_vec3_muladds(c, s * r2, out);
}

static uint32_t RandomWeightedTriangle(const SSSFitter* f, float r) { // takes into account area!
    double target = (double)r * f->mesh.area;
    uint32_t lo = 0, hi = f->mesh.triangle_count - 1;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (f->mesh.cdf[mid] < target) lo = mid + 1; else hi = mid;
    }
    return lo;
}

static void PackBlob(SSSFitter* f) {
    uint32_t table = SSSG_HEADER_WORDS;
    uint32_t surf = table + f->mesh.triangle_count * SSSG_TABLE_STRIDE;
    uint32_t lobe = surf + f->surface.count * SSSG_SURF_STRIDE;
    uint32_t gauss = lobe + f->surface.lobe_count * SSSG_LOBE_STRIDE;
    uint32_t node = gauss + f->cloud.tangible.count * SSSG_GAUSS_STRIDE;
    uint32_t total = node + f->cloud.tangible.node_count * SSSG_NODE_STRIDE;
    // before the internal cloud exists (walk phase) there are no gaussians or nodes to pack
    if (!f->cloud.tangible.gaussians) {
        node = gauss;
        total = gauss;
    }
    if (!f->result.blob || f->result.words != total) {
        EZ_FREE(f->result.blob);
        f->result.blob = EZ_MINALLOC(total, sizeof(uint32_t));
        f->result.words = total;
    }
    uint32_t* b = f->result.blob;
    memset(b, 0, (size_t)total * 4);
    b[SSSG_H_MAGIC] = SSSG_MAGIC;
    b[SSSG_H_VERSION] = SSSG_VERSION;
    b[SSSG_H_TCOUNT] = f->mesh.triangle_count;
    b[SSSG_H_TABLE] = table;
    b[SSSG_H_SURF] = surf;
    b[SSSG_H_NSURF] = f->surface.count;
    b[SSSG_H_LOBE] = lobe;
    b[SSSG_H_NLOBE] = f->surface.lobe_count;
    b[SSSG_H_GAUSS] = gauss;
    b[SSSG_H_NGAUSS] = f->cloud.tangible.gaussians ? f->cloud.tangible.count : 0;
    b[SSSG_H_NODE] = node;
    b[SSSG_H_NNODE] = f->cloud.tangible.gaussians ? f->cloud.tangible.node_count : 0;
    b[SSSG_H_SIGMA] = F2U(f->mesh.sigma);
    b[SSSG_H_G] = F2U(f->mesh.anisotropy);
    b[SSSG_H_MFP] = F2U(f->mesh.mfp);
    b[SSSG_H_EPS] = F2U(SSS_EPS_FRACTION * f->mesh.mfp);
    for (int k = 0; k < SSS_Q; k++) b[SSSG_H_BG + k] = F2U(f->cloud.tangible.background[k]);
    b[SSSG_H_BGW] = F2U(SSS_BG_WEIGHT);
    b[SSSG_H_DMIN] = F2U(SSS_DMIN_FRACTION * f->mesh.mfp);
    b[SSSG_H_TSTART] = SSSG_NONE;
    b[SSSG_H_MATERIAL] = SSSG_NONE;
    b[SSSG_H_SCALE] = F2U(1.0f);
    for (uint32_t t = 0; t < f->mesh.triangle_count; t++) {
        uint32_t* r = b + table + t * SSSG_TABLE_STRIDE;
        r[0] = f->surface.table[t * 3];
        r[1] = f->surface.table[t * 3 + 1];
        r[2] = f->surface.table[t * 3 + 2];
        r[3] = f->mesh.flips[t] ? SSSG_TRI_FLIP : 0u;
    }
    for (uint32_t c = 0; c < f->surface.count; c++) {
        uint32_t* r = b + surf + c * SSSG_SURF_STRIDE;
        for (int k = 0; k < 3; k++) {
            r[k] = F2U(f->surface.centers[c][k]);
            r[4 + k] = F2U(f->surface.normals[c][k]);
            r[8 + k] = F2U(f->surface.exit_axies ? f->surface.exit_axies[c][k] : f->surface.normals[c][k]);
        }
        r[3] = F2U(f->surface.tangent_spreads[c]);
        r[7] = F2U(f->surface.normal_spreads[c]);
        r[11] = F2U(f->surface.exit_exponents ? f->surface.exit_exponents[c] : 1.0f);
        r[12] = f->surface.first_lobes ? f->surface.first_lobes[c] : 0;
        r[13] = f->surface.lobe_counts ? f->surface.lobe_counts[c] : 0;
        r[14] = f->surface.walks_entered ? f->surface.walks_entered[c] : 0;
        r[15] = f->surface.walks_exited ? f->surface.walks_exited[c] : 0;
        // cumulative lobe weights within this gaussian
        if (f->surface.lobe_counts) {
            float cum = 0;
            for (uint32_t j = 0; j < f->surface.lobe_counts[c]; j++) {
                const Lobe* l = &f->surface.lobes[f->surface.first_lobes[c] + j];
                uint32_t* lr = b + lobe + (f->surface.first_lobes[c] + j) * SSSG_LOBE_STRIDE;
                cum += l->weight;
                for (int k = 0; k < 3; k++) {
                    lr[k] = F2U(l->mean[k]);
                    lr[13 + k] = F2U(l->axis[k]);
                }
                lr[3] = F2U(j + 1 == f->surface.lobe_counts[c] ? 1.0f : cum);
                for (int col = 0; col < 3; col++)
                    for (int row = 0; row < 3; row++)
                        lr[4 + col * 3 + row] = F2U(l->L[col][row]);
            }
        }
    }
    if (!f->cloud.tangible.gaussians) return;
    for (uint32_t c = 0; c < f->cloud.tangible.count; c++) {
        uint32_t* r = b + gauss + c * SSSG_GAUSS_STRIDE;
        const Gauss* g = &f->cloud.tangible.gaussians[c];
        for (int k = 0; k < 3; k++) r[k] = F2U(g->mu[k]);
        for (int col = 0; col < 3; col++)
            for (int row = 0; row < 3; row++)
                r[4 + col * 3 + row] = F2U(g->W[col][row]);
        for (int k = 0; k < SSS_Q; k++) r[13 + k] = F2U(g->q[k]);
    }
    for (uint32_t n = 0; n < f->cloud.tangible.node_count; n++) {
        uint32_t* r = b + node + n * SSSG_NODE_STRIDE;
        const BuildNode* nd = &f->cloud.tangible.nodes[n];
        for (int k = 0; k < 3; k++) {
            r[k] = F2U(nd->bmin[k]);
            r[4 + k] = F2U(nd->bmax[k]);
        }
        r[3] = nd->left;
        r[7] = nd->count ? (SSSG_LEAF | nd->count) : nd->right;
    }
}

static void SetupPhase(SSSFitter* f) {
    vec3 bmin = { FLT_MAX, FLT_MAX, FLT_MAX };
    vec3 bmax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (uint32_t i = 0; i < f->mesh.vertex_count; i++) {
        glm_vec3_minv(bmin, (float*)f->mesh.positions[i], bmin);
        glm_vec3_maxv(bmax, (float*)f->mesh.positions[i], bmax);
    }
    f->mesh.diagonal = glm_vec3_distance(bmin, bmax);
    if (!(f->mesh.diagonal > 0.0f)) {
        FailFitting(f, "mesh has no extent");
        return;
    }
    f->mesh.epsilon = 1e-5f * f->mesh.diagonal;
    f->mesh.cdf = EZ_MINALLOC(f->mesh.triangle_count, sizeof(double));
    double total = 0.0;
    for (uint32_t t = 0; t < f->mesh.triangle_count; t++) {
        vec3 a, b, c;
        TriangleCorners(f, t, a, b, c);
        total += TriangleArea(a, b, c);
        f->mesh.cdf[t] = total;
    }
    f->mesh.area = total;
    if (!(total > 0.0)) {
        FailFitting(f, "mesh has no surface area");
        return;
    }
    float (*verts)[4] = EZ_MINALLOC(f->mesh.vertex_count, sizeof(float[4]));
    for (uint32_t i = 0; i < f->mesh.vertex_count; i++) glm_vec3_copy((float*)f->mesh.positions[i], verts[i]);
    Triangle* tris = EZ_MINALLOC(f->mesh.triangle_count, sizeof(Triangle));
    float* cdf = EZ_MINALLOC(f->mesh.triangle_count, sizeof(float));
    for (uint32_t t = 0; t < f->mesh.triangle_count; t++) {
        tris[t].a = f->mesh.indices[t * 3];
        tris[t].b = f->mesh.indices[t * 3 + 1];
        tris[t].c = f->mesh.indices[t * 3 + 2];
        tris[t].an = tris[t].bn = tris[t].cn = 0xFFFFFFFFu;
        tris[t].material = 0;
        cdf[t] = (float)(f->mesh.cdf[t] / total);
    }
    cdf[f->mesh.triangle_count - 1] = 1.0f;
    uint32_t nnodes = 0;
    SSSGPUNode* nodes = IsolatedBVH(f, &nnodes);
    BOOL ok = UploadBuffer(f, SSS_BIND_TRIANGLES, tris, (size_t)f->mesh.triangle_count * sizeof(Triangle)) &&
              UploadBuffer(f, SSS_BIND_VERTICES, verts, (size_t)f->mesh.vertex_count * sizeof(float[4])) &&
              UploadBuffer(f, SSS_BIND_NODES, nodes, (size_t)nnodes * sizeof(SSSGPUNode)) &&
              UploadBuffer(f, SSS_BIND_CDF, cdf, (size_t)f->mesh.triangle_count * sizeof(float)) &&
              RecreateBuffer(f, SSS_BIND_FLIPS, (size_t)f->mesh.triangle_count * sizeof(uint32_t));
    EZ_FREE(verts);
    EZ_FREE(tris);
    EZ_FREE(cdf);
    EZ_FREE(nodes);
    if (!ok) return;
    f->fgpu.cursor = 0;
    f->fgpu.batch = SSS_BATCH_FIRST;
    f->phase = SSSFIT_ORIENT;
    SetStatus(f, "Orienting the surface");
}

static void OrientPhase(SSSFitter* f, double budget, ClockFunction clock) {
    double start = clock();
    while (f->fgpu.cursor < f->mesh.triangle_count) {
        uint32_t n = MIN(f->fgpu.batch, f->mesh.triangle_count - f->fgpu.cursor);
        double t0 = clock();
        if (!SubmitKernel(f, SSS_MODE_ORIENT, f->fgpu.cursor, n)) return;
        AdaptBatch(f, clock() - t0);
        f->fgpu.cursor += n;
        SetStatus(f, "Orienting the surface %u/%u", f->fgpu.cursor, f->mesh.triangle_count);
        if (clock() - start >= budget) return;
    }
    uint32_t* flips = EZ_MINALLOC(f->mesh.triangle_count, sizeof(uint32_t));
    if (!DownloadBuffer(f, SSS_BIND_FLIPS, flips, (size_t)f->mesh.triangle_count * sizeof(uint32_t))) {
        EZ_FREE(flips);
        return;
    }
    f->mesh.normals = EZ_MINALLOC(f->mesh.triangle_count, sizeof(vec3));
    f->mesh.flips = EZ_MINALLOC(f->mesh.triangle_count, 1);
    for (uint32_t t = 0; t < f->mesh.triangle_count; t++) {
        vec3 a, b, c, e1, e2;
        TriangleCorners(f, t, a, b, c);
        glm_vec3_sub(b, a, e1);
        glm_vec3_sub(c, a, e2);
        glm_vec3_cross(e1, e2, f->mesh.normals[t]); // winding normal, flipped below where the parity test says it points inwards
        if (!(glm_vec3_norm(f->mesh.normals[t]) > 0.0f)) {
            glm_vec3_copy(GLM_ZUP, f->mesh.normals[t]);
            continue;
        }
        glm_vec3_normalize(f->mesh.normals[t]);
        if (flips[t]) {
            f->mesh.flips[t] = 1;
            glm_vec3_negate(f->mesh.normals[t]);
        }
    }
    EZ_FREE(flips);
    f->phase = SSSFIT_SURFACE;
    SetStatus(f, "Clustering surface gaussians");
}

static void SurfacePhase(SSSFitter* f) {
    uint32_t ns = f->cfg.surface_gaussians;
    uint32_t m = ns * SSS_SURFACE_SAMPLES_PER;
    if (m < 20000) m = 20000;
    vec3* sp = EZ_MINALLOC(m, sizeof(vec3));
    vec3* snr = EZ_MINALLOC(m, sizeof(vec3));
    for (uint32_t i = 0; i < m; i++) {
        uint32_t t = RandomWeightedTriangle(f, RandomFloat(&f->rng));
        float r1 = RandomFloat(&f->rng);
        float r2 = RandomFloat(&f->rng);
        BarycentricPoint(f, t, r1, r2, sp[i]);
        glm_vec3_copy(f->mesh.normals[t], snr[i]);
    }
    if (ns > m) ns = m;
    f->surface.count = ns;
    f->surface.centers = EZ_MINALLOC(ns, sizeof(vec3));
    f->surface.normals = EZ_MINALLOC(ns, sizeof(vec3));
    f->surface.tangent_spreads = EZ_MINALLOC(ns, sizeof(float));
    f->surface.normal_spreads = EZ_MINALLOC(ns, sizeof(float));
    // samples are already random: the first ns seed the clusters
    memcpy(f->surface.centers, sp, (size_t)ns * sizeof(vec3));
    memcpy(f->surface.normals, snr, (size_t)ns * sizeof(vec3));
    float spacing = (float)sqrt(f->mesh.area / (double)ns);
    uint32_t* assign = EZ_MINALLOC(m, sizeof(uint32_t));
    double* acc = EZ_MINALLOC((size_t)ns * 7, sizeof(double));
    for (int iter = 0; iter < 12; iter++) {
        Grid grid;
        BuildGrid(&grid, f->surface.centers, ns, spacing);
        for (uint32_t i = 0; i < m; i++) {
            uint32_t ids[8] = { 0 };
            float d2[8];
            uint32_t k = GridLocale(&grid, sp[i], 8, ids, d2);
            uint32_t best = ids[0];
            float bestv = FLT_MAX;
            for (uint32_t j = 0; j < k; j++) {
                float pen = glm_vec3_dot(snr[i], f->surface.normals[ids[j]]) > 0.3f ? 1.0f : 16.0f;
                if (d2[j] * pen < bestv) { bestv = d2[j] * pen; best = ids[j]; }
            }
            assign[i] = best;
        }
        FreeGrid(&grid);
        memset(acc, 0, (size_t)ns * 7 * sizeof(double));
        for (uint32_t i = 0; i < m; i++) {
            double* a = &acc[assign[i] * 7];
            for (int k = 0; k < 3; k++) { a[k] += sp[i][k]; a[3 + k] += snr[i][k]; }
            a[6] += 1.0;
        }
        for (uint32_t c = 0; c < ns; c++) {
            double* a = &acc[c * 7];
            if (a[6] < 0.5) {
                uint32_t r = RandomU32(&f->rng) % m; // empty cluster: reseed on a random sample
                glm_vec3_copy(sp[r], f->surface.centers[c]);
                glm_vec3_copy(snr[r], f->surface.normals[c]);
                continue;
            }
            for (int k = 0; k < 3; k++) { f->surface.centers[c][k] = (float)(a[k] / a[6]); f->surface.normals[c][k] = (float)a[3 + k]; }
            glm_vec3_normalize(f->surface.normals[c]);
            if (glm_vec3_norm(f->surface.normals[c]) < 0.5f) glm_vec3_copy(snr[c], f->surface.normals[c]);
        }
    }
    // footprint of each cluster
    memset(acc, 0, (size_t)ns * 7 * sizeof(double));
    for (uint32_t i = 0; i < m; i++) {
        uint32_t c = assign[i];
        vec3 d;
        glm_vec3_sub(sp[i], f->surface.centers[c], d);
        float dn = glm_vec3_dot(d, f->surface.normals[c]);
        acc[c * 7 + 0] += fmax(glm_vec3_norm2(d) - dn * dn, 0.0);
        acc[c * 7 + 1] += dn * dn;
        acc[c * 7 + 6] += 1.0;
    }
    for (uint32_t c = 0; c < ns; c++) {
        double n = acc[c * 7 + 6] > 0 ? acc[c * 7 + 6] : 1.0;
        float st = (float)sqrt(acc[c * 7 + 0] / n * 0.5);
        float sn = (float)sqrt(acc[c * 7 + 1] / n);
        st = CLAMP(st, 0.4f * spacing, 2.0f * spacing);
        f->surface.tangent_spreads[c] = st;
        f->surface.normal_spreads[c] = fmaxf(sn, 0.25f * st);
    }
    // per triangle candidate table: nearest clusters on the same side of the surface
    f->surface.table = EZ_MINALLOC((size_t)f->mesh.triangle_count * 3, sizeof(uint32_t));
    Grid grid;
    BuildGrid(&grid, f->surface.centers, ns, spacing);
    for (uint32_t t = 0; t < f->mesh.triangle_count; t++) {
        vec3 a, b, c, cen;
        TriangleCorners(f, t, a, b, c);
        glm_vec3_add(a, b, cen);
        glm_vec3_add(cen, c, cen);
        glm_vec3_scale(cen, 1.0f / 3.0f, cen);
        uint32_t ids[16];
        float d2[16];
        uint32_t k = GridLocale(&grid, cen, 16, ids, d2);
        uint32_t* row = &f->surface.table[t * 3];
        row[0] = row[1] = row[2] = SSSG_NONE;
        uint32_t filled = 0;
        for (uint32_t j = 0; j < k && filled < 3; j++)
            if (glm_vec3_dot(f->mesh.normals[t], f->surface.normals[ids[j]]) > 0.25f) row[filled++] = ids[j];
        if (filled == 0) row[0] = ids[0];
    }
    FreeGrid(&grid);
    EZ_FREE(acc);
    EZ_FREE(assign);
    EZ_FREE(sp);
    EZ_FREE(snr);
    // the walks need the triangle table (flip flags) and the surface gaussians on the GPU
    PackBlob(f);
    if (!UploadBuffer(f, SSS_BIND_BLOB, f->result.blob, (size_t)f->result.words * sizeof(uint32_t))) return;
    f->walks.target = f->cfg.walks;
    f->walks.records = EZ_MINALLOC(f->walks.target, sizeof(SSSWalkRecord));
    f->walks.interior.points = EZ_MINALLOC(SSS_INTERIOR_CAP, sizeof(vec3));
    f->fgpu.batch = SSS_BATCH_FIRST;
    f->phase = SSSFIT_WALKS;
    SetStatus(f, "Tracing reference walks 0/%u", f->walks.target);
}

static void AddInteriorPoint(SSSFitter* f, const vec3 p) {
    f->walks.interior.seen++;
    if (f->walks.interior.count < SSS_INTERIOR_CAP) {
        glm_vec3_copy((float*)p, f->walks.interior.points[f->walks.interior.count++]);
        return;
    }
    uint64_t r = ((uint64_t)RandomU32(&f->rng) << 32 | RandomU32(&f->rng)) % f->walks.interior.seen;
    if (r < SSS_INTERIOR_CAP) glm_vec3_copy((float*)p, f->walks.interior.points[r]);
}

static void WalksPhase(SSSFitter* f, double budget, ClockFunction clock) {
    double start = clock();
    while (f->walks.records_count < f->walks.target) {
        uint32_t want = f->walks.target - f->walks.records_count;
        uint32_t n = MIN(f->fgpu.batch, want + want / 16 + 64); // a few spare attempts for walks that leak
        if (!RecreateBuffer(f, SSS_BIND_RECORDS, (size_t)n * sizeof(SSSWalkRecord))) return;
        double t0 = clock();
        if (!SubmitKernel(f, SSS_MODE_WALKS, f->walks.attempts, n)) return;
        SSSWalkRecord* out = EZ_MINALLOC(n, sizeof(SSSWalkRecord));
        if (!DownloadBuffer(f, SSS_BIND_RECORDS, out, (size_t)n * sizeof(SSSWalkRecord))) {
            EZ_FREE(out);
            return;
        }
        AdaptBatch(f, clock() - t0);
        for (uint32_t i = 0; i < n; i++) {
            const SSSWalkRecord* r = &out[i];
            f->walks.attempts++;
            f->walks.steps += r->info[0];
            if (!r->info[1]) {
                f->stats.walks.lost++;
                if (r->info[2] >= 1 && r->info[2] <= 3) f->stats.walks.lostwhy[r->info[2] - 1]++;
                continue;
            }
            if (f->walks.records_count >= f->walks.target) continue;
            f->walks.events += (double)r->info[0] - 1.0;
            f->walks.length += (double)f->mesh.sigma * r->exit[3];
            if (r->interior[3] > 0.0f) AddInteriorPoint(f, r->interior);
            f->walks.records[f->walks.records_count++] = *r;
        }
        EZ_FREE(out);
        if (f->walks.attempts > 64 && f->stats.walks.lost > f->walks.attempts / 2) {
            FailFitting(f, "more than half of the walks escaped, the mesh is probably not closed");
            return;
        }
        SetStatus(f, "Tracing reference walks %u/%u", f->walks.records_count, f->walks.target);
        if (clock() - start >= budget) return;
    }
    f->stats.walks.kept = f->walks.records_count;
    f->stats.mean_steps = f->walks.steps / (double)(f->walks.attempts ? f->walks.attempts : 1);
    f->stats.events_per_length = f->walks.events / fmax(f->walks.length, 1e-9);
    f->walks.holdout = f->walks.records_count - f->walks.records_count / 10;
    f->phase = SSSFIT_LOBES;
    SetStatus(f, "Fitting exit lobes");
}

static void DefaultLobe(const vec3 n, float mfp, Lobe* l) {
    memset(l, 0, sizeof(*l));
    double c[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            c[i][j] = (double)mfp * mfp * ((i == j ? 1.0 : 0.0) - 0.99 * n[i] * n[j]);
    Cholesky3(c, l->L);
    glm_vec3_copy((float*)n, l->axis);
    l->weight = 1.0f;
}

static uint32_t FitLobes(SSSFitter* f, const vec3* pts, const vec3* nrm, uint32_t n, uint32_t K, const vec3 fallback_n, Lobe* out) { // EM algorithm
    if (n < 4) {
        DefaultLobe(fallback_n, f->mesh.mfp, &out[0]);
        return 1;
    }
    if (K > n / 24) K = n / 24;
    if (K < 1) K = 1;
    double reg = (0.02 * f->mesh.mfp) * (0.02 * f->mesh.mfp) + 1e-14 * (double)f->mesh.diagonal * f->mesh.diagonal;
    double mu[8][3], cov[8][3][3], pi[8];
    // k-means++ seeding of the means
    uint32_t first = RandomU32(&f->rng) % n;
    for (int k = 0; k < 3; k++) mu[0][k] = pts[first][k];
    double* dmin = EZ_MINALLOC(n, sizeof(double));
    for (uint32_t i = 0; i < n; i++) dmin[i] = DBL_MAX;
    for (uint32_t c = 1; c < K; c++) {
        double total = 0.0;
        for (uint32_t i = 0; i < n; i++) {
            double d2 = 0;
            for (int k = 0; k < 3; k++) { double d = pts[i][k] - mu[c - 1][k]; d2 += d * d; }
            if (d2 < dmin[i]) dmin[i] = d2;
            total += dmin[i];
        }
        double x = RandomFloat(&f->rng) * total;
        uint32_t pick = n - 1;
        for (uint32_t i = 0; i < n; i++) { x -= dmin[i]; if (x <= 0) { pick = i; break; } }
        for (int k = 0; k < 3; k++) mu[c][k] = pts[pick][k];
    }
    EZ_FREE(dmin);
    // global covariance as the starting point of every component
    double gm[3] = { 0, 0, 0 }, gc[3][3] = { { 0 } };
    for (uint32_t i = 0; i < n; i++) for (int k = 0; k < 3; k++) gm[k] += pts[i][k] / n;
    for (uint32_t i = 0; i < n; i++)
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++) gc[a][b] += (pts[i][a] - gm[a]) * (pts[i][b] - gm[b]) / n;
    for (uint32_t c = 0; c < K; c++) {
        pi[c] = 1.0 / K;
        for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) cov[c][a][b] = gc[a][b] / (double)K + (a == b ? reg : 0.0);
    }
    double* resp = EZ_MINALLOC((size_t)n * K, sizeof(double));
    for (int iter = 0; iter < 60; iter++) {
        // E step
        double inv[8][3][3], logdet[8];
        for (uint32_t c = 0; c < K; c++) {
            double (*m)[3] = cov[c];
            double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
            det = fmax(det, 1e-300);
            logdet[c] = log(det);
            inv[c][0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
            inv[c][0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
            inv[c][0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
            inv[c][1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
            inv[c][1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
            inv[c][1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
            inv[c][2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
            inv[c][2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
            inv[c][2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
        }
        for (uint32_t i = 0; i < n; i++) {
            double best = -DBL_MAX, lp[8];
            for (uint32_t c = 0; c < K; c++) {
                double d[3] = { pts[i][0] - mu[c][0], pts[i][1] - mu[c][1], pts[i][2] - mu[c][2] };
                double q = 0;
                for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) q += d[a] * inv[c][a][b] * d[b];
                lp[c] = log(fmax(pi[c], 1e-300)) - 0.5 * logdet[c] - 0.5 * q;
                if (lp[c] > best) best = lp[c];
            }
            double s = 0;
            for (uint32_t c = 0; c < K; c++) { lp[c] = exp(lp[c] - best); s += lp[c]; }
            for (uint32_t c = 0; c < K; c++) resp[(size_t)i * K + c] = lp[c] / s;
        }
        // M step
        for (uint32_t c = 0; c < K; c++) {
            double rs = 0, m[3] = { 0, 0, 0 };
            for (uint32_t i = 0; i < n; i++) {
                double r = resp[(size_t)i * K + c];
                rs += r;
                for (int k = 0; k < 3; k++) m[k] += r * pts[i][k];
            }
            if (rs < 1e-9) { pi[c] = 0; continue; }
            for (int k = 0; k < 3; k++) mu[c][k] = m[k] / rs;
            double cv[3][3] = { { 0 } };
            for (uint32_t i = 0; i < n; i++) {
                double r = resp[(size_t)i * K + c];
                double d[3] = { pts[i][0] - mu[c][0], pts[i][1] - mu[c][1], pts[i][2] - mu[c][2] };
                for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) cv[a][b] += r * d[a] * d[b];
            }
            for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) cov[c][a][b] = cv[a][b] / rs + (a == b ? reg : 0.0);
            pi[c] = rs / n;
        }
    }
    uint32_t used = 0;
    for (uint32_t c = 0; c < K; c++) {
        if (pi[c] < 1e-3) continue;
        Lobe* l = &out[used];
        memset(l, 0, sizeof(*l));
        for (int k = 0; k < 3; k++) l->mean[k] = (float)mu[c][k];
        l->weight = (float)pi[c];
        Cholesky3((const double (*)[3])cov[c], l->L);
        vec3 a = { 0, 0, 0 };
        for (uint32_t i = 0; i < n; i++) glm_vec3_muladds((float*)nrm[i], (float)resp[(size_t)i * K + c], a);
        if (glm_vec3_norm(a) < 1e-6f) glm_vec3_copy((float*)fallback_n, a);
        glm_vec3_normalize_to(a, l->axis);
        used++;
    }
    EZ_FREE(resp);
    if (used == 0) { DefaultLobe(fallback_n, f->mesh.mfp, &out[0]); return 1; }
    float total = 0;
    for (uint32_t c = 0; c < used; c++) total += out[c].weight;
    for (uint32_t c = 0; c < used; c++) out[c].weight /= total;
    return used;
}

static void LobesPhase(SSSFitter* f, double budget, ClockFunction clock) {
    double tstart = clock();
    uint32_t ns = f->surface.count, ntrain = f->walks.holdout;
    if (!f->surface.bucket_starts) {
        // surface gaussians under each walk's entry and exit were picked on the GPU (sss_pick_surface)
        f->surface.walks_entered = EZ_MINALLOC(ns, sizeof(uint32_t));
        f->surface.walks_exited = EZ_MINALLOC(ns, sizeof(uint32_t));
        for (uint32_t i = 0; i < ntrain; i++) {
            f->surface.walks_entered[f->walks.records[i].triangles[2]]++;
            f->surface.walks_exited[f->walks.records[i].triangles[3]]++;
        }
        // bucket training records by entry gaussian
        uint32_t* bstart = EZ_MINALLOC(ns + 1, sizeof(uint32_t));
        for (uint32_t c = 0; c < ns; c++) bstart[c + 1] = bstart[c] + f->surface.walks_entered[c];
        uint32_t* items = EZ_MINALLOC(ntrain, sizeof(uint32_t));
        uint32_t* cur = EZ_MINALLOC(ns, sizeof(uint32_t));
        for (uint32_t i = 0; i < ntrain; i++) {
            uint32_t c = f->walks.records[i].triangles[2];
            items[bstart[c] + cur[c]++] = i;
        }
        EZ_FREE(cur);
        f->surface.first_lobes = EZ_MINALLOC(ns, sizeof(uint32_t));
        f->surface.lobe_counts = EZ_MINALLOC(ns, sizeof(uint32_t));
        f->surface.lobe_capacity = ns * f->cfg.lobes;
        f->surface.lobes = EZ_MINALLOC(f->surface.lobe_capacity, sizeof(Lobe));
        f->surface.bucket_starts = bstart;
        f->surface.bucket_sizes = items;
        f->surface.cursor = 0;
    }
    uint32_t* bstart = f->surface.bucket_starts;
    uint32_t* items = f->surface.bucket_sizes;
    vec3* pts = EZ_MINALLOC(SSS_EM_MAX_POINTS, sizeof(vec3));
    vec3* nrm = EZ_MINALLOC(SSS_EM_MAX_POINTS, sizeof(vec3));
    for (; f->surface.cursor < ns; f->surface.cursor++) {
        uint32_t c = f->surface.cursor;
        if ((c & 7u) == 0u && c > 0) {
            SetStatus(f, "Fitting exit lobes %u/%u", c, ns);
            if (clock() - tstart >= budget) {
                EZ_FREE(pts);
                EZ_FREE(nrm);
                return;
            }
        }
        uint32_t cnt = bstart[c + 1] - bstart[c];
        uint32_t take = MIN(cnt, SSS_EM_MAX_POINTS);
        for (uint32_t j = 0; j < take; j++) {
            uint32_t pickj = take == cnt ? j : (uint32_t)(((uint64_t)j * cnt) / take); // even subsample
            const SSSWalkRecord* w = &f->walks.records[items[bstart[c] + pickj]];
            glm_vec3_sub((float*)w->exit, (float*)w->entry, pts[j]);
            glm_vec3_copy(f->mesh.normals[w->triangles[1]], nrm[j]);
        }
        f->surface.first_lobes[c] = f->surface.lobe_count;
        f->surface.lobe_counts[c] = FitLobes(f, pts, nrm, take, f->cfg.lobes, f->surface.normals[c], &f->surface.lobes[f->surface.lobe_count]);
        f->surface.lobe_count += f->surface.lobe_counts[c];
    }
    EZ_FREE(pts);
    EZ_FREE(nrm);
    // exit directions: cosine power lobe per gaussian, fitted from the walks that left under it
    f->surface.exit_axies = EZ_MINALLOC(ns, sizeof(vec3));
    f->surface.exit_exponents = EZ_MINALLOC(ns, sizeof(float));
    double* dsum = EZ_MINALLOC((size_t)ns * 3, sizeof(double));
    for (uint32_t i = 0; i < ntrain; i++) {
        const SSSWalkRecord* w = &f->walks.records[i];
        for (int k = 0; k < 3; k++) dsum[w->triangles[3] * 3 + k] += w->direction[k];
    }
    double* csum = EZ_MINALLOC(ns, sizeof(double));
    for (uint32_t c = 0; c < ns; c++) {
        vec3 a = { (float)dsum[c * 3], (float)dsum[c * 3 + 1], (float)dsum[c * 3 + 2] };
        if (f->surface.walks_exited[c] < 8 || glm_vec3_norm(a) < 1e-6f) glm_vec3_copy(f->surface.normals[c], a);
        glm_vec3_normalize_to(a, f->surface.exit_axies[c]);
    }
    for (uint32_t i = 0; i < ntrain; i++) {
        const SSSWalkRecord* w = &f->walks.records[i];
        csum[w->triangles[3]] += glm_vec3_dot((float*)w->direction, f->surface.exit_axies[w->triangles[3]]);
    }
    for (uint32_t c = 0; c < ns; c++) {
        if (f->surface.walks_exited[c] < 8) {
            f->surface.exit_exponents[c] = 1.0f;
            continue;
        }
        double m = csum[c] / f->surface.walks_exited[c];
        // E[cos] of a normalised cos^e lobe is (e + 1) / (e + 2)
        double e = m >= 0.995 ? 200.0 : (2.0 * m - 1.0) / (1.0 - m);
        f->surface.exit_exponents[c] = (float)fmin(fmax(e, 0.0), 200.0);
    }
    EZ_FREE(dsum);
    EZ_FREE(csum);
    f->phase = SSSFIT_CLOUD;
    SetStatus(f, "Building internal cloud");
}

static void ModelFromSums(float C, const float* Q, float u, LengthModel* lm) { // DO NOT EDIT UNLESS ALSO EDITING SHADER LOGIC
    float inv = 1.0f / C;
    lm->m1 = (Q[0] + u * Q[1]) * inv;
    lm->s1 = expf(CLAMP(Q[2] * inv, -6.0f, 4.0f));
    lm->m2 = (Q[3] + u * Q[4]) * inv;
    lm->s2 = expf(CLAMP(Q[5] * inv, -6.0f, 4.0f));
    lm->w1 = 1.0f / (1.0f + expf(-CLAMP(Q[6] * inv, -20.0f, 20.0f)));
}

static void PrecomputeWhitening(Gauss* g) {
    // W = S^-1 R^T: W[col][row] = R^T[col][row] / s[row] = R[row][col] / s[row]
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++)
            g->W[col][row] = g->R[row][col] / g->s[row];
}

static void CloudPhase(SSSFitter* f, double budget, ClockFunction clock) {
    double start = clock();
    uint32_t n = f->walks.interior.count;
    if (!f->cloud.ethereal.centers) {
        uint32_t ngw = f->cfg.internal_gaussians;
        if (ngw > n / 4) ngw = n / 4;
        if (ngw < 1) ngw = 1;
        f->cloud.tangible.count = ngw;
        f->cloud.ethereal.centers = EZ_MINALLOC(ngw, sizeof(vec3));
        f->cloud.ethereal.assignments = EZ_MINALLOC(n, sizeof(uint32_t));
        f->cloud.ethereal.sums = EZ_MINALLOC((size_t)ngw * 4, sizeof(double));
        for (uint32_t c = 0; c < ngw; c++) glm_vec3_copy(f->walks.interior.points[n ? (c * (n / ngw)) % n : 0], f->cloud.ethereal.centers[c]);
        // spacing estimate from the bounding volume of the seeds
        vec3 vb0 = { FLT_MAX, FLT_MAX, FLT_MAX }, vb1 = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (uint32_t i = 0; i < n; i++) {
            glm_vec3_minv(vb0, f->walks.interior.points[i], vb0);
            glm_vec3_maxv(vb1, f->walks.interior.points[i], vb1);
        }
        double vol = 1.0;
        for (int k = 0; k < 3; k++) vol *= fmax(vb1[k] - vb0[k], 1e-6 * f->mesh.diagonal);
        f->cloud.ethereal.spacing = (float)cbrt(vol / ngw);
        f->cloud.ethereal.iteration = 0;
    }
    uint32_t ng = f->cloud.tangible.count;
    vec3* centres = f->cloud.ethereal.centers;
    uint32_t* assign = f->cloud.ethereal.assignments;
    double* acc = f->cloud.ethereal.sums;
    float spacing = f->cloud.ethereal.spacing;
    // k-means over the scattering locations, one iteration per time slice
    while (f->cloud.ethereal.iteration < 10 && n > 0) {
        Grid grid;
        BuildGrid(&grid, centres, ng, spacing);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t id = 0;
            float d2;
            GridLocale(&grid, f->walks.interior.points[i], 1, &id, &d2);
            assign[i] = id;
        }
        FreeGrid(&grid);
        memset(acc, 0, (size_t)ng * 4 * sizeof(double));
        for (uint32_t i = 0; i < n; i++) {
            double* a = &acc[assign[i] * 4];
            for (int k = 0; k < 3; k++) a[k] += f->walks.interior.points[i][k];
            a[3] += 1.0;
        }
        for (uint32_t c = 0; c < ng; c++) {
            if (acc[c * 4 + 3] < 0.5) { glm_vec3_copy(f->walks.interior.points[RandomU32(&f->rng) % n], centres[c]); continue; }
            for (int k = 0; k < 3; k++) centres[c][k] = (float)(acc[c * 4 + k] / acc[c * 4 + 3]);
        }
        f->cloud.ethereal.iteration++;
        SetStatus(f, "Clustering the internal cloud %u/10", f->cloud.ethereal.iteration);
        if (clock() - start >= budget) return;
    }
    f->cloud.tangible.gaussians = EZ_MINALLOC(ng, sizeof(Gauss));
    f->cloud.ethereal.centers = NULL;
    f->cloud.ethereal.assignments = NULL;
    f->cloud.ethereal.sums = NULL;
    // covariance of every cluster -> oriented gaussian
    double* cov = EZ_MINALLOC((size_t)ng * 9, sizeof(double));
    memset(acc, 0, (size_t)ng * 4 * sizeof(double));
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = assign[i];
        vec3 d;
        glm_vec3_sub(f->walks.interior.points[i], centres[c], d);
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++)
                cov[c * 9 + a * 3 + b] += d[a] * d[b];
        acc[c * 4 + 3] += 1.0;
    }
    Grid cgrid;
    BuildGrid(&cgrid, centres, ng, spacing);
    for (uint32_t c = 0; c < ng; c++) {
        Gauss* g = &f->cloud.tangible.gaussians[c];
        glm_vec3_copy(centres[c], g->mu);
        uint32_t ids[2];
        float d2[2];
        uint32_t k = GridLocale(&cgrid, g->mu, 2, ids, d2);
        float nn = k > 1 ? sqrtf(d2[1]) : spacing;
        float iso = fmaxf(0.75f * nn, 0.05f * f->mesh.mfp);
        double cnt = acc[c * 4 + 3];
        if (cnt < 6) {
            glm_mat3_identity(g->R);
            glm_vec3_fill(g->s, iso);
        } else {
            double a[3][3], ev[3], V[3][3];
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++)
                    a[i][j] = cov[c * 9 + i * 3 + j] / cnt;
            Jacobi3(a, ev, V);
            for (int i = 0; i < 3; i++) g->s[i] = 1.4f * (float)sqrt(fmax(ev[i], 0.0));
            float smin = fmaxf(0.3f * glm_vec3_max(g->s), 0.05f * f->mesh.mfp);
            for (int i = 0; i < 3; i++) g->s[i] = fmaxf(g->s[i], smin);
            for (int col = 0; col < 3; col++)
                for (int row = 0; row < 3; row++)
                    g->R[col][row] = (float)V[row][col];
        }
        PrecomputeWhitening(g);
    }
    FreeGrid(&cgrid);
    EZ_FREE(cov);
    EZ_FREE(acc);
    EZ_FREE(assign);
    EZ_FREE(centres);
    // acceleration structure over the 3 sigma boxes, gaussians reordered into leaf order
    vec3* mins = EZ_MINALLOC(ng, sizeof(vec3));
    vec3* maxs = EZ_MINALLOC(ng, sizeof(vec3));
    for (uint32_t c = 0; c < ng; c++) {
        Gauss* g = &f->cloud.tangible.gaussians[c];
        for (int row = 0; row < 3; row++) {
            double v = 0; // Sigma_rr = sum_k (R_rk s_k)^2
            for (int k = 0; k < 3; k++) { double e = g->R[k][row] * g->s[k]; v += e * e; }
            float ext = 3.0f * (float)sqrt(v);
            mins[c][row] = g->mu[row] - ext;
            maxs[c][row] = g->mu[row] + ext;
        }
    }
    uint32_t* order = EZ_MINALLOC(ng, sizeof(uint32_t));
    f->cloud.tangible.nodes = BuildBVH(ng, mins, maxs, 4, order, &f->cloud.tangible.node_count);
    Gauss* sorted = EZ_MINALLOC(ng, sizeof(Gauss));
    for (uint32_t i = 0; i < ng; i++) sorted[i] = f->cloud.tangible.gaussians[order[i]];
    EZ_FREE(f->cloud.tangible.gaussians);
    f->cloud.tangible.gaussians = sorted;
    EZ_FREE(order);
    EZ_FREE(mins);
    EZ_FREE(maxs);
    // starting model: one global regression of the log excess length on log chord length
    f->optimizer.walkcount = f->walks.holdout;
    f->optimizer.starts = EZ_MINALLOC((size_t)f->optimizer.walkcount + 1, sizeof(uint32_t));
    f->optimizer.background_weights = EZ_MINALLOC(f->optimizer.walkcount, sizeof(float));
    f->optimizer.excess_length = EZ_MINALLOC(f->optimizer.walkcount, sizeof(float));
    f->optimizer.log_length = EZ_MINALLOC(f->optimizer.walkcount, sizeof(float));
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    float epsl = SSS_EPS_FRACTION * f->mesh.mfp;
    for (uint32_t i = 0; i < f->optimizer.walkcount; i++) {
        const SSSWalkRecord* w = &f->walks.records[i];
        float D = glm_vec3_distance((float*)w->exit, (float*)w->entry);
        f->optimizer.excess_length[i] = logf(fmaxf(w->exit[3] - D, 0.0f) + epsl);
        f->optimizer.log_length[i] = logf(1.0f + D / f->mesh.mfp);
        sx += f->optimizer.log_length[i]; sy += f->optimizer.excess_length[i]; sxx += f->optimizer.log_length[i] * f->optimizer.log_length[i]; sxy += f->optimizer.log_length[i] * f->optimizer.excess_length[i];
    }
    double nn = f->optimizer.walkcount > 0 ? f->optimizer.walkcount : 1;
    double den = sxx - sx * sx / nn;
    double b1 = den > 1e-12 ? (sxy - sx * sy / nn) / den : 0.0;
    double b0 = (sy - b1 * sx) / nn;
    double ss = 0;
    for (uint32_t i = 0; i < f->optimizer.walkcount; i++) { double r = f->optimizer.excess_length[i] - (b0 + b1 * f->optimizer.log_length[i]); ss += r * r; }
    double sd = sqrt(ss / nn);
    // two components straddling the global fit; the optimiser separates short and long walks
    f->optimizer.initial_fit[0] = (float)(b0 - 0.6 * sd);
    f->optimizer.initial_fit[1] = (float)b1;
    f->optimizer.initial_fit[2] = (float)log(fmax(0.7 * sd, 1e-3));
    f->optimizer.initial_fit[3] = (float)(b0 + 0.6 * sd);
    f->optimizer.initial_fit[4] = (float)b1;
    f->optimizer.initial_fit[5] = (float)log(fmax(0.7 * sd, 1e-3));
    f->optimizer.initial_fit[6] = 0.0f;
    for (uint32_t c = 0; c < ng; c++) memcpy(f->cloud.tangible.gaussians[c].q, f->optimizer.initial_fit, sizeof(f->optimizer.initial_fit));
    memcpy(f->cloud.tangible.background, f->optimizer.initial_fit, sizeof(f->optimizer.initial_fit));
    f->optimizer.entry_capacity = f->optimizer.walkcount * 8 + 1024;
    f->optimizer.indices = EZ_MINALLOC(f->optimizer.entry_capacity, sizeof(uint32_t));
    f->optimizer.integrals = EZ_MINALLOC(f->optimizer.entry_capacity, sizeof(float));
    // the chord traversal reads the packed nodes / gaussians
    PackBlob(f);
    if (!UploadBuffer(f, SSS_BIND_BLOB, f->result.blob, (size_t)f->result.words * sizeof(uint32_t))) return;
    f->fgpu.cursor = 0;
    f->fgpu.batch = SSS_BATCH_FIRST;
    f->phase = SSSFIT_CHORDS;
    SetStatus(f, "Integrating chords through the internal cloud");
}

static void ChordPhase(SSSFitter* f, double budget, ClockFunction clock) {
    double start = clock();
    while (f->fgpu.cursor < f->optimizer.walkcount) {
        uint32_t n = MIN(f->fgpu.batch, f->optimizer.walkcount - f->fgpu.cursor);
        double t0 = clock();
        // pass 1: how many gaussians each chord passes, and the chord length of the background term
        if (!UploadBuffer(f, SSS_BIND_RECORDS, &f->walks.records[f->fgpu.cursor], (size_t)n * sizeof(SSSWalkRecord))) return;
        if (!RecreateBuffer(f, SSS_BIND_CHORD_META, (size_t)n * 4 * sizeof(uint32_t))) return;
        if (!SubmitKernel(f, SSS_MODE_CHORD_COUNT, f->fgpu.cursor, n)) return;
        uint32_t* meta = EZ_MINALLOC((size_t)n * 4, sizeof(uint32_t));
        if (!DownloadBuffer(f, SSS_BIND_CHORD_META, meta, (size_t)n * 4 * sizeof(uint32_t))) { EZ_FREE(meta); return; }
        uint32_t total = 0;
        for (uint32_t i = 0; i < n; i++) {
            meta[i * 4 + 1] = total;
            total += meta[i * 4];
            f->optimizer.background_weights[f->fgpu.cursor + i] = SSS_BG_WEIGHT * U2F(meta[i * 4 + 2]);
            f->optimizer.starts[f->fgpu.cursor + i] = f->optimizer.entries_used + meta[i * 4 + 1];
        }
        // pass 2: the (gaussian, line integral) pairs, at the offsets just computed
        if (total > 0) {
            uint32_t* entries = EZ_MINALLOC((size_t)total * 2, sizeof(uint32_t));
            BOOL ok = UploadBuffer(f, SSS_BIND_CHORD_META, meta, (size_t)n * 4 * sizeof(uint32_t)) &&
                      RecreateBuffer(f, SSS_BIND_CHORD_ENTRIES, (size_t)total * 2 * sizeof(uint32_t)) &&
                      SubmitKernel(f, SSS_MODE_CHORD_WRITE, f->fgpu.cursor, n) &&
                      DownloadBuffer(f, SSS_BIND_CHORD_ENTRIES, entries, (size_t)total * 2 * sizeof(uint32_t));
            if (!ok) {
                EZ_FREE(entries);
                EZ_FREE(meta);
                return;
            }
            if (f->optimizer.entries_used + total > f->optimizer.entry_capacity) {
                uint32_t newcap = (f->optimizer.entries_used + total) * 2;
                f->optimizer.indices = EZ_REALLOC(f->optimizer.indices, newcap, sizeof(uint32_t));
                f->optimizer.integrals = EZ_REALLOC(f->optimizer.integrals, newcap, sizeof(float));
                f->optimizer.entry_capacity = newcap;
            }
            for (uint32_t e = 0; e < total; e++) {
                f->optimizer.indices[f->optimizer.entries_used + e] = entries[e * 2];
                f->optimizer.integrals[f->optimizer.entries_used + e] = U2F(entries[e * 2 + 1]);
            }
            f->optimizer.entries_used += total;
            EZ_FREE(entries);
        }
        EZ_FREE(meta);
        AdaptBatch(f, clock() - t0);
        f->fgpu.cursor += n;
        SetStatus(f, "Integrating chords through the internal cloud %u/%u", f->fgpu.cursor, f->optimizer.walkcount);
        if (clock() - start >= budget) return;
    }
    f->optimizer.starts[f->optimizer.walkcount] = f->optimizer.entries_used;
    uint32_t P = (f->cloud.tangible.count + 1) * SSS_Q;
    f->optimizer.adam = EZ_MINALLOC(P, sizeof(AdamSlot));
    f->optimizer.gradient = EZ_MINALLOC(P, sizeof(float));
    f->optimizer.epoch = 0;
    f->phase = SSSFIT_OPTIMISE;
    SetStatus(f, "Optimising path length field");
}

static double Evaluate(SSSFitter* f, float* grad, double* r2) {
    uint32_t ng = f->cloud.tangible.count;
    uint32_t P = (ng + 1) * SSS_Q;
    if (grad) memset(grad, 0, (size_t)P * sizeof(float));
    double nll = 0, sres = 0, stot = 0, ymean = 0;
    for (uint32_t i = 0; i < f->optimizer.walkcount; i++) ymean += f->optimizer.excess_length[i];
    ymean /= (f->optimizer.walkcount ? f->optimizer.walkcount : 1);
    const float LOG_SQRT_2PI = 0.91893853f;
    for (uint32_t i = 0; i < f->optimizer.walkcount; i++) {
        float C = f->optimizer.background_weights[i], Q[SSS_Q];
        for (int k = 0; k < SSS_Q; k++) Q[k] = f->optimizer.background_weights[i] * f->cloud.tangible.background[k];
        for (uint32_t e = f->optimizer.starts[i]; e < f->optimizer.starts[i + 1]; e++) {
            const Gauss* g = &f->cloud.tangible.gaussians[f->optimizer.indices[e]];
            float c = f->optimizer.integrals[e];
            C += c;
            for (int k = 0; k < SSS_Q; k++) Q[k] += c * g->q[k];
        }
        float u = f->optimizer.log_length[i];
        float y = f->optimizer.excess_length[i];
        LengthModel lm;
        ModelFromSums(C, Q, u, &lm);
        float z1 = (y - lm.m1) / lm.s1, z2 = (y - lm.m2) / lm.s2;
        // log densities of both components, combined with log-sum-exp
        float l1 = logf(fmaxf(lm.w1, 1e-30f)) - logf(lm.s1) - 0.5f * z1 * z1;
        float l2 = logf(fmaxf(1.0f - lm.w1, 1e-30f)) - logf(lm.s2) - 0.5f * z2 * z2;
        float mx = fmaxf(l1, l2);
        float lse = mx + logf(expf(l1 - mx) + expf(l2 - mx));
        nll += -(lse - LOG_SQRT_2PI);
        float mean = lm.w1 * lm.m1 + (1.0f - lm.w1) * lm.m2;
        sres += (y - mean) * (y - mean);
        stot += (y - ymean) * (y - ymean);
        if (!grad) continue;
        float r1 = expf(l1 - lse), r2 = 1.0f - r1; // responsibilities
        float invC = 1.0f / C;
        float ls1 = Q[2] * invC, ls2 = Q[5] * invC, lg = Q[6] * invC;
        // derivatives of the negative log liklihoood with respect to the blended quantities
        float dm1 = -r1 * z1 / lm.s1, dm2 = -r2 * z2 / lm.s2;
        float dls1 = (ls1 > -6.0f && ls1 < 4.0f) ? r1 * (1.0f - z1 * z1) : 0.0f;
        float dls2 = (ls2 > -6.0f && ls2 < 4.0f) ? r2 * (1.0f - z2 * z2) : 0.0f;
        float dlg = (lg > -20.0f && lg < 20.0f) ? lm.w1 - r1 : 0.0f;
        float dq[SSS_Q] = { dm1, dm1 * u, dls1, dm2, dm2 * u, dls2, dlg };
        for (uint32_t e = f->optimizer.starts[i]; e < f->optimizer.starts[i + 1]; e++) {
            float w = f->optimizer.integrals[e] * invC;
            float* gg = &grad[f->optimizer.indices[e] * SSS_Q];
            for (int k = 0; k < SSS_Q; k++) gg[k] += dq[k] * w;
        }
        float wb = f->optimizer.background_weights[i] * invC;
        float* gb = &grad[ng * SSS_Q];
        for (int k = 0; k < SSS_Q; k++) gb[k] += dq[k] * wb;
    }
    double n = f->optimizer.walkcount ? f->optimizer.walkcount : 1;
    if (grad) for (uint32_t p = 0; p < P; p++) grad[p] /= (float)n;
    if (r2) *r2 = stot > 0 ? 1.0 - sres / stot : 0.0;
    return nll / n;
}

static void OptimisePhase(SSSFitter* f, double budget, ClockFunction clock) {
    double start = clock();
    if (f->optimizer.epoch == 0) f->stats.nll_initial = Evaluate(f, NULL, NULL);
    const float lr = 0.03f;
    const float b1 = 0.9f;
    const float b2 = 0.999f;
    const float reg = 2e-4f;
    while (f->optimizer.epoch < f->cfg.epochs) {
        Evaluate(f, f->optimizer.gradient, NULL);
        f->optimizer.epoch++;
        float bc1 = 1.0f - powf(b1, (float)f->optimizer.epoch), bc2 = 1.0f - powf(b2, (float)f->optimizer.epoch);
        for (uint32_t p = 0; p < (f->cloud.tangible.count + 1) * SSS_Q; p++) {
            uint32_t gi = p / SSS_Q, k = p % SSS_Q;
            float* param = gi < f->cloud.tangible.count ? &f->cloud.tangible.gaussians[gi].q[k] : &f->cloud.tangible.background[k];
            float init = f->optimizer.initial_fit[k];
            float gr = f->optimizer.gradient[p] + reg * (*param - init); // weak pull towards the global fit for sparse gaussians
            AdamSlot* a = &f->optimizer.adam[p];
            a->m = b1 * a->m + (1 - b1) * gr;
            a->v = b2 * a->v + (1 - b2) * gr * gr;
            *param -= lr * (a->m / bc1) / (sqrtf(a->v / bc2) + 1e-8f);
        }
        if (f->optimizer.epoch % 10 == 0) SetStatus(f, "Optimising path length field %u/%u", f->optimizer.epoch, f->cfg.epochs);
        if (clock() - start >= budget) return;
    }
    f->stats.nll_final = Evaluate(f, NULL, &f->stats.r2_mean);
    PackBlob(f);
    f->validater.count = MIN(f->cfg.validation, f->walks.records_count - f->walks.holdout);
    f->fgpu.cursor = 0;
    if (f->validater.count == 0) {
        f->phase = SSSFIT_DONE;
        SetStatus(f, "Done");
        return;
    }
    if (!UploadBuffer(f, SSS_BIND_BLOB, f->result.blob, (size_t)f->result.words * sizeof(uint32_t))) return;
    f->validater.model_exit_distance = EZ_MINALLOC(f->validater.count, sizeof(float));
    f->validater.model_path_length = EZ_MINALLOC(f->validater.count, sizeof(float));
    f->validater.walk_exit_distance = EZ_MINALLOC(f->validater.count, sizeof(float));
    f->validater.walk_path_length = EZ_MINALLOC(f->validater.count, sizeof(float));
    f->fgpu.batch = SSS_BATCH_FIRST;
    f->phase = SSSFIT_VALIDATE;
    SetStatus(f, "Validating against held out walks");
}

static int SSSBlobGaussian(const uint32_t* blob, uint32_t i, vec3 mu, mat3 axes, vec3 scales, float* log_excess) {
    if (i >= blob[SSSG_H_NGAUSS]) return 0;
    const uint32_t* g = blob + blob[SSSG_H_GAUSS] + i * SSSG_GAUSS_STRIDE;
    mat3 W;
    for (int col = 0; col < 3; col++) for (int row = 0; row < 3; row++) W[col][row] = U2F(g[4 + col * 3 + row]);
    for (int k = 0; k < 3; k++) mu[k] = U2F(g[k]);
    // W^T W = R S^-2 R^T: (W^T W)_rc = sum_k W_kr W_kc
    double a[3][3], ev[3], V[3][3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            double acc = 0;
            for (int k = 0; k < 3; k++) acc += (double)W[r][k] * W[c][k];
            a[r][c] = acc;
        }
    Jacobi3(a, ev, V);
    for (int c = 0; c < 3; c++) {
        scales[c] = (float)(1.0 / sqrt(fmax(ev[c], 1e-30)));
        for (int r = 0; r < 3; r++) axes[c][r] = (float)V[r][c];
    }
    vec3 c2;
    glm_vec3_cross(axes[0], axes[1], c2);
    if (glm_vec3_dot(c2, axes[2]) < 0.0f) glm_vec3_negate(axes[2]);
    float q[SSS_Q];
    for (int k = 0; k < SSS_Q; k++) q[k] = U2F(g[13 + k]);
    LengthModel lm;
    ModelFromSums(1.0f, q, logf(2.0f), &lm);
    *log_excess = lm.w1 * lm.m1 + (1.0f - lm.w1) * lm.m2;
    return 1;
}

static uint32_t SSSBlobWords(const uint32_t* b, uint32_t available) {
    if (!b || available < SSSG_HEADER_WORDS) return 0;
    if (b[SSSG_H_MAGIC] != SSSG_MAGIC || b[SSSG_H_VERSION] != SSSG_VERSION) return 0;
    uint64_t ends[5] = {
        (uint64_t)b[SSSG_H_TABLE] + (uint64_t)b[SSSG_H_TCOUNT] * SSSG_TABLE_STRIDE,
        (uint64_t)b[SSSG_H_SURF] + (uint64_t)b[SSSG_H_NSURF] * SSSG_SURF_STRIDE,
        (uint64_t)b[SSSG_H_LOBE] + (uint64_t)b[SSSG_H_NLOBE] * SSSG_LOBE_STRIDE,
        (uint64_t)b[SSSG_H_GAUSS] + (uint64_t)b[SSSG_H_NGAUSS] * SSSG_GAUSS_STRIDE,
        (uint64_t)b[SSSG_H_NODE] + (uint64_t)b[SSSG_H_NNODE] * SSSG_NODE_STRIDE
    };
    uint64_t total = SSSG_HEADER_WORDS;
    for (int i = 0; i < 5; i++) if (ends[i] > total) total = ends[i];
    if (total > available) return 0;
    if (b[SSSG_H_NSURF] == 0 || b[SSSG_H_TCOUNT] == 0) return 0;
    return (uint32_t)total;
}

static int CompareFloat(const void* a, const void* b) {
    float x = *(const float*)a;
    float y = *(const float*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static void Quantiles(float* v, uint32_t n, double* q) {
    qsort(v, n, sizeof(float), CompareFloat);
    const double ps[4] = { 0.25, 0.5, 0.75, 0.9 };
    for (int i = 0; i < 4; i++) q[i] = n ? v[(uint32_t)(ps[i] * (n - 1))] : 0.0;
}

static void PhaseValidate(SSSFitter* f, double budget, ClockFunction clock) {
    double start = clock();
    while (f->fgpu.cursor < f->validater.count) {
        uint32_t n = MIN(f->fgpu.batch, f->validater.count - f->fgpu.cursor);
        const SSSWalkRecord* held = &f->walks.records[f->walks.holdout + f->fgpu.cursor];
        double t0 = clock();
        if (!UploadBuffer(f, SSS_BIND_RECORDS, held, (size_t)n * sizeof(SSSWalkRecord))) return;
        if (!RecreateBuffer(f, SSS_BIND_VALIDATION, (size_t)n * sizeof(SSSValidationSample))) return;
        if (!SubmitKernel(f, SSS_MODE_VALIDATE, f->fgpu.cursor, n)) return;
        SSSValidationSample* vs = EZ_MINALLOC(n, sizeof(SSSValidationSample));
        if (!DownloadBuffer(f, SSS_BIND_VALIDATION, vs, (size_t)n * sizeof(SSSValidationSample))) { EZ_FREE(vs); return; }
        AdaptBatch(f, clock() - t0);
        for (uint32_t k = 0; k < n; k++) {
            uint32_t i = f->fgpu.cursor + k;
            const SSSWalkRecord* w = &held[k];
            const SSSValidationSample* s = &vs[k];
            f->validater.walk_exit_distance[i] = glm_vec3_distance((float*)w->exit, (float*)w->entry) / f->mesh.mfp;
            f->validater.walk_path_length[i] = w->exit[3] / f->mesh.mfp;
            f->validater.model_exit_distance[i] = s->normal[3] / f->mesh.mfp;
            f->validater.model_path_length[i] = s->direction[3] / f->mesh.mfp;
            if (s->exit[3] < 0.5f) f->stats.projection_failures++;
            // throughput at a few albedos: the walk's exact weight (albedo per scattering event; info[0] counts the
            // free flights including the final one that leaves) vs exp(-(1 - a) L) with the walk's and the cloud's lengths
            for (int a = 0; a < 3; a++) {
                double al = SSS_TP_ALBEDOS[a];
                f->stats.throughput.walk[a] += pow(al, (double)w->info[0] - 1.0);
                f->stats.throughput.length[a] += exp(-(1.0 - al) * f->validater.walk_path_length[i]);
                f->stats.throughput.model[a] += exp(-(1.0 - al) * f->validater.model_path_length[i]);
            }
            f->validater.walk_cosine += glm_vec3_dot((float*)w->direction, f->mesh.normals[w->triangles[1]]);
            f->validater.model_cosine += glm_vec3_dot((float*)s->direction, (float*)s->normal);
        }
        EZ_FREE(vs);
        f->fgpu.cursor += n;
        SetStatus(f, "Validating against held out walks %u/%u", f->fgpu.cursor, f->validater.count);
        if (clock() - start >= budget) return;
    }
    uint32_t n = f->validater.count;
    f->stats.validation_samples = n;
    for (int k = 0; k < 3; k++) {
        f->stats.throughput.walk[k] /= n;
        f->stats.throughput.length[k] /= n;
        f->stats.throughput.model[k] /= n;
    }
    f->stats.walks.cosine = f->validater.walk_cosine / n;
    f->stats.model.cosine = f->validater.model_cosine / n;
    Quantiles(f->validater.walk_exit_distance, n, f->stats.walks.distance_quantiles);
    Quantiles(f->validater.model_exit_distance, n, f->stats.model.distance_quantiles);
    Quantiles(f->validater.walk_path_length, n, f->stats.walks.length_quantiles);
    Quantiles(f->validater.model_path_length, n, f->stats.model.length_quantiles);
    f->phase = SSSFIT_DONE;
    SetStatus(f, "Done");
}

static SSSFitPhase SSSFitStep(SSSFitter* f, double budget, ClockFunction clock) {
    if (!f) return SSSFIT_FAILED;
    double start = clock();
    while (f->phase != SSSFIT_DONE && f->phase != SSSFIT_FAILED) {
        double left = budget - (clock() - start);
        if (left <= 0.0) break;
        switch (f->phase) {
            case SSSFIT_SETUP: SetupPhase(f); break;
            case SSSFIT_ORIENT: OrientPhase(f, left, clock); break;
            case SSSFIT_SURFACE: SurfacePhase(f); break;
            case SSSFIT_WALKS: WalksPhase(f, left, clock); break;
            case SSSFIT_LOBES: LobesPhase(f, left, clock); break;
            case SSSFIT_CLOUD: CloudPhase(f, left, clock); break;
            case SSSFIT_CHORDS: ChordPhase(f, left, clock); break;
            case SSSFIT_OPTIMISE: OptimisePhase(f, left, clock); break;
            case SSSFIT_VALIDATE: PhaseValidate(f, left, clock); break;
            default: break;
        }
    }
    return f->phase;
}

static BOOL MeshTriangles(const MeshDescriptor* md, uint32_t* start, uint32_t* count) {
    if (md->tend < md->tstart || md->tend == (TriangleID)-1) return FALSE;
    *start = md->tstart;
    *count = md->tend - md->tstart + 1;
    return TRUE;
}

static float TransformScale(const MeshDescriptor* md) { // cube root of |det| of the linear part: converts object space lengths to world space (same as SSSObject.scale)
    mat4 m;
    mat3 m3;
    memcpy(m, md->transform, sizeof(mat4));
    glm_mat4_pick3(m, m3);
    float s = cbrtf(fabsf(glm_mat3_det(m3)));
    return s > 1e-8f ? s : 1.0f;
}

static uint32_t MaterialDensities(const SurfaceMaterial* m, float* out) {
    float t[3] = { 0 };
    for (int k = 0; k < 3; k++) t[k] = fmaxf((m->scattering[k] + m->absorbtion[k]) * m->scale, 1e-6f);
    float lo = fminf(t[0], fminf(t[1], t[2])), hi = fmaxf(t[0], fmaxf(t[1], t[2]));
    if ((hi - lo) > SSS_SPECTRAL_SPREAD * hi) {
        for (int k = 0; k < 3; k++) out[k] = t[k];
        return 3;
    }
    vec3 total;
    glm_vec3_add((float*)m->scattering, (float*)m->absorbtion, total);
    glm_vec3_fill(out, MAX(Luminance(total) * m->scale, 1e-6f));
    return 1;
}

static MaterialID DominantMaterial(uint32_t start, uint32_t count) {
    Geometry* geo = RendererGeometry();
    MaterialID best = 0;
    size_t bestn = 0;
    size_t nm = geo->materials.size;
    size_t* hist = EZ_ALLOC(nm ? nm : 1, sizeof(size_t));
    BOOL multiple_mats = FALSE;
    size_t first_mat = (size_t)-1;
    for (uint32_t t = start; t < start + count && t < geo->triangles.size; t++) {
        MaterialID m = geo->triangles.data[t].material;
        if (m < nm && ++hist[m] > bestn) {
            bestn = hist[m];
            if (first_mat != (size_t)-1 && first_mat != m) multiple_mats = TRUE;
            if (first_mat == (size_t)-1) first_mat = m;
            best = m;
        }
    }
    EZ_FREE(hist);
    if (multiple_mats) logwarn("More than one unique material found in the mesh - choosing the most dominant material");
    return best;
}

static Cloud* FindCloud(size_t mesh) {
    for (size_t i = 0; i < g_cloud_count; i++) if (g_clouds[i].mesh == mesh) return &g_clouds[i];
    return NULL;
}

static void FreeCloud(Cloud* c) {
    for (int k = 0; k < 3; k++) EZ_FREE(c->blob[k]);
    memset(c, 0, sizeof(*c));
}

static void RemoveCloudAt(size_t i) {
    FreeCloud(&g_clouds[i]);
    memmove(&g_clouds[i], &g_clouds[i + 1], (g_cloud_count - i - 1) * sizeof(Cloud));
    g_cloud_count--;
}

static void RebuildBuffer() {
    Geometry* geo = RendererGeometry();
    for (size_t i = 0; i < g_cloud_count; i++) {
        Cloud* c = &g_clouds[i];
        if (c->mesh < geo->meshes.size) {
            uint32_t s = 0;
            uint32_t n = 0;
            if (MeshTriangles(&geo->meshes.data[c->mesh], &s, &n) && n == c->tcount) c->tstart = s;
        }
    }
    size_t words = SSS_BUFFER_HEADER + SSS_ENTRY_WORDS * g_cloud_count;
    for (size_t i = 0; i < g_cloud_count; i++)
        for (uint32_t k = 0; k < g_clouds[i].channels; k++)
            words += g_clouds[i].words[k];
    ARRLIST_uint32_t_zero(&(geo->gsss), words);
    uint32_t* w = (uint32_t*)geo->gsss.data;
    w[0] = (uint32_t)g_cloud_count;
    uint32_t base = (uint32_t)(SSS_BUFFER_HEADER + SSS_ENTRY_WORDS * g_cloud_count);
    for (size_t i = 0; i < g_cloud_count; i++) {
        Cloud* c = &g_clouds[i];
        uint32_t* e = w + SSS_BUFFER_HEADER + SSS_ENTRY_WORDS * i;
        for (uint32_t k = 0; k < 3; k++) {
            if (k >= c->channels) {
                e[k] = e[0];
                continue;
            }
            e[k] = base;
            memcpy(w + base, c->blob[k], (size_t)c->words[k] * sizeof(uint32_t));
            w[base + SSSG_H_TSTART] = c->tstart;
            base += c->words[k];
        }
        e[3] = c->mesh;
        e[4] = c->enabled ? 1u : 0u;
        e[5] = c->tstart;
        e[6] = c->channels;
    }
    g_update_gsss = TRUE;
    if (NumCameras() > 0) RendererCamera()->config.reset = TRUE;
}

static void StoreCloud(Cloud c) {
    Cloud* existing = FindCloud(c.mesh);
    if (existing) {
        FreeCloud(existing);
        *existing = c;
    } else {
        g_clouds = g_clouds ? EZ_REALLOC(g_clouds, g_cloud_count + 1, sizeof(Cloud)) : EZ_ALLOC(1, sizeof(Cloud));
        g_clouds[g_cloud_count++] = c;
    }
    RebuildBuffer();
}

static void LogStats(const SSSFitStats* s) {
    logtrace("Subsurface splats: %u reference walks (%u lost: %u through cracks, %u from outside, %u at the step limit), %.1f scattering events per walk on average", s->walks.kept, s->walks.lost, s->walks.lostwhy[CRACK_ESCAPE], s->walks.lostwhy[SURFACE_REACH], s->walks.lostwhy[STEP_LIMIT], s->mean_steps);
    logtrace("    path length field: log likelihood %.4f -> %.4f, explains %.3f percent of the log excess length variance", -s->nll_initial, -s->nll_final, 100.0 * s->r2_mean);
    if (s->validation_samples == 0) return;
    logtrace("    validation on %u held out walks (walk / model):", s->validation_samples);
    logtrace("        exit distance median %.2f / %.2f mfp, 90%% %.2f / %.2f mfp", s->walks.distance_quantiles[1], s->model.distance_quantiles[1], s->walks.distance_quantiles[3], s->model.distance_quantiles[3]);
    logtrace("        path length median %.2f / %.2f mfp, 90%% %.2f / %.2f mfp", s->walks.length_quantiles[1], s->model.length_quantiles[1], s->walks.length_quantiles[3], s->model.length_quantiles[3]);
    logtrace("        throughput walk / length only / cloud: a=0.5 %.4f / %.4f / %.4f, a=0.9 %.4f / %.4f / %.4f, a=0.99 %.4f / %.4f / %.4f", s->throughput.walk[0], s->throughput.length[0], s->throughput.model[0], s->throughput.walk[1], s->throughput.length[1], s->throughput.model[1], s->throughput.walk[2], s->throughput.length[2], s->throughput.model[2]);
    logtrace("        brightness vs walk (length only / cloud): a=0.5 %.3fx / %.3fx, a=0.9 %.3fx / %.3fx, a=0.99 %.3fx / %.3fx", s->throughput.length[0] / fmax(s->throughput.walk[0], 1e-12), s->throughput.model[0] / fmax(s->throughput.walk[0], 1e-12), s->throughput.length[1] / fmax(s->throughput.walk[1], 1e-12), s->throughput.model[1] / fmax(s->throughput.walk[1], 1e-12), s->throughput.length[2] / fmax(s->throughput.walk[2], 1e-12), s->throughput.model[2] / fmax(s->throughput.walk[2], 1e-12));
    logtrace("        mean exit cosine %.3f / %.3f, projection failures %u (%.1f%%)", s->walks.cosine, s->model.cosine, s->projection_failures, 100.0 * s->projection_failures / (double)s->validation_samples);
}

static void ReleaseJobData(CloudTrainJob* j) {
    if (j->fitter) SSSFitDestroy(j->fitter); // first: it borrows the mesh and the GPU context
    j->fitter = NULL;
    if (j->gpu_alive) {
        tgpu_kernel_destroy(&j->gpu, &j->kernel);
        tgpu_destroy(&j->gpu);
        j->gpu_alive = FALSE;
    }
    EZ_FREE(j->positions);
    j->positions = NULL;
    EZ_FREE(j->indices);
    j->indices = NULL;
    for (int k = 0; k < 3; k++) {
        EZ_FREE(j->blob[k]);
        j->blob[k] = NULL;
        j->words[k] = 0;
    }
}

static void FinishChannel(CloudTrainJob* j) {
    static const char* names[3] = { "red", "green", "blue" };
    uint32_t words = 0;
    const uint32_t* blob = SSSFitBlob(j->fitter, &words);
    j->stats = *SSSFitGetStats(j->fitter);
    j->have_stats = TRUE;
    if (j->channels == 3) logtrace("Subsurface splats, %s channel:", names[j->channel]);
    LogStats(&j->stats);
    j->blob[j->channel] = EZ_ALLOC(words ? words : 1, sizeof(uint32_t));
    if (blob) memcpy(j->blob[j->channel], blob, (size_t)words * sizeof(uint32_t));
    j->words[j->channel] = words;
    SSSFitDestroy(j->fitter);
    j->fitter = NULL;
    if (!blob) {
        snprintf(j->status, sizeof(j->status), "Failed: the fit produced no cloud");
        ReleaseJobData(j);
        j->phase = SUBSURFACE_FAILED;
        return;
    }
    if (++j->channel < j->channels) {
        SSSFitInput in = { j->positions, j->indices, j->vertex_count, j->tcount, j->sigma[j->channel] * j->scale, j->anisotropy, &j->gpu, &j->kernel };
        j->fitter = SSSFitCreate(&in, &j->config);
        if (!j->fitter) {
            snprintf(j->status, sizeof(j->status), "Failed: could not set up the %s channel fit", names[j->channel]);
            ReleaseJobData(j);
            j->phase = SUBSURFACE_FAILED;
        }
        return;
    }
    Geometry* geo = RendererGeometry();
    uint32_t s = 0;
    uint32_t n = 0;
    if (j->mesh >= geo->meshes.size || !MeshTriangles(&geo->meshes.data[j->mesh], &s, &n) || n != j->tcount) {
        snprintf(j->status, sizeof(j->status), "Failed: the object changed while it was being trained");
        ReleaseJobData(j);
        j->phase = SUBSURFACE_FAILED;
        return;
    }
    Cloud c = { 0 };
    c.mesh = (uint32_t)j->mesh;
    c.enabled = TRUE;
    c.channels = j->channels;
    c.tstart = s;
    c.tcount = n;
    c.material = j->material;
    c.anisotropy = j->anisotropy;
    c.scale = j->scale;
    uint32_t total = 0;
    for (uint32_t k = 0; k < j->channels; k++) {
        c.blob[k] = j->blob[k];
        c.words[k] = j->words[k];
        c.sigma[k] = j->sigma[k];
        j->blob[k] = NULL;
        c.blob[k][SSSG_H_TSTART] = s;
        c.blob[k][SSSG_H_MATERIAL] = j->material;
        memcpy(&c.blob[k][SSSG_H_SCALE], &j->scale, 4);
        total += c.words[k];
    }
    ReleaseJobData(j);
    StoreCloud(c);
    snprintf(j->status, sizeof(j->status), "Done in %.0fs: %.1f KB cloud%s linked to \"%s\"", GetTime() - j->started, total * 4.0 / 1024.0, c.channels == 3 ? " (per channel)" : "", *MeshNameReference(j->mesh));
    loginfo("%s", j->status);
    j->phase = SUBSURFACE_DONE;
}

GSSSConfig SubsurfaceDefaultConfig() {
    GSSSConfig c = { 0 };
    c.surface_gaussians = 384;
    c.internal_gaussians = 1024;
    c.walks = 300000;
    c.lobes = 6;
    c.max_steps = 20000;
    c.epochs = 250;
    c.validation = 20000;
    c.seed = 0x5EED5u;
    c.gpu_index = -1;
    return c;
}

size_t SubsurfaceCloudCount() {
    return g_cloud_count;
}

BOOL SubsurfaceTrainStart(size_t mesh, const GSSSConfig* config) {
    if (g_job.phase == SUBSURFACE_TRAINING) {
        logwarn("A subsurface cloud is already being trained");
        return FALSE;
    }
    ReleaseJobData(&g_job);
    g_job = (CloudTrainJob){ 0 };
    Geometry* geo = RendererGeometry();
    g_job.mesh = mesh;
    if (mesh >= geo->meshes.size) {
        snprintf(g_job.status, sizeof(g_job.status), "Failed: no such object");
        logerror(g_job.status);
        g_job.phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    MeshDescriptor* md = &geo->meshes.data[mesh];
    if (!MeshTriangles(md, &g_job.tstart, &g_job.tcount) || g_job.tstart + g_job.tcount > geo->triangles.size) {
        snprintf(g_job.status, sizeof(g_job.status), "Failed: the object has no triangles");
        logerror(g_job.status);
        g_job.phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    g_job.material = DominantMaterial(g_job.tstart, g_job.tcount);
    const SurfaceMaterial* mat = &geo->materials.data[g_job.material];
    if (mat->model != SUBSURFACE_MODEL) {
        snprintf(g_job.status, sizeof(g_job.status), "Failed: material \"%s\" does not use the subsurface model", MaterialName(g_job.material));
        logerror(g_job.status);
        g_job.phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    g_job.channels = MaterialDensities(mat, g_job.sigma);
    g_job.anisotropy = mat->anisotropy;
    g_job.scale = TransformScale(md);
    size_t nv = geo->vertices.size;
    uint32_t* remap = EZ_ALLOC(nv ? nv : 1, sizeof(uint32_t));
    for (size_t i = 0; i < nv; i++) remap[i] = (uint32_t)-1;
    uint32_t* idx = EZ_ALLOC((size_t)g_job.tcount * 3, sizeof(uint32_t));
    vec3* pos = EZ_ALLOC((size_t)g_job.tcount * 3, sizeof(vec3));
    uint32_t used = 0;
    for (uint32_t t = 0; t < g_job.tcount; t++) {
        const Triangle* tri = &geo->triangles.data[g_job.tstart + t];
        VertexID ids[3] = { tri->a, tri->b, tri->c };
        for (int k = 0; k < 3; k++) {
            VertexID v = ids[k] < nv ? ids[k] : 0;
            if (remap[v] == (uint32_t)-1) {
                remap[v] = used;
                glm_vec3_copy(geo->vertices.data[v], pos[used]);
                used++;
            }
            idx[t * 3 + k] = remap[v];
        }
    }
    EZ_FREE(remap);
    g_job.positions = pos;
    g_job.indices = idx;
    g_job.vertex_count = used;
    g_job.config = config ? *config : SubsurfaceDefaultConfig();
    g_job.channel = 0;
    if (tgpu_create(&g_job.gpu, g_job.config.gpu_index, 0) != 0) {
        snprintf(g_job.status, sizeof(g_job.status), "Failed: no Vulkan compute device for training");
        logerror(g_job.status);
        ReleaseJobData(&g_job);
        g_job.phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    g_job.gpu_alive = TRUE;
    if (tgpu_kernel_create(&g_job.gpu, &g_job.kernel, SSS_KERNEL_PATH) != 0) {
        snprintf(g_job.status, sizeof(g_job.status), "Failed: could not load %s", SSS_KERNEL_PATH);
        logerror(g_job.status);
        ReleaseJobData(&g_job);
        g_job.phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    SSSFitInput in = { g_job.positions, g_job.indices, g_job.vertex_count, g_job.tcount, g_job.sigma[0] * g_job.scale, g_job.anisotropy, &g_job.gpu, &g_job.kernel };
    g_job.fitter = SSSFitCreate(&in, &g_job.config);
    if (!g_job.fitter) {
        snprintf(g_job.status, sizeof(g_job.status), "Failed: could not set up the fit");
        logerror(g_job.status);
        ReleaseJobData(&g_job);
        g_job.phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    g_job.started = GetTime();
    g_job.phase = SUBSURFACE_TRAINING;
    snprintf(g_job.status, sizeof(g_job.status), "Starting");
    if (g_job.channels == 3) loginfo("Training subsurface splats for \"%s\": %u triangles, coloured scattering -> one cloud per channel, sigma_t %.3f / %.3f / %.3f, g %.2f", *MeshNameReference(mesh), g_job.tcount, g_job.sigma[0], g_job.sigma[1], g_job.sigma[2], g_job.anisotropy);
    else loginfo("Training subsurface splats for \"%s\": %u triangles, sigma %.3f (mean free path %.4g), g %.2f", *MeshNameReference(mesh), g_job.tcount, g_job.sigma[0], 1.0 / g_job.sigma[0], g_job.anisotropy);
    loginfo("Subsurface splat training runs its walks on %s", g_job.gpu.gpu_name);
    return TRUE;
}

BOOL SubsurfaceTrainTick(double seconds) {
    if (g_job.phase != SUBSURFACE_TRAINING || !g_job.fitter) return FALSE;
    SSSFitPhase p = SSSFitStep(g_job.fitter, seconds, GetTime);
    if (g_job.channels == 3) {
        static const char* names[3] = { "red", "green", "blue" };
        snprintf(g_job.status, sizeof(g_job.status), "[%s %u/3] %s", names[g_job.channel], g_job.channel + 1, SSSFitStatus(g_job.fitter));
    } else {
        snprintf(g_job.status, sizeof(g_job.status), "%s", SSSFitStatus(g_job.fitter));
    }
    if (p == SSSFIT_FAILED) {
        logerror("Subsurface splat training failed: %s", SSSFitStatus(g_job.fitter));
        ReleaseJobData(&g_job);
        g_job.phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    if (p == SSSFIT_DONE) {
        FinishChannel(&g_job);
        return g_job.phase == SUBSURFACE_TRAINING;
    }
    return TRUE;
}

void SubsurfaceTrainCancel() {
    if (g_job.phase != SUBSURFACE_TRAINING) return;
    ReleaseJobData(&g_job);
    g_job.phase = SUBSURFACE_IDLE;
    snprintf(g_job.status, sizeof(g_job.status), "Cancelled");
    loginfo("Subsurface splat training cancelled");
}

SubsurfaceTrainPhase SubsurfaceTrainPhaseNow() {
    return g_job.phase;
}

size_t SubsurfaceTrainMesh() {
    return g_job.phase == SUBSURFACE_TRAINING ? g_job.mesh : (size_t)-1;
}

float SubsurfaceTrainProgress() {
    if (g_job.phase == SUBSURFACE_DONE) return 1.0f;
    if (!g_job.fitter || g_job.channels == 0) return 0.0f;
    return ((float)g_job.channel + SSSFitProgress(g_job.fitter)) / (float)g_job.channels;
}

const char* SubsurfaceTrainStatus() {
    return g_job.status;
}

BOOL QuerySubsurfaceCloudInfo(size_t mesh, SubsurfaceCloudInfo* info) {
    memset(info, 0, sizeof(*info));
    Cloud* c = FindCloud(mesh);
    if (!c) return FALSE;
    const uint32_t* b = c->blob[0];
    info->linked = TRUE;
    info->enabled = c->enabled;
    info->channels = c->channels;
    info->triangles = c->tcount;
    info->surface_gaussians = b[SSSG_H_NSURF];
    info->lobes = b[SSSG_H_NLOBE];
    info->internal_gaussians = b[SSSG_H_NGAUSS];
    info->bytes = 0;
    for (uint32_t k = 0; k < c->channels; k++) info->bytes += c->words[k] * 4;
    info->sigma = c->sigma[0];
    info->anisotropy = c->anisotropy;
    info->scale = c->scale;
    info->material = c->material;
    Geometry* geo = RendererGeometry();
    uint32_t s = 0;
    uint32_t n = 0;
    if (mesh >= geo->meshes.size || !MeshTriangles(&geo->meshes.data[mesh], &s, &n) || n != c->tcount) {
        info->stale = TRUE;
        snprintf(info->reason, sizeof(info->reason), "triangles changed, the random walk is used instead");
        return TRUE;
    }
    MaterialID now = geo->triangles.data[s].material;
    if (now >= geo->materials.size) return TRUE;
    const SurfaceMaterial* m = &geo->materials.data[now];
    float sigma[3] = { 0 };
    uint32_t channels = MaterialDensities(m, sigma);
    float scale = TransformScale(&geo->meshes.data[mesh]);
    BOOL density_changed = FALSE;
    for (uint32_t k = 0; k < channels && channels == c->channels; k++)
        if (fabsf(sigma[k] - c->sigma[k]) > 0.01f * c->sigma[k]) density_changed = TRUE;
    if (m->model != SUBSURFACE_MODEL) {
        info->stale = TRUE;
        snprintf(info->reason, sizeof(info->reason), "material is no longer a subsurface material");
    } else if (channels != c->channels) {
        info->stale = TRUE;
        snprintf(info->reason, sizeof(info->reason), channels == 3 ? "scattering became coloured, retrain for per channel clouds" : "scattering became grey, retrain");
    } else if (density_changed) {
        info->stale = TRUE;
        snprintf(info->reason, sizeof(info->reason), "scattering density changed (%.3g, trained %.3g), retrain", sigma[0], c->sigma[0]);
    } else if (fabsf(m->anisotropy - c->anisotropy) > 0.01f) {
        info->stale = TRUE;
        snprintf(info->reason, sizeof(info->reason), "anisotropy changed (%.2f, trained %.2f), retrain", m->anisotropy, c->anisotropy);
    } else if (fabsf(scale - c->scale) > 0.01f * c->scale) {
        info->stale = TRUE;
        snprintf(info->reason, sizeof(info->reason), "object scale changed (%.3g, trained %.3g), retrain", scale, c->scale);
    }
    return TRUE;
}

void SubsurfaceSetCloudEnabled(size_t mesh, BOOL enabled) {
    Cloud* c = FindCloud(mesh);
    if (!c || c->enabled == enabled) return;
    c->enabled = enabled;
    RebuildBuffer();
}

BOOL SubsurfaceRemoveCloud(size_t mesh) {
    for (size_t i = 0; i < g_cloud_count; i++) {
        if (g_clouds[i].mesh != mesh) continue;
        RemoveCloudAt(i);
        RebuildBuffer();
        return TRUE;
    }
    return FALSE;
}

void SubsurfaceClear() {
    SubsurfaceTrainCancel();
    for (size_t i = 0; i < g_cloud_count; i++) FreeCloud(&g_clouds[i]);
    EZ_FREE(g_clouds);
    g_clouds = NULL;
    g_cloud_count = 0;
    ARRLIST_uint32_t_clear(&(RendererGeometry()->gsss));
    g_update_gsss = TRUE;
}

void SubsurfaceMeshDeleted(size_t mesh) {
    if (g_job.phase == SUBSURFACE_TRAINING) {
        if (g_job.mesh == mesh) SubsurfaceTrainCancel();
        else if (g_job.mesh > mesh) g_job.mesh--;
    }
    BOOL changed = FALSE;
    for (size_t i = 0; i < g_cloud_count;) {
        if (g_clouds[i].mesh == mesh) { RemoveCloudAt(i); changed = TRUE; continue; }
        if (g_clouds[i].mesh > mesh) { g_clouds[i].mesh--; changed = TRUE; }
        i++;
    }
    if (changed || g_cloud_count > 0) RebuildBuffer();
}

BOOL SubsurfacePreviewCloud(size_t mesh) {
    Cloud* c = FindCloud(mesh);
    Geometry* geo = RendererGeometry();
    if (!c || mesh >= geo->meshes.size) return FALSE;
    mat4 M = { 0 };
    memcpy(M, geo->meshes.data[mesh].transform, sizeof(mat4));
    const uint32_t* pblob = c->blob[c->channels == 3 ? 1 : 0];
    uint32_t n = pblob[SSSG_H_NGAUSS];
    float* le = EZ_ALLOC(n ? n : 1, sizeof(float));
    float lo = FLT_MAX;
    float hi = -FLT_MAX;
    for (uint32_t i = 0; i < n; i++) {
        vec3 mu, sc;
        mat3 ax;
        SSSBlobGaussian(pblob, i, mu, ax, sc, &le[i]);
        if (le[i] < lo) lo = le[i];
        if (le[i] > hi) hi = le[i];
    }
    size_t first = NumSplats();
    for (uint32_t i = 0; i < n; i++) {
        vec3 mu, sc;
        mat3 ax;
        float unused = 0;
        SSSBlobGaussian(pblob, i, mu, ax, sc, &unused);
        GaussianSplat s = { 0 };
        glm_mat4_mulv3(M, mu, 1.0f, s.position);
        vec3 cols[3] = { 0 };
        for (int k = 0; k < 3; k++) {
            vec3 a;
            glm_vec3_scale(ax[k], sc[k], a);
            glm_mat4_mulv3(M, a, 0.0f, cols[k]);
            float len = glm_vec3_norm(cols[k]);
            s.logscale[k] = logf(fmaxf(len, 1e-6f));
            glm_vec3_normalize(cols[k]);
        }
        vec3 c2 = { 0 };
        glm_vec3_cross(cols[0], cols[1], c2);
        glm_vec3_normalize(c2);
        if (glm_vec3_dot(c2, cols[2]) < 0.0f) glm_vec3_negate(c2);
        glm_vec3_cross(c2, cols[0], cols[1]);
        QuaternionFromBasis(cols[0], cols[1], c2, s.rotation);
        s.opacity = logf(0.5f / 0.5f);
        float t = hi > lo ? (le[i] - lo) / (hi - lo) : 0.5f;
        float rgb[3] = { t, 0.25f + 0.5f * (1.0f - fabsf(2.0f * t - 1.0f)), 1.0f - t };
        for (int k = 0; k < 3; k++) s.shdc[k] = (0.15f * rgb[k] - 0.5f) / 0.28209479177387814f;
        SubmitSplat(s);
    }
    EZ_FREE(le);
    char name[MAX_MESH_NAME_SIZE];
    snprintf(name, sizeof(name), "%s (subsurface cloud)", *MeshNameReference(mesh));
    return SubmitSplatMeshDescriptor(first, n, name);
}

uint32_t* SubsurfaceSerialize(size_t* words) {
    size_t total = 1;
    for (size_t i = 0; i < g_cloud_count; i++) {
        total += SSS_RECORD_WORDS;
        for (uint32_t k = 0; k < g_clouds[i].channels; k++)
            total += 2 + g_clouds[i].words[k];
    }
    uint32_t* out = EZ_ALLOC(total, sizeof(uint32_t));
    out[0] = (uint32_t)g_cloud_count;
    size_t w = 1;
    for (size_t i = 0; i < g_cloud_count; i++) {
        const Cloud* c = &g_clouds[i];
        out[w++] = c->mesh;
        out[w++] = c->enabled ? 1u : 0u;
        out[w++] = c->tstart;
        out[w++] = c->tcount;
        out[w++] = c->material;
        memcpy(&out[w++], &c->anisotropy, 4);
        memcpy(&out[w++], &c->scale, 4);
        out[w++] = c->channels;
        for (uint32_t k = 0; k < c->channels; k++) {
            memcpy(&out[w++], &c->sigma[k], 4);
            out[w++] = c->words[k];
            memcpy(&out[w], c->blob[k], (size_t)c->words[k] * sizeof(uint32_t));
            w += c->words[k];
        }
    }
    *words = total;
    return out;
}

BOOL SubsurfaceDeserialize(const uint32_t* data, size_t words) {
    SubsurfaceClear();
    if (!data || words == 0) return TRUE;
    Geometry* geo = RendererGeometry();
    size_t count = data[0], w = 1, loaded = 0;
    BOOL ok = TRUE;
    for (size_t i = 0; i < count && ok; i++) {
        if (w + SSS_RECORD_WORDS > words) { ok = FALSE; break; }
        Cloud c = { 0 };
        c.mesh = data[w];
        c.enabled = data[w + 1] != 0;
        c.tstart = data[w + 2];
        c.tcount = data[w + 3];
        c.material = data[w + 4];
        memcpy(&c.anisotropy, &data[w + 5], 4);
        memcpy(&c.scale, &data[w + 6], 4);
        c.channels = data[w + 7];
        w += SSS_RECORD_WORDS;
        if (c.channels != 1 && c.channels != 3) { ok = FALSE; break; }
        BOOL valid = TRUE;
        for (uint32_t k = 0; k < c.channels; k++) {
            if (w + 2 > words) { ok = FALSE; break; }
            memcpy(&c.sigma[k], &data[w], 4);
            c.words[k] = data[w + 1];
            w += 2;
            if (c.words[k] > words - w) { ok = FALSE; break; }
            const uint32_t* blob = &data[w];
            w += c.words[k];
            if (SSSBlobWords(blob, c.words[k]) == 0 || blob[SSSG_H_TCOUNT] != c.tcount) { valid = FALSE; continue; }
            c.blob[k] = EZ_ALLOC(c.words[k], sizeof(uint32_t));
            memcpy(c.blob[k], blob, (size_t)c.words[k] * sizeof(uint32_t));
        }
        uint32_t s, n;
        if (!ok || !valid || c.mesh >= geo->meshes.size || !MeshTriangles(&geo->meshes.data[c.mesh], &s, &n) ||
            n != c.tcount || FindCloud(c.mesh)) {
            if (ok) logwarn("Skipping a saved subsurface cloud that no longer matches its object");
            FreeCloud(&c);
            continue;
        }
        g_clouds = g_clouds ? EZ_REALLOC(g_clouds, g_cloud_count + 1, sizeof(Cloud)) : EZ_ALLOC(1, sizeof(Cloud));
        g_clouds[g_cloud_count++] = c;
        loaded++;
    }
    if (!ok) logwarn("The session's subsurface cloud data is truncated, some clouds were not restored");
    if (loaded > 0) {
        RebuildBuffer();
        loginfo("Restored %zu subsurface cloud%s", loaded, loaded == 1 ? "" : "s");
    }
    return ok;
}

BOOL SubsurfaceNeedsUpdate() {
    return g_update_gsss;
}

void ResolveSubsurfaceUpdate() {
    g_update_gsss = FALSE;
}

// ---- packed cloud format (uint32 words, floats stored as raw bits) ----------------------------------------------------
// One object is a self contained blob; every offset inside it is relative to the start of the blob. The shaders read it
// through SubsurfaceSuite (shaders/shared.import), which documents the same layout.
//
// surface gaussian words: 0-2 centre, 3 tangential sigma, 4-6 outward normal, 7 normal sigma,
//                         8-10 exit direction axis, 11 exit direction exponent, 12 first lobe, 13 lobe count,
//                         14 training samples (entries), 15 training samples (exits)
// lobe words:             0-2 mean offset from the entry point, 3 cumulative weight within its surface gaussian,
//                         4-12 lower cholesky factor of the covariance (column major), 13-15 projection axis
// internal gaussian words: 0-2 mean, 3 unused, 4-12 whitening matrix W = S^-1 R^T (column major),
//                         13-19 path length field coefficients (see sss_length_model in shared.import)
