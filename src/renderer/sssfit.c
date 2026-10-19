#include "sssfit.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <stdlib.h>
#include <stdarg.h>

static inline size_t sss_atleast1(size_t n) { return n ? n : 1; }

#ifdef SSSFIT_STANDALONE
#define SSS_ALLOC(n, s) calloc(sss_atleast1((size_t)(n)), (s))
#define SSS_REALLOC(p, n, s) realloc((p), (size_t)(n) * (s))
#define SSS_FREE(p) free((void*)(p))
#else
#include <easymemory.h>
#define SSS_ALLOC(n, s) EZ_ALLOC(sss_atleast1((size_t)(n)), (s))
#define SSS_REALLOC(p, n, s) EZ_REALLOC((p), (n), (s))
#define SSS_FREE(p) EZ_FREE(p)
#endif

#define SSS_PI 3.14159265358979323846
#define SSS_SQRT_2PI 2.50662827463f
#define SSS_INTERIOR_CAP 200000u
#define SSS_EM_MAX_POINTS 2048u
#define SSS_SURFACE_SAMPLES_PER 24u
#define SSS_BG_WEIGHT 0.05f      // line density of the background term, relative to the peak of a gaussian
#define SSS_EPS_FRACTION 0.1f     // excess length floor, in mean free paths
#define SSS_DMIN_FRACTION 0.01f   // chords shorter than this (in mean free paths) are evaluated as a point
#define SSS_GAUSS_CUTOFF 9.0f
#define SSS_PROJECTION_TRIES 3
#define SSS_Q 7
#define SSS_MAX_EXCESS_MFP 2000.0f // cap on the sampled extra path length (log normal tails are unbounded)     // squared mahalanobis support of an internal gaussian (3 sigma)

// ================================================================================================================
// small helpers
// ================================================================================================================

static inline float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static inline void sub3(const float* a, const float* b, float* o) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static inline void cross3(const float* a, const float* b, float* o) {
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    o[0] = x; o[1] = y; o[2] = z;
}
static inline float len3(const float* a) { return sqrtf(dot3(a, a)); }
static inline void norm3(float* a) {
    float l = len3(a);
    if (l > 1e-30f) { a[0] /= l; a[1] /= l; a[2] /= l; }
}
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

typedef struct { uint64_t s; } Rng;

static inline uint32_t rng_u32(Rng* r) {
    // pcg32
    uint64_t old = r->s;
    r->s = old * 6364136223846793005ull + 1442695040888963407ull;
    uint32_t xs = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xs >> rot) | (xs << ((32u - rot) & 31u));
}
static inline float rng_f(Rng* r) { return (float)(rng_u32(r) >> 8) * (1.0f / 16777216.0f); }
static float rng_cb(void* st) { return rng_f((Rng*)st); }

// orthonormal basis, same construction as onb() in path.comp
static void onb3(const float* w, const float* local, float* out) {
    float a[3] = { 1, 0, 0 };
    if (fabsf(w[0]) > 0.9f) { a[0] = 0; a[1] = 1; }
    float v[3], u[3];
    cross3(w, a, v); norm3(v);
    cross3(w, v, u); norm3(u);
    for (int k = 0; k < 3; k++) out[k] = local[0] * u[k] + local[1] * v[k] + local[2] * w[k];
    norm3(out);
}

static void sample_hg(const float* axis, float g, Rng* r, float* out) {
    float xi1 = rng_f(r), xi2 = rng_f(r);
    float ct;
    if (fabsf(g) < 0.001f) {
        ct = 1.0f - 2.0f * xi1;
    } else {
        float sq = (1.0f - g * g) / (1.0f - g + 2.0f * g * xi1);
        ct = clampf((1.0f + g * g - sq * sq) / (2.0f * g), -1.0f, 1.0f);
    }
    float st = sqrtf(fmaxf(0.0f, 1.0f - ct * ct));
    float phi = 2.0f * (float)SSS_PI * xi2;
    float local[3] = { st * cosf(phi), st * sinf(phi), ct };
    onb3(axis, local, out);
}

static void sample_cosine(const float* axis, Rng* r, float* out) {
    float r1 = rng_f(r), r2 = rng_f(r);
    float s = sqrtf(r2), phi = 2.0f * (float)SSS_PI * r1;
    float local[3] = { cosf(phi) * s, sinf(phi) * s, sqrtf(fmaxf(0.0f, 1.0f - r2)) };
    onb3(axis, local, out);
}

// Abramowitz & Stegun 7.1.26 and M. Giles' erfinv, identical to the versions in path.comp
static float erf_approx(float x) {
    float s = x < 0.0f ? -1.0f : (x > 0.0f ? 1.0f : 0.0f);
    x = fabsf(x);
    float t = 1.0f / (1.0f + 0.3275911f * x);
    float y = 1.0f - (((((1.061405429f * t - 1.453152027f) * t) + 1.421413741f) * t - 0.284496736f) * t + 0.254829592f) * t * expf(-x * x);
    return s * y;
}
static float normal_cdf(float x) { return 0.5f * (1.0f + erf_approx(x * 0.70710678f)); }
static float erfinv_approx(float x) {
    float w = -logf((1.0f - x) * (1.0f + x));
    float p;
    if (w < 5.0f) {
        w = w - 2.5f;
        p = 2.81022636e-08f;
        p = 3.43273939e-07f + p * w;
        p = -3.5233877e-06f + p * w;
        p = -4.39150654e-06f + p * w;
        p = 0.00021858087f + p * w;
        p = -0.00125372503f + p * w;
        p = -0.00417768164f + p * w;
        p = 0.246640727f + p * w;
        p = 1.50140941f + p * w;
    } else {
        w = sqrtf(w) - 3.0f;
        p = -0.000200214257f;
        p = 0.000100950558f + p * w;
        p = 0.00134934322f + p * w;
        p = -0.00367342844f + p * w;
        p = 0.00573950773f + p * w;
        p = -0.0076224613f + p * w;
        p = 0.00943887047f + p * w;
        p = 1.00167406f + p * w;
        p = 2.83297682f + p * w;
    }
    return p * x;
}
static float normal_quantile(float p) {
    p = clampf(p, 1e-7f, 1.0f - 1e-7f);
    return 1.41421356f * erfinv_approx(2.0f * p - 1.0f);
}

// symmetric 3x3 eigen decomposition (cyclic jacobi). a is row major and destroyed; columns of v are eigenvectors.
static void jacobi3(double a[3][3], double evals[3], double v[3][3]) {
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) v[i][j] = i == j ? 1.0 : 0.0;
    for (int sweep = 0; sweep < 32; sweep++) {
        double off = fabs(a[0][1]) + fabs(a[0][2]) + fabs(a[1][2]);
        if (off < 1e-30) break;
        for (int p = 0; p < 2; p++) {
            for (int q = p + 1; q < 3; q++) {
                if (fabs(a[p][q]) < 1e-300) continue;
                double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                double t = (theta >= 0 ? 1.0 : -1.0) / (fabs(theta) + sqrt(theta * theta + 1.0));
                double c = 1.0 / sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < 3; k++) {
                    double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; k++) {
                    double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; k++) {
                    double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    for (int i = 0; i < 3; i++) evals[i] = a[i][i];
}

// lower cholesky of a symmetric positive definite 3x3 (row major in, column major lower factor out)
static void cholesky3(const double c[3][3], float* Lcm) {
    double l00 = sqrt(fmax(c[0][0], 1e-30));
    double l10 = c[1][0] / l00;
    double l20 = c[2][0] / l00;
    double l11 = sqrt(fmax(c[1][1] - l10 * l10, 1e-30));
    double l21 = (c[2][1] - l20 * l10) / l11;
    double l22 = sqrt(fmax(c[2][2] - l20 * l20 - l21 * l21, 1e-30));
    // column major: L[col*3 + row]
    float L[9] = { (float)l00, (float)l10, (float)l20, 0, (float)l11, (float)l21, 0, 0, (float)l22 };
    memcpy(Lcm, L, sizeof(L));
}

// ================================================================================================================
// triangle BVH (object space)
// ================================================================================================================

typedef struct {
    float bmin[3], bmax[3];
    uint32_t left;  // first triangle for leaves
    uint32_t count; // 0 for interior nodes, right child = left + 1 is not assumed: right stored in `right`
    uint32_t right;
} TriNode;

typedef struct {
    TriNode* nodes;
    uint32_t nnodes;
    uint32_t* order; // triangle indices in leaf order
    const float* pos;
    const uint32_t* idx;
} TriBVH;

static float* g_sort_keys = NULL; // centroid keys for the quickselect below (build is single threaded)

static void select_nth(uint32_t* a, int64_t lo, int64_t hi, int64_t nth) {
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

typedef struct { uint32_t node, start, end; } BuildTask;

// Generic median split builder over primitives with boxes (bmins/bmaxs, 3 floats each). Returns nodes.
static TriNode* build_bvh(uint32_t n, const float* bmins, const float* bmaxs, uint32_t leafsize, uint32_t* order, uint32_t* out_nodes) {
    uint32_t cap = n * 2 + 1;
    TriNode* nodes = SSS_ALLOC(cap, sizeof(TriNode));
    float* keys = SSS_ALLOC(n, sizeof(float));
    for (uint32_t i = 0; i < n; i++) order[i] = i;
    BuildTask* stack = SSS_ALLOC(128 + n, sizeof(BuildTask));
    uint32_t sp = 0, used = 1;
    stack[sp++] = (BuildTask){ 0, 0, n };
    while (sp) {
        BuildTask t = stack[--sp];
        TriNode* nd = &nodes[t.node];
        float cmin[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, cmax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (int k = 0; k < 3; k++) { nd->bmin[k] = FLT_MAX; nd->bmax[k] = -FLT_MAX; }
        for (uint32_t i = t.start; i < t.end; i++) {
            uint32_t p = order[i];
            for (int k = 0; k < 3; k++) {
                float lo = bmins[p * 3 + k], hi = bmaxs[p * 3 + k], c = 0.5f * (lo + hi);
                if (lo < nd->bmin[k]) nd->bmin[k] = lo;
                if (hi > nd->bmax[k]) nd->bmax[k] = hi;
                if (c < cmin[k]) cmin[k] = c;
                if (c > cmax[k]) cmax[k] = c;
            }
        }
        uint32_t count = t.end - t.start;
        if (count <= leafsize) {
            nd->left = t.start;
            nd->count = count;
            continue;
        }
        int axis = 0;
        float ext = cmax[0] - cmin[0];
        for (int k = 1; k < 3; k++) if (cmax[k] - cmin[k] > ext) { ext = cmax[k] - cmin[k]; axis = k; }
        for (uint32_t i = t.start; i < t.end; i++) {
            uint32_t p = order[i];
            keys[p] = 0.5f * (bmins[p * 3 + axis] + bmaxs[p * 3 + axis]);
        }
        g_sort_keys = keys;
        uint32_t mid = t.start + count / 2;
        select_nth(order, t.start, (int64_t)t.end - 1, mid);
        uint32_t l = used++, r = used++;
        nd->left = l;
        nd->right = r;
        nd->count = 0;
        stack[sp++] = (BuildTask){ l, t.start, mid };
        stack[sp++] = (BuildTask){ r, mid, t.end };
    }
    g_sort_keys = NULL;
    SSS_FREE(keys);
    SSS_FREE(stack);
    *out_nodes = used;
    return nodes;
}

static void tribvh_build(TriBVH* b, const float* pos, const uint32_t* idx, uint32_t nt) {
    float* mins = SSS_ALLOC((size_t)nt * 3, sizeof(float));
    float* maxs = SSS_ALLOC((size_t)nt * 3, sizeof(float));
    for (uint32_t t = 0; t < nt; t++) {
        for (int k = 0; k < 3; k++) { mins[t * 3 + k] = FLT_MAX; maxs[t * 3 + k] = -FLT_MAX; }
        for (int v = 0; v < 3; v++) {
            const float* p = &pos[idx[t * 3 + v] * 3];
            for (int k = 0; k < 3; k++) {
                if (p[k] < mins[t * 3 + k]) mins[t * 3 + k] = p[k];
                if (p[k] > maxs[t * 3 + k]) maxs[t * 3 + k] = p[k];
            }
        }
    }
    b->order = SSS_ALLOC(nt, sizeof(uint32_t));
    b->nodes = build_bvh(nt, mins, maxs, 4, b->order, &b->nnodes);
    b->pos = pos;
    b->idx = idx;
    SSS_FREE(mins);
    SSS_FREE(maxs);
}

static void tribvh_free(TriBVH* b) {
    SSS_FREE(b->nodes);
    SSS_FREE(b->order);
    memset(b, 0, sizeof(*b));
}

static inline int ray_box(const float* o, const float* inv, const float* bmin, const float* bmax, float tmin, float tmax) {
    for (int k = 0; k < 3; k++) {
        float t0 = (bmin[k] - o[k]) * inv[k], t1 = (bmax[k] - o[k]) * inv[k];
        if (t0 > t1) { float s = t0; t0 = t1; t1 = s; }
        if (t0 > tmin) tmin = t0;
        if (t1 < tmax) tmax = t1;
        if (tmax < tmin) return 0;
    }
    return 1;
}

static inline float ray_tri(const float* o, const float* d, const float* a, const float* b, const float* c) {
    float e1[3], e2[3], h[3], s[3], q[3];
    sub3(b, a, e1); sub3(c, a, e2);
    cross3(d, e2, h);
    float det = dot3(e1, h);
    if (fabsf(det) < 1e-20f) return -1.0f;
    float f = 1.0f / det;
    sub3(o, a, s);
    float u = f * dot3(s, h);
    if (u < 0.0f || u > 1.0f) return -1.0f;
    cross3(s, e1, q);
    float v = f * dot3(d, q);
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    return f * dot3(e2, q);
}

static void safe_inv(const float* d, float* inv) {
    for (int k = 0; k < 3; k++) inv[k] = 1.0f / (fabsf(d[k]) < 1e-20f ? (d[k] < 0 ? -1e-20f : 1e-20f) : d[k]);
}

// closest hit with t in (tmin, tmax); returns -1 on a miss
static float tribvh_trace(const TriBVH* b, const float* o, const float* d, float tmin, float tmax, uint32_t* tri) {
    float inv[3];
    safe_inv(d, inv);
    uint32_t stack[128];
    int sp = 0;
    stack[sp++] = 0;
    float best = -1.0f;
    float limit = tmax;
    while (sp > 0) {
        const TriNode* n = &b->nodes[stack[--sp]];
        if (!ray_box(o, inv, n->bmin, n->bmax, tmin, limit)) continue;
        if (n->count) {
            for (uint32_t i = 0; i < n->count; i++) {
                uint32_t t = b->order[n->left + i];
                const uint32_t* id = &b->idx[t * 3];
                float h = ray_tri(o, d, &b->pos[id[0] * 3], &b->pos[id[1] * 3], &b->pos[id[2] * 3]);
                if (h > tmin && h < limit) { limit = h; best = h; *tri = t; }
            }
        } else if (sp < 126) {
            stack[sp++] = n->left;
            stack[sp++] = n->right;
        }
    }
    return best;
}

// number of surface crossings along the ray (used for inside / outside parity)
static uint32_t tribvh_count(const TriBVH* b, const float* o, const float* d, float tmin) {
    float inv[3];
    safe_inv(d, inv);
    uint32_t stack[128];
    int sp = 0;
    stack[sp++] = 0;
    uint32_t hits = 0;
    while (sp > 0) {
        const TriNode* n = &b->nodes[stack[--sp]];
        if (!ray_box(o, inv, n->bmin, n->bmax, tmin, FLT_MAX)) continue;
        if (n->count) {
            for (uint32_t i = 0; i < n->count; i++) {
                uint32_t t = b->order[n->left + i];
                const uint32_t* id = &b->idx[t * 3];
                float h = ray_tri(o, d, &b->pos[id[0] * 3], &b->pos[id[1] * 3], &b->pos[id[2] * 3]);
                if (h > tmin) hits++;
            }
        } else if (sp < 126) {
            stack[sp++] = n->left;
            stack[sp++] = n->right;
        }
    }
    return hits;
}

static float caster_trace(void* user, const float* o, const float* d, float tmax, uint32_t* tri) {
    const TriBVH* b = (const TriBVH*)user;
    return tribvh_trace(b, o, d, 0.0f, tmax, tri);
}

// ================================================================================================================
// uniform grid for nearest neighbour queries
// ================================================================================================================

typedef struct {
    const float* pts; // stride 3
    uint32_t n;
    float origin[3];
    float h;
    int dims[3];
    uint32_t* start; // cell -> first item (prefix sums), size cells + 1
    uint32_t* items;
} Grid;

static void grid_build(Grid* g, const float* pts, uint32_t n, float h) {
    memset(g, 0, sizeof(*g));
    g->pts = pts;
    g->n = n;
    float bmin[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, bmax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (uint32_t i = 0; i < n; i++)
        for (int k = 0; k < 3; k++) {
            if (pts[i * 3 + k] < bmin[k]) bmin[k] = pts[i * 3 + k];
            if (pts[i * 3 + k] > bmax[k]) bmax[k] = pts[i * 3 + k];
        }
    if (n == 0) for (int k = 0; k < 3; k++) { bmin[k] = 0; bmax[k] = 1; }
    float maxext = fmaxf(bmax[0] - bmin[0], fmaxf(bmax[1] - bmin[1], bmax[2] - bmin[2]));
    if (!(h > 0.0f)) h = maxext > 0 ? maxext : 1.0f;
    for (;;) {
        double cells = 1.0;
        for (int k = 0; k < 3; k++) cells *= floor((bmax[k] - bmin[k]) / h) + 1.0;
        if (cells <= 4.0 * (double)n + 64.0 && cells <= 16777216.0) break;
        h *= 1.25f;
    }
    g->h = h;
    for (int k = 0; k < 3; k++) {
        g->origin[k] = bmin[k];
        g->dims[k] = (int)floorf((bmax[k] - bmin[k]) / h) + 1;
    }
    size_t cells = (size_t)g->dims[0] * g->dims[1] * g->dims[2];
    g->start = SSS_ALLOC(cells + 1, sizeof(uint32_t));
    g->items = SSS_ALLOC(n, sizeof(uint32_t));
    uint32_t* cellof = SSS_ALLOC(n, sizeof(uint32_t));
    for (uint32_t i = 0; i < n; i++) {
        int c[3];
        for (int k = 0; k < 3; k++) {
            c[k] = (int)floorf((pts[i * 3 + k] - g->origin[k]) / h);
            if (c[k] < 0) c[k] = 0;
            if (c[k] >= g->dims[k]) c[k] = g->dims[k] - 1;
        }
        cellof[i] = (uint32_t)(c[0] + g->dims[0] * (c[1] + g->dims[1] * c[2]));
        g->start[cellof[i] + 1]++;
    }
    for (size_t c = 0; c < cells; c++) g->start[c + 1] += g->start[c];
    uint32_t* cursor = SSS_ALLOC(cells, sizeof(uint32_t));
    for (uint32_t i = 0; i < n; i++) g->items[g->start[cellof[i]] + cursor[cellof[i]]++] = i;
    SSS_FREE(cursor);
    SSS_FREE(cellof);
}

static void grid_free(Grid* g) {
    SSS_FREE(g->start);
    SSS_FREE(g->items);
    memset(g, 0, sizeof(*g));
}

// k nearest items to q, sorted by distance. Returns how many were found (<= k, k <= 32).
static uint32_t grid_knn(const Grid* g, const float* q, uint32_t k, uint32_t* out, float* outd2) {
    if (g->n == 0 || k == 0) return 0;
    int c[3];
    for (int a = 0; a < 3; a++) {
        c[a] = (int)floorf((q[a] - g->origin[a]) / g->h);
        if (c[a] < 0) c[a] = 0;
        if (c[a] >= g->dims[a]) c[a] = g->dims[a] - 1;
    }
    uint32_t found = 0;
    int maxr = g->dims[0];
    if (g->dims[1] > maxr) maxr = g->dims[1];
    if (g->dims[2] > maxr) maxr = g->dims[2];
    for (int r = 0; r <= maxr; r++) {
        for (int z = c[2] - r; z <= c[2] + r; z++) {
            if (z < 0 || z >= g->dims[2]) continue;
            for (int y = c[1] - r; y <= c[1] + r; y++) {
                if (y < 0 || y >= g->dims[1]) continue;
                for (int x = c[0] - r; x <= c[0] + r; x++) {
                    if (x < 0 || x >= g->dims[0]) continue;
                    if (abs(x - c[0]) != r && abs(y - c[1]) != r && abs(z - c[2]) != r) continue; // shell only
                    size_t cell = (size_t)x + (size_t)g->dims[0] * ((size_t)y + (size_t)g->dims[1] * (size_t)z);
                    for (uint32_t i = g->start[cell]; i < g->start[cell + 1]; i++) {
                        uint32_t id = g->items[i];
                        float d[3];
                        sub3(&g->pts[id * 3], q, d);
                        float d2 = dot3(d, d);
                        if (found == k && d2 >= outd2[k - 1]) continue;
                        uint32_t j = found < k ? found++ : k - 1;
                        while (j > 0 && outd2[j - 1] > d2) { outd2[j] = outd2[j - 1]; out[j] = out[j - 1]; j--; }
                        outd2[j] = d2;
                        out[j] = id;
                    }
                }
            }
        }
        // everything not visited yet is at least r * h away
        float reach = (float)r * g->h;
        if (found == k && outd2[k - 1] <= reach * reach) break;
    }
    return found;
}

// ================================================================================================================
// fitter state
// ================================================================================================================

typedef struct {
    float in[3], out[3], dir[3];
    uint32_t tin, tout;
    float length;
    uint32_t events;
    uint32_t sg_in, sg_out;
} WalkRec;

typedef struct {
    float mean[3];
    float weight;
    float L[9];
    float axis[3];
} Lobe;

typedef struct {
    float mu[3];
    float R[9];     // column major rotation (columns = principal axes)
    float s[3];     // standard deviations
    float W[9];     // column major S^-1 R^T
    float q[SSS_Q]; // path length field coefficients
} Gauss;

typedef struct { float m, v; } AdamSlot;

struct SSSFitter {
    SSSFitConfig cfg;
    SSSFitPhase phase;
    char status[192];
    SSSFitStats stats;
    Rng rng;
    // mesh
    float* pos;
    uint32_t* idx;
    uint32_t nv, nt;
    float* tn;       // outward unit normal per triangle
    uint8_t* flip;
    double* cdf;
    double area;
    float sigma, g, mfp, diag, eps;
    TriBVH bvh;
    // surface gaussians
    uint32_t ns;
    float* sc;       // centre
    float* snrm;     // normal
    float* st;       // tangential sigma
    float* sn;       // normal sigma
    uint32_t* table; // 3 per triangle
    float* daxis;
    float* dexp;
    uint32_t* lfirst;
    uint32_t* lcount;
    uint32_t* lbucket_start; // training records bucketed by entry gaussian (lobe phase)
    uint32_t* lbucket_items;
    uint32_t lcursor;
    float* cl_centres;       // internal cloud k-means state (cloud phase)
    uint32_t* cl_assign;
    double* cl_acc;
    float cl_spacing;
    uint32_t cl_iter;
    uint32_t* nin;
    uint32_t* nout;
    Lobe* lobes;
    uint32_t nlobes, lobecap;
    // walks
    WalkRec* rec;
    uint32_t nrec, target, attempts;
    uint32_t holdout;    // records [holdout, nrec) are held out for validation
    double steps_sum, events_sum, length_sum;
    double poisson_true[3], poisson_model[3];
    float* interior;
    uint32_t ninterior;
    uint64_t interior_seen;
    // internal cloud
    uint32_t ng;
    Gauss* gs;
    TriNode* gnodes;
    uint32_t ngnodes;
    float bg[SSS_Q];
    // features (training records only)
    uint32_t nfeat;
    uint32_t* fstart;
    uint32_t* fidx;
    float* fval;
    float* fbg;
    float* fy;
    float* fu;
    uint32_t nentries, entrycap;
    uint32_t feat_cursor;
    float init_q[SSS_Q];
    float ev_k, ev_c; // calibrated event count model: E[a^n | L] ~ a^(1 + c) exp(-k sigma (1 - a) L)
    AdamSlot* adam;
    float* grad;
    uint32_t epoch;
    // validation
    uint32_t val_cursor, val_count;
    float* val_model_d;
    float* val_model_l;
    float* val_walk_d;
    float* val_walk_l;
    double val_cos_model, val_cos_walk;
    // result
    uint32_t* blob;
    uint32_t blob_words;
};

SSSFitConfig SSSFitDefaultConfig() {
    SSSFitConfig c = { 0 };
    c.surface_gaussians = 384;
    c.internal_gaussians = 1024;
    c.walks = 300000;
    c.lobes = 6;
    c.max_steps = 20000;
    c.epochs = 250;
    c.validation = 20000;
    c.seed = 0x5EED5u;
    return c;
}

static void set_status(SSSFitter* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(f->status, sizeof(f->status), fmt, ap);
    va_end(ap);
}

static void fail(SSSFitter* f, const char* why) {
    set_status(f, "Failed: %s", why);
    f->phase = SSSFIT_FAILED;
}

SSSFitter* SSSFitCreate(const SSSFitInput* in, const SSSFitConfig* cfg) {
    if (!in || !cfg || in->triangle_count == 0 || in->vertex_count == 0 || !(in->sigma > 0.0f)) return NULL;
    SSSFitter* f = SSS_ALLOC(1, sizeof(SSSFitter));
    f->cfg = *cfg;
    if (f->cfg.surface_gaussians < 4) f->cfg.surface_gaussians = 4;
    if (f->cfg.internal_gaussians < 4) f->cfg.internal_gaussians = 4;
    if (f->cfg.walks < 1000) f->cfg.walks = 1000;
    if (f->cfg.lobes < 1) f->cfg.lobes = 1;
    if (f->cfg.lobes > 8) f->cfg.lobes = 8;
    if (f->cfg.max_steps < 64) f->cfg.max_steps = 64;
    f->nv = in->vertex_count;
    f->nt = in->triangle_count;
    f->pos = SSS_ALLOC((size_t)f->nv * 3, sizeof(float));
    f->idx = SSS_ALLOC((size_t)f->nt * 3, sizeof(uint32_t));
    memcpy(f->pos, in->positions, (size_t)f->nv * 3 * sizeof(float));
    memcpy(f->idx, in->indices, (size_t)f->nt * 3 * sizeof(uint32_t));
    for (size_t i = 0; i < (size_t)f->nt * 3; i++) if (f->idx[i] >= f->nv) f->idx[i] = 0;
    f->sigma = in->sigma;
    f->g = clampf(in->g, -0.99f, 0.99f);
    f->mfp = 1.0f / in->sigma;
    f->rng.s = 0x853c49e6748fea9bull ^ ((uint64_t)f->cfg.seed * 0x9E3779B97F4A7C15ull);
    f->phase = SSSFIT_SETUP;
    set_status(f, "Preparing mesh");
    return f;
}

void SSSFitDestroy(SSSFitter* f) {
    if (!f) return;
    SSS_FREE(f->pos); SSS_FREE(f->idx); SSS_FREE(f->tn); SSS_FREE(f->flip); SSS_FREE(f->cdf);
    tribvh_free(&f->bvh);
    SSS_FREE(f->sc); SSS_FREE(f->snrm); SSS_FREE(f->st); SSS_FREE(f->sn); SSS_FREE(f->table);
    SSS_FREE(f->daxis); SSS_FREE(f->dexp); SSS_FREE(f->lfirst); SSS_FREE(f->lcount); SSS_FREE(f->nin); SSS_FREE(f->nout);
    SSS_FREE(f->lobes);
    SSS_FREE(f->lbucket_start); SSS_FREE(f->lbucket_items);
    SSS_FREE(f->cl_centres); SSS_FREE(f->cl_assign); SSS_FREE(f->cl_acc);
    SSS_FREE(f->rec); SSS_FREE(f->interior);
    SSS_FREE(f->gs); SSS_FREE(f->gnodes);
    SSS_FREE(f->fstart); SSS_FREE(f->fidx); SSS_FREE(f->fval); SSS_FREE(f->fbg); SSS_FREE(f->fy); SSS_FREE(f->fu);
    SSS_FREE(f->adam); SSS_FREE(f->grad);
    SSS_FREE(f->val_model_d); SSS_FREE(f->val_model_l); SSS_FREE(f->val_walk_d); SSS_FREE(f->val_walk_l);
    SSS_FREE(f->blob);
    SSS_FREE(f);
}

SSSFitPhase SSSFitPhaseNow(const SSSFitter* f) { return f ? f->phase : SSSFIT_FAILED; }
const char* SSSFitStatus(const SSSFitter* f) { return f ? f->status : "No fitter"; }
const SSSFitStats* SSSFitGetStats(const SSSFitter* f) { return f ? &f->stats : NULL; }

uint32_t SSSFitHeldOut(const SSSFitter* f) {
    return f && f->phase == SSSFIT_DONE ? f->nrec - f->holdout : 0;
}

int SSSFitCompare(SSSFitter* f, uint32_t i, float* walk_length, uint32_t* walk_events, float* model_length) {
    if (!f || f->phase != SSSFIT_DONE || f->holdout + i >= f->nrec) return 0;
    const WalkRec* w = &f->rec[f->holdout + i];
    SSSCaster cs = { caster_trace, &f->bvh, f->pos, f->idx };
    SSSSample smp;
    sssfit_sample(f->blob, &cs, w->in, w->tin, rng_cb, &f->rng, &smp);
    *walk_length = w->length;
    *walk_events = w->events;
    *model_length = smp.length;
    return 1;
}

const uint32_t* SSSFitBlob(const SSSFitter* f, uint32_t* words) {
    if (!f || f->phase != SSSFIT_DONE) { if (words) *words = 0; return NULL; }
    if (words) *words = f->blob_words;
    return f->blob;
}

float SSSFitProgress(const SSSFitter* f) {
    if (!f) return 0.0f;
    switch (f->phase) {
        case SSSFIT_SETUP: return 0.0f;
        case SSSFIT_SURFACE: return 0.02f;
        case SSSFIT_WALKS: return 0.05f + 0.55f * (float)f->nrec / (float)(f->target ? f->target : 1);
        case SSSFIT_LOBES: return 0.60f + 0.05f * (float)f->lcursor / (float)(f->ns ? f->ns : 1);
        case SSSFIT_CLOUD: return 0.65f + 0.1f * (float)f->feat_cursor / (float)(f->nfeat ? f->nfeat : 1);
        case SSSFIT_OPTIMISE: return 0.75f + 0.2f * (float)f->epoch / (float)(f->cfg.epochs ? f->cfg.epochs : 1);
        case SSSFIT_VALIDATE: return 0.95f + 0.05f * (float)f->val_cursor / (float)(f->val_count ? f->val_count : 1);
        case SSSFIT_DONE: return 1.0f;
        default: return 0.0f;
    }
}

static void tri_point(const SSSFitter* f, uint32_t t, float r1, float r2, float* out) {
    float s = sqrtf(r1);
    float u = 1.0f - s, v = s * (1.0f - r2), w = s * r2;
    const float* a = &f->pos[f->idx[t * 3] * 3];
    const float* b = &f->pos[f->idx[t * 3 + 1] * 3];
    const float* c = &f->pos[f->idx[t * 3 + 2] * 3];
    for (int k = 0; k < 3; k++) out[k] = u * a[k] + v * b[k] + w * c[k];
}

static uint32_t pick_triangle(const SSSFitter* f, float r) {
    double target = (double)r * f->area;
    uint32_t lo = 0, hi = f->nt - 1;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (f->cdf[mid] < target) lo = mid + 1; else hi = mid;
    }
    return lo;
}

// ================================================================================================================
// phase: setup (BVH, outward normals, area distribution)
// ================================================================================================================

static void phase_setup(SSSFitter* f) {
    float bmin[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, bmax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (uint32_t i = 0; i < f->nv; i++)
        for (int k = 0; k < 3; k++) {
            if (f->pos[i * 3 + k] < bmin[k]) bmin[k] = f->pos[i * 3 + k];
            if (f->pos[i * 3 + k] > bmax[k]) bmax[k] = f->pos[i * 3 + k];
        }
    float d[3];
    sub3(bmax, bmin, d);
    f->diag = len3(d);
    if (!(f->diag > 0.0f)) { fail(f, "mesh has no extent"); return; }
    f->eps = 1e-5f * f->diag;
    tribvh_build(&f->bvh, f->pos, f->idx, f->nt);
    f->tn = SSS_ALLOC((size_t)f->nt * 3, sizeof(float));
    f->flip = SSS_ALLOC(f->nt, 1);
    f->cdf = SSS_ALLOC(f->nt, sizeof(double));
    double total = 0.0;
    uint32_t flipped = 0;
    for (uint32_t t = 0; t < f->nt; t++) {
        const float* a = &f->pos[f->idx[t * 3] * 3];
        const float* b = &f->pos[f->idx[t * 3 + 1] * 3];
        const float* c = &f->pos[f->idx[t * 3 + 2] * 3];
        float e1[3], e2[3], n[3];
        sub3(b, a, e1); sub3(c, a, e2);
        cross3(e1, e2, n);
        float l = len3(n);
        total += 0.5 * l;
        f->cdf[t] = total;
        if (l <= 0.0f) { f->tn[t * 3 + 2] = 1.0f; continue; }
        for (int k = 0; k < 3; k++) n[k] /= l;
        // parity vote: a ray leaving through the outward side crosses the surface an even number of times
        int inward_votes = 0;
        for (int v = 0; v < 3; v++) {
            float o[3], dir[3], jitter[3] = { rng_f(&f->rng) - 0.5f, rng_f(&f->rng) - 0.5f, rng_f(&f->rng) - 0.5f };
            tri_point(f, t, rng_f(&f->rng), rng_f(&f->rng), o);
            for (int k = 0; k < 3; k++) dir[k] = n[k] + 0.3f * jitter[k];
            norm3(dir);
            if (dot3(dir, n) < 0.2f) memcpy(dir, n, sizeof(dir));
            uint32_t hits = tribvh_count(&f->bvh, o, dir, f->eps);
            if (hits & 1u) inward_votes++;
        }
        if (inward_votes >= 2) {
            f->flip[t] = 1;
            flipped++;
            for (int k = 0; k < 3; k++) n[k] = -n[k];
        }
        memcpy(&f->tn[t * 3], n, sizeof(n));
    }
    f->area = total;
    if (!(total > 0.0)) { fail(f, "mesh has no surface area"); return; }
    f->phase = SSSFIT_SURFACE;
    set_status(f, "Clustering surface gaussians");
    (void)flipped;
}

// ================================================================================================================
// phase: surface gaussians
// ================================================================================================================

static float surface_weight(const float* c, const float* n, float st, float sn, const float* p) {
    float d[3];
    sub3(p, c, d);
    float dn = dot3(d, n);
    float dt2 = fmaxf(dot3(d, d) - dn * dn, 0.0f);
    return expf(-0.5f * (dt2 / (st * st) + dn * dn / (sn * sn)));
}

static uint32_t pick_surface(const SSSFitter* f, uint32_t tri, const float* p, float r) {
    const uint32_t* c = &f->table[tri * 3];
    float w[3] = { 0, 0, 0 }, total = 0.0f;
    for (int k = 0; k < 3; k++) {
        if (c[k] == SSSG_NONE) continue;
        w[k] = surface_weight(&f->sc[c[k] * 3], &f->snrm[c[k] * 3], f->st[c[k]], f->sn[c[k]], p);
        total += w[k];
    }
    if (!(total > 1e-30f)) return c[0];
    float x = r * total;
    for (int k = 0; k < 3; k++) {
        if (c[k] == SSSG_NONE) continue;
        if (x <= w[k]) return c[k];
        x -= w[k];
    }
    return c[0];
}

static void phase_surface(SSSFitter* f) {
    uint32_t ns = f->cfg.surface_gaussians;
    uint32_t m = ns * SSS_SURFACE_SAMPLES_PER;
    if (m < 20000) m = 20000;
    float* sp = SSS_ALLOC((size_t)m * 3, sizeof(float));
    float* snr = SSS_ALLOC((size_t)m * 3, sizeof(float));
    uint32_t* stri = SSS_ALLOC(m, sizeof(uint32_t));
    for (uint32_t i = 0; i < m; i++) {
        uint32_t t = pick_triangle(f, rng_f(&f->rng));
        tri_point(f, t, rng_f(&f->rng), rng_f(&f->rng), &sp[i * 3]);
        memcpy(&snr[i * 3], &f->tn[t * 3], 12);
        stri[i] = t;
    }
    if (ns > m) ns = m;
    f->ns = ns;
    f->sc = SSS_ALLOC((size_t)ns * 3, sizeof(float));
    f->snrm = SSS_ALLOC((size_t)ns * 3, sizeof(float));
    f->st = SSS_ALLOC(ns, sizeof(float));
    f->sn = SSS_ALLOC(ns, sizeof(float));
    // samples are already random: the first ns seed the clusters
    memcpy(f->sc, sp, (size_t)ns * 12);
    memcpy(f->snrm, snr, (size_t)ns * 12);
    float spacing = (float)sqrt(f->area / (double)ns);
    uint32_t* assign = SSS_ALLOC(m, sizeof(uint32_t));
    double* acc = SSS_ALLOC((size_t)ns * 7, sizeof(double));
    for (int iter = 0; iter < 12; iter++) {
        Grid grid;
        grid_build(&grid, f->sc, ns, spacing);
        for (uint32_t i = 0; i < m; i++) {
            uint32_t ids[8] = { 0 };
            float d2[8];
            uint32_t k = grid_knn(&grid, &sp[i * 3], 8, ids, d2);
            uint32_t best = ids[0];
            float bestv = FLT_MAX;
            for (uint32_t j = 0; j < k; j++) {
                float pen = dot3(&snr[i * 3], &f->snrm[ids[j] * 3]) > 0.3f ? 1.0f : 16.0f;
                if (d2[j] * pen < bestv) { bestv = d2[j] * pen; best = ids[j]; }
            }
            assign[i] = best;
        }
        grid_free(&grid);
        memset(acc, 0, (size_t)ns * 7 * sizeof(double));
        for (uint32_t i = 0; i < m; i++) {
            double* a = &acc[assign[i] * 7];
            for (int k = 0; k < 3; k++) { a[k] += sp[i * 3 + k]; a[3 + k] += snr[i * 3 + k]; }
            a[6] += 1.0;
        }
        for (uint32_t c = 0; c < ns; c++) {
            double* a = &acc[c * 7];
            if (a[6] < 0.5) {
                uint32_t r = rng_u32(&f->rng) % m; // empty cluster: reseed on a random sample
                memcpy(&f->sc[c * 3], &sp[r * 3], 12);
                memcpy(&f->snrm[c * 3], &snr[r * 3], 12);
                continue;
            }
            for (int k = 0; k < 3; k++) { f->sc[c * 3 + k] = (float)(a[k] / a[6]); f->snrm[c * 3 + k] = (float)a[3 + k]; }
            norm3(&f->snrm[c * 3]);
            if (len3(&f->snrm[c * 3]) < 0.5f) memcpy(&f->snrm[c * 3], &snr[c * 3], 12);
        }
    }
    // footprint of each cluster
    memset(acc, 0, (size_t)ns * 7 * sizeof(double));
    for (uint32_t i = 0; i < m; i++) {
        uint32_t c = assign[i];
        float d[3];
        sub3(&sp[i * 3], &f->sc[c * 3], d);
        float dn = dot3(d, &f->snrm[c * 3]);
        acc[c * 7 + 0] += fmax(dot3(d, d) - dn * dn, 0.0);
        acc[c * 7 + 1] += dn * dn;
        acc[c * 7 + 6] += 1.0;
    }
    for (uint32_t c = 0; c < ns; c++) {
        double n = acc[c * 7 + 6] > 0 ? acc[c * 7 + 6] : 1.0;
        float st = (float)sqrt(acc[c * 7 + 0] / n * 0.5);
        float sn = (float)sqrt(acc[c * 7 + 1] / n);
        st = fmaxf(st, 0.4f * spacing);
        st = fminf(st, 2.0f * spacing);
        f->st[c] = st;
        f->sn[c] = fmaxf(sn, 0.25f * st);
    }
    // per triangle candidate table: nearest clusters on the same side of the surface
    f->table = SSS_ALLOC((size_t)f->nt * 3, sizeof(uint32_t));
    Grid grid;
    grid_build(&grid, f->sc, ns, spacing);
    for (uint32_t t = 0; t < f->nt; t++) {
        float cen[3];
        const float* a = &f->pos[f->idx[t * 3] * 3];
        const float* b = &f->pos[f->idx[t * 3 + 1] * 3];
        const float* c = &f->pos[f->idx[t * 3 + 2] * 3];
        for (int k = 0; k < 3; k++) cen[k] = (a[k] + b[k] + c[k]) / 3.0f;
        uint32_t ids[16];
        float d2[16];
        uint32_t k = grid_knn(&grid, cen, 16, ids, d2);
        uint32_t* row = &f->table[t * 3];
        row[0] = row[1] = row[2] = SSSG_NONE;
        uint32_t filled = 0;
        for (uint32_t j = 0; j < k && filled < 3; j++)
            if (dot3(&f->tn[t * 3], &f->snrm[ids[j] * 3]) > 0.25f) row[filled++] = ids[j];
        if (filled == 0) row[0] = ids[0];
    }
    grid_free(&grid);
    SSS_FREE(acc);
    SSS_FREE(assign);
    SSS_FREE(sp);
    SSS_FREE(snr);
    SSS_FREE(stri);
    f->target = f->cfg.walks;
    f->rec = SSS_ALLOC(f->target, sizeof(WalkRec));
    f->interior = SSS_ALLOC((size_t)SSS_INTERIOR_CAP * 3, sizeof(float));
    f->phase = SSSFIT_WALKS;
    set_status(f, "Tracing reference walks 0/%u", f->target);
}

// ================================================================================================================
// phase: reference random walks
// ================================================================================================================

static void add_interior(SSSFitter* f, const float* p) {
    f->interior_seen++;
    if (f->ninterior < SSS_INTERIOR_CAP) {
        memcpy(&f->interior[f->ninterior++ * 3], p, 12);
        return;
    }
    uint64_t r = ((uint64_t)rng_u32(&f->rng) << 32 | rng_u32(&f->rng)) % f->interior_seen;
    if (r < SSS_INTERIOR_CAP) memcpy(&f->interior[r * 3], p, 12);
}

// One geometric walk. Light enters through a cosine weighted incident direction; the first direction inside follows
// the phase function around it (same convention as subsurface_walk in path.comp).
static int trace_walk(SSSFitter* f, WalkRec* w) {
    uint32_t t = pick_triangle(f, rng_f(&f->rng));
    float p[3];
    tri_point(f, t, rng_f(&f->rng), rng_f(&f->rng), p);
    const float* n = &f->tn[t * 3];
    float inward[3] = { -n[0], -n[1], -n[2] };
    float wi[3], d[3];
    sample_cosine(inward, &f->rng, wi);
    sample_hg(wi, f->g, &f->rng, d);
    float dn = dot3(d, n);
    if (dn > 0.0f) for (int k = 0; k < 3; k++) d[k] -= 2.0f * dn * n[k];
    float x[3];
    memcpy(x, p, 12);
    double L = 0.0;
    uint32_t events = 0;
    float interior_pick[3];
    int have_interior = 0;
    for (uint32_t step = 0; step < f->cfg.max_steps; step++) {
        uint32_t tri = 0;
        float tb = tribvh_trace(&f->bvh, x, d, f->eps, FLT_MAX, &tri);
        f->steps_sum += 1.0;
        if (tb < 0.0f) return 0; // leaked
        float xi = fmaxf(rng_f(&f->rng), 1e-7f);
        float fd = -logf(xi) / f->sigma;
        events++;
        if (fd >= tb) {
            if (dot3(d, &f->tn[tri * 3]) <= 0.0f) return 0; // hit the surface from outside: we had leaked
            for (int k = 0; k < 3; k++) w->out[k] = x[k] + d[k] * tb;
            L += tb;
            memcpy(w->in, p, 12);
            memcpy(w->dir, d, 12);
            w->tin = t;
            w->tout = tri;
            w->length = (float)L;
            w->events = events;
            if (have_interior) add_interior(f, interior_pick);
            return 1;
        }
        for (int k = 0; k < 3; k++) x[k] += d[k] * fd;
        L += fd;
        // keep one scattering location per walk (uniformly chosen) for seeding the internal cloud
        if (rng_u32(&f->rng) % events == 0) { memcpy(interior_pick, x, 12); have_interior = 1; }
        float nd[3];
        sample_hg(d, f->g, &f->rng, nd);
        memcpy(d, nd, 12);
    }
    return 0;
}

static void phase_walks(SSSFitter* f, double budget, SSSClockFn clock) {
    double start = clock();
    while (f->nrec < f->target) {
        for (int i = 0; i < 64 && f->nrec < f->target; i++) {
            f->attempts++;
            WalkRec w = { 0 };
            if (trace_walk(f, &w)) {
                f->events_sum += (double)(w.events - 1);
                f->length_sum += (double)f->sigma * w.length;
                for (int k = 0; k < 3; k++) {
                    double a = k == 0 ? 0.5 : (k == 1 ? 0.9 : 0.99);
                    f->poisson_true[k] += pow(a, (double)w.events);
                    f->poisson_model[k] += a * exp(-(double)f->sigma * (1.0 - a) * w.length);
                }
                f->rec[f->nrec++] = w;
            } else {
                f->stats.walks_lost++;
            }
            if (f->attempts > 64 && f->stats.walks_lost > f->attempts / 2) {
                fail(f, "more than half of the walks escaped, the mesh is probably not closed");
                return;
            }
        }
        set_status(f, "Tracing reference walks %u/%u", f->nrec, f->target);
        if (clock() - start >= budget) return;
    }
    f->stats.walks_kept = f->nrec;
    f->stats.mean_steps = f->steps_sum / (double)(f->attempts ? f->attempts : 1);
    f->stats.events_per_length = f->events_sum / fmax(f->length_sum, 1e-9);
    for (int k = 0; k < 3; k++) {
        f->stats.albedo_true[k] = f->poisson_true[k] / (double)(f->nrec ? f->nrec : 1);
        f->stats.albedo_model[k] = f->poisson_model[k] / (double)(f->nrec ? f->nrec : 1);
    }
    f->holdout = f->nrec - f->nrec / 10;
    f->phase = SSSFIT_LOBES;
    set_status(f, "Fitting exit lobes");
}

// ================================================================================================================
// phase: exit lobes and exit directions per surface gaussian
// ================================================================================================================

static void default_lobe(const float* n, float mfp, Lobe* l) {
    memset(l, 0, sizeof(*l));
    double c[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            c[i][j] = (double)mfp * mfp * ((i == j ? 1.0 : 0.0) - 0.99 * n[i] * n[j]);
    cholesky3(c, l->L);
    memcpy(l->axis, n, 12);
    l->weight = 1.0f;
}

// EM for a K component 3D gaussian mixture over offsets (and exit normals for the projection axes).
static uint32_t fit_gmm(SSSFitter* f, const float* pts, const float* nrm, uint32_t n, uint32_t K, const float* fallback_n, Lobe* out) {
    if (n < 4) { default_lobe(fallback_n, f->mfp, &out[0]); return 1; }
    if (K > n / 24) K = n / 24;
    if (K < 1) K = 1;
    double reg = (0.02 * f->mfp) * (0.02 * f->mfp) + 1e-14 * (double)f->diag * f->diag;
    double mu[8][3], cov[8][3][3], pi[8];
    // k-means++ seeding of the means
    uint32_t first = rng_u32(&f->rng) % n;
    for (int k = 0; k < 3; k++) mu[0][k] = pts[first * 3 + k];
    double* dmin = SSS_ALLOC(n, sizeof(double));
    for (uint32_t i = 0; i < n; i++) dmin[i] = DBL_MAX;
    for (uint32_t c = 1; c < K; c++) {
        double total = 0.0;
        for (uint32_t i = 0; i < n; i++) {
            double d2 = 0;
            for (int k = 0; k < 3; k++) { double d = pts[i * 3 + k] - mu[c - 1][k]; d2 += d * d; }
            if (d2 < dmin[i]) dmin[i] = d2;
            total += dmin[i];
        }
        double x = rng_f(&f->rng) * total;
        uint32_t pick = n - 1;
        for (uint32_t i = 0; i < n; i++) { x -= dmin[i]; if (x <= 0) { pick = i; break; } }
        for (int k = 0; k < 3; k++) mu[c][k] = pts[pick * 3 + k];
    }
    SSS_FREE(dmin);
    // global covariance as the starting point of every component
    double gm[3] = { 0, 0, 0 }, gc[3][3] = { { 0 } };
    for (uint32_t i = 0; i < n; i++) for (int k = 0; k < 3; k++) gm[k] += pts[i * 3 + k] / n;
    for (uint32_t i = 0; i < n; i++)
        for (int a = 0; a < 3; a++)
            for (int b = 0; b < 3; b++) gc[a][b] += (pts[i * 3 + a] - gm[a]) * (pts[i * 3 + b] - gm[b]) / n;
    for (uint32_t c = 0; c < K; c++) {
        pi[c] = 1.0 / K;
        for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) cov[c][a][b] = gc[a][b] / (double)K + (a == b ? reg : 0.0);
    }
    double* resp = SSS_ALLOC((size_t)n * K, sizeof(double));
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
                double d[3] = { pts[i * 3] - mu[c][0], pts[i * 3 + 1] - mu[c][1], pts[i * 3 + 2] - mu[c][2] };
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
                for (int k = 0; k < 3; k++) m[k] += r * pts[i * 3 + k];
            }
            if (rs < 1e-9) { pi[c] = 0; continue; }
            for (int k = 0; k < 3; k++) mu[c][k] = m[k] / rs;
            double cv[3][3] = { { 0 } };
            for (uint32_t i = 0; i < n; i++) {
                double r = resp[(size_t)i * K + c];
                double d[3] = { pts[i * 3] - mu[c][0], pts[i * 3 + 1] - mu[c][1], pts[i * 3 + 2] - mu[c][2] };
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
        cholesky3((const double (*)[3])cov[c], l->L);
        double ax[3] = { 0, 0, 0 };
        for (uint32_t i = 0; i < n; i++) for (int k = 0; k < 3; k++) ax[k] += resp[(size_t)i * K + c] * nrm[i * 3 + k];
        float a[3] = { (float)ax[0], (float)ax[1], (float)ax[2] };
        if (len3(a) < 1e-6f) memcpy(a, fallback_n, 12);
        norm3(a);
        memcpy(l->axis, a, 12);
        used++;
    }
    SSS_FREE(resp);
    if (used == 0) { default_lobe(fallback_n, f->mfp, &out[0]); return 1; }
    float total = 0;
    for (uint32_t c = 0; c < used; c++) total += out[c].weight;
    for (uint32_t c = 0; c < used; c++) out[c].weight /= total;
    return used;
}

// Fits the length only throughput model E[a^n | L] ~ a^(1 + c) exp(-k sigma (1 - a) L) to the walks. With k = 1,
// c = 0 this is the poisson approximation (scattering events at rate sigma along the path); the calibration absorbs
// how far real walks deviate from it (short walks scatter more often per unit length than a poisson process).
static void calibrate_events(SSSFitter* f) {
    const double as[6] = { 0.3, 0.5, 0.7, 0.85, 0.95, 0.99 };
    uint32_t n = f->holdout < 6000 ? f->holdout : 6000;
    double truth[6] = { 0 };
    for (uint32_t i = 0; i < n; i++) {
        const WalkRec* w = &f->rec[(uint32_t)(((uint64_t)i * f->holdout) / n)];
        for (int a = 0; a < 6; a++) truth[a] += pow(as[a], (double)w->events) / n;
    }
    double bestk = 1.0, bestc = 0.0, besterr = DBL_MAX;
    for (int ki = 0; ki <= 60; ki++) {
        double k = 0.5 + 0.025 * ki;
        double M[6] = { 0 };
        for (uint32_t i = 0; i < n; i++) {
            const WalkRec* w = &f->rec[(uint32_t)(((uint64_t)i * f->holdout) / n)];
            for (int a = 0; a < 6; a++) M[a] += as[a] * exp(-k * f->sigma * (1.0 - as[a]) * w->length) / n;
        }
        double num = 0, den = 0;
        for (int a = 0; a < 6; a++) {
            double la = log(as[a]);
            num += la * (log(truth[a]) - log(M[a]));
            den += la * la;
        }
        double c = fmin(fmax(num / den, -0.9), 3.0);
        double err = 0;
        for (int a = 0; a < 6; a++) {
            double r = log(truth[a]) - (log(M[a]) + c * log(as[a]));
            err += r * r;
        }
        if (err < besterr) { besterr = err; bestk = k; bestc = c; }
    }
    f->ev_k = (float)bestk;
    f->ev_c = (float)bestc;
    // report the calibrated model on the same three albedos as the walk statistics
    const double rep[3] = { 0.5, 0.9, 0.99 };
    for (int a = 0; a < 3; a++) {
        double m = 0;
        for (uint32_t i = 0; i < f->holdout; i++)
            m += pow(rep[a], 1.0 + bestc) * exp(-bestk * f->sigma * (1.0 - rep[a]) * f->rec[i].length);
        f->stats.albedo_model[a] = m / (f->holdout ? f->holdout : 1);
    }
    f->stats.event_k = bestk;
    f->stats.event_c = bestc;
}

static void phase_lobes(SSSFitter* f, double budget, SSSClockFn clock) {
    double tstart = clock();
    uint32_t ns = f->ns, ntrain = f->holdout;
    if (!f->lbucket_start) {
    calibrate_events(f);
    f->nin = SSS_ALLOC(ns, sizeof(uint32_t));
    f->nout = SSS_ALLOC(ns, sizeof(uint32_t));
    for (uint32_t i = 0; i < f->nrec; i++) {
        WalkRec* w = &f->rec[i];
        w->sg_in = pick_surface(f, w->tin, w->in, rng_f(&f->rng));
        w->sg_out = pick_surface(f, w->tout, w->out, rng_f(&f->rng));
        if (i < ntrain) { f->nin[w->sg_in]++; f->nout[w->sg_out]++; }
    }
    // bucket training records by entry gaussian
    uint32_t* start = SSS_ALLOC(ns + 1, sizeof(uint32_t));
    for (uint32_t c = 0; c < ns; c++) start[c + 1] = start[c] + f->nin[c];
    uint32_t* items = SSS_ALLOC(ntrain, sizeof(uint32_t));
    uint32_t* cur = SSS_ALLOC(ns, sizeof(uint32_t));
    for (uint32_t i = 0; i < ntrain; i++) items[start[f->rec[i].sg_in] + cur[f->rec[i].sg_in]++] = i;
    SSS_FREE(cur);
    f->lfirst = SSS_ALLOC(ns, sizeof(uint32_t));
    f->lcount = SSS_ALLOC(ns, sizeof(uint32_t));
    f->lobecap = ns * f->cfg.lobes;
    f->lobes = SSS_ALLOC(f->lobecap, sizeof(Lobe));
    f->lbucket_start = start;
    f->lbucket_items = items;
    f->lcursor = 0;
    }
    uint32_t* start = f->lbucket_start;
    uint32_t* items = f->lbucket_items;
    float* pts = SSS_ALLOC((size_t)SSS_EM_MAX_POINTS * 3, sizeof(float));
    float* nrm = SSS_ALLOC((size_t)SSS_EM_MAX_POINTS * 3, sizeof(float));
    for (; f->lcursor < ns; f->lcursor++) {
        uint32_t c = f->lcursor;
        if ((c & 7u) == 0u && c > 0) {
            set_status(f, "Fitting exit lobes %u/%u", c, ns);
            if (clock() - tstart >= budget) { SSS_FREE(pts); SSS_FREE(nrm); return; }
        }
        uint32_t cnt = start[c + 1] - start[c];
        uint32_t take = cnt < SSS_EM_MAX_POINTS ? cnt : SSS_EM_MAX_POINTS;
        for (uint32_t j = 0; j < take; j++) {
            uint32_t pickj = take == cnt ? j : (uint32_t)(((uint64_t)j * cnt) / take); // even subsample
            const WalkRec* w = &f->rec[items[start[c] + pickj]];
            sub3(w->out, w->in, &pts[j * 3]);
            memcpy(&nrm[j * 3], &f->tn[w->tout * 3], 12);
        }
        f->lfirst[c] = f->nlobes;
        f->lcount[c] = fit_gmm(f, pts, nrm, take, f->cfg.lobes, &f->snrm[c * 3], &f->lobes[f->nlobes]);
        f->nlobes += f->lcount[c];
    }
    SSS_FREE(pts);
    SSS_FREE(nrm);
    // exit directions: cosine power lobe per gaussian, fitted from the walks that left under it
    f->daxis = SSS_ALLOC((size_t)ns * 3, sizeof(float));
    f->dexp = SSS_ALLOC(ns, sizeof(float));
    double* dsum = SSS_ALLOC((size_t)ns * 3, sizeof(double));
    for (uint32_t i = 0; i < ntrain; i++) {
        const WalkRec* w = &f->rec[i];
        for (int k = 0; k < 3; k++) dsum[w->sg_out * 3 + k] += w->dir[k];
    }
    double* csum = SSS_ALLOC(ns, sizeof(double));
    for (uint32_t c = 0; c < ns; c++) {
        float a[3] = { (float)dsum[c * 3], (float)dsum[c * 3 + 1], (float)dsum[c * 3 + 2] };
        if (f->nout[c] < 8 || len3(a) < 1e-6f) memcpy(a, &f->snrm[c * 3], 12);
        norm3(a);
        memcpy(&f->daxis[c * 3], a, 12);
    }
    for (uint32_t i = 0; i < ntrain; i++) {
        const WalkRec* w = &f->rec[i];
        csum[w->sg_out] += dot3(w->dir, &f->daxis[w->sg_out * 3]);
    }
    for (uint32_t c = 0; c < ns; c++) {
        if (f->nout[c] < 8) { f->dexp[c] = 1.0f; continue; }
        double m = csum[c] / f->nout[c];
        // E[cos] of a normalised cos^e lobe is (e + 1) / (e + 2)
        double e = m >= 0.995 ? 200.0 : (2.0 * m - 1.0) / (1.0 - m);
        f->dexp[c] = (float)fmin(fmax(e, 0.0), 200.0);
    }
    SSS_FREE(dsum);
    SSS_FREE(csum);
    f->phase = SSSFIT_CLOUD;
    set_status(f, "Building internal cloud");
}

// ================================================================================================================
// internal cloud: shared chord evaluation (mirrors sss_cloud_eval in path.comp)
// ================================================================================================================

// Path length field: every internal gaussian (and the background) carries SSS_Q coefficients
//   q0 + q1 u, q2   : mean / log std of the first log normal component of log(excess + eps)
//   q3 + q4 u, q5   : mean / log std of the second component
//   q6              : logit of the first component's weight
// with u = log(1 + chord / mfp). Coefficients are blended along the chord with the gaussians' line integrals.
typedef struct {
    float c;          // sum of weights
    float q[SSS_Q];   // weighted coefficient sums
} CloudAcc;

typedef struct {
    float m1, s1, m2, s2, w1;
} LengthModel;

static void model_from_sums(float C, const float* Q, float u, LengthModel* lm) {
    float inv = 1.0f / C;
    lm->m1 = (Q[0] + u * Q[1]) * inv;
    lm->s1 = expf(clampf(Q[2] * inv, -6.0f, 4.0f));
    lm->m2 = (Q[3] + u * Q[4]) * inv;
    lm->s2 = expf(clampf(Q[5] * inv, -6.0f, 4.0f));
    lm->w1 = 1.0f / (1.0f + expf(-clampf(Q[6] * inv, -20.0f, 20.0f)));
}

// line integral of exp(-0.5 |W (x - mu)|^2) along p0 -> p1 (arc length units), 0 when outside the support
static float gauss_chord(const float* W, const float* mu, const float* p0, const float* p1, float D) {
    float r[3], e[3], o[3], dv[3];
    sub3(p0, mu, r);
    sub3(p1, p0, e);
    for (int k = 0; k < 3; k++) {
        o[k] = W[k] * r[0] + W[3 + k] * r[1] + W[6 + k] * r[2];
        dv[k] = W[k] * e[0] + W[3 + k] * e[1] + W[6 + k] * e[2];
    }
    float A = dot3(dv, dv);
    float od = dot3(o, dv);
    float tc = -od / A;
    float h2 = fmaxf(dot3(o, o) - od * od / A, 0.0f);
    if (h2 > SSS_GAUSS_CUTOFF) return 0.0f;
    float sa = sqrtf(A);
    float za = -tc * sa, zb = (1.0f - tc) * sa;
    if (zb < -3.0f || za > 3.0f) return 0.0f;
    return D * expf(-0.5f * h2) * SSS_SQRT_2PI / sa * (normal_cdf(zb) - normal_cdf(za));
}

static float gauss_point(const float* W, const float* mu, const float* p) {
    float r[3], o[3];
    sub3(p, mu, r);
    for (int k = 0; k < 3; k++) o[k] = W[k] * r[0] + W[3 + k] * r[1] + W[6 + k] * r[2];
    float q = dot3(o, o);
    if (q > SSS_GAUSS_CUTOFF) return 0.0f;
    return expf(-0.5f * q);
}

static inline int seg_box(const float* p0, const float* inv, const float* bmin, const float* bmax) {
    return ray_box(p0, inv, bmin, bmax, 0.0f, 1.0f);
}
static inline int point_box(const float* p, const float* bmin, const float* bmax) {
    return p[0] >= bmin[0] && p[1] >= bmin[1] && p[2] >= bmin[2] && p[0] <= bmax[0] && p[1] <= bmax[1] && p[2] <= bmax[2];
}

// Visits the cloud along a chord. When `cb` is set it is called for every contributing gaussian (training);
// otherwise the coefficients are blended from the packed blob (runtime).
typedef void (*ChordVisit)(void* user, uint32_t g, float c);

static float blob_chord(const uint32_t* blob, const float* p0, const float* p1, ChordVisit cb, void* user, CloudAcc* acc) {
    const uint32_t* gs = blob + blob[SSSG_H_GAUSS];
    const uint32_t* nodes = blob + blob[SSSG_H_NODE];
    uint32_t nnodes = blob[SSSG_H_NNODE];
    float dmin = u2f(blob[SSSG_H_DMIN]);
    float e[3];
    sub3(p1, p0, e);
    float D = len3(e);
    int point = D < dmin;
    float len = point ? dmin : D;
    float inv[3];
    safe_inv(e, inv);
    if (acc) memset(acc, 0, sizeof(*acc));
    if (nnodes == 0) return len;
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const uint32_t* nd = nodes + stack[--sp] * SSSG_NODE_STRIDE;
        float bmin[3] = { u2f(nd[0]), u2f(nd[1]), u2f(nd[2]) }, bmax[3] = { u2f(nd[4]), u2f(nd[5]), u2f(nd[6]) };
        if (point ? !point_box(p0, bmin, bmax) : !seg_box(p0, inv, bmin, bmax)) continue;
        if (nd[7] & SSSG_LEAF) {
            uint32_t first = nd[3], count = nd[7] & ~SSSG_LEAF;
            for (uint32_t i = first; i < first + count; i++) {
                const uint32_t* g = gs + i * SSSG_GAUSS_STRIDE;
                float mu[3] = { u2f(g[0]), u2f(g[1]), u2f(g[2]) };
                float W[9];
                for (int k = 0; k < 9; k++) W[k] = u2f(g[4 + k]);
                float c = point ? dmin * gauss_point(W, mu, p0) : gauss_chord(W, mu, p0, p1, D);
                if (!(c > 0.0f)) continue;
                if (cb) cb(user, i, c);
                if (acc) {
                    acc->c += c;
                    for (int k = 0; k < SSS_Q; k++) acc->q[k] += c * u2f(g[13 + k]);
                }
            }
        } else if (sp < 62) {
            stack[sp++] = nd[3];
            stack[sp++] = nd[7];
        }
    }
    return len;
}

// distribution of log(excess + eps) for a chord (mirrors the shader)
static void blob_length_params(const uint32_t* blob, const float* p0, const float* p1, LengthModel* lm) {
    CloudAcc acc;
    float len = blob_chord(blob, p0, p1, NULL, NULL, &acc);
    float mfp = u2f(blob[SSSG_H_MFP]);
    float cbg = u2f(blob[SSSG_H_BGW]) * len;
    float d[3];
    sub3(p1, p0, d);
    float u = logf(1.0f + len3(d) / mfp);
    for (int k = 0; k < SSS_Q; k++) acc.q[k] += cbg * u2f(blob[SSSG_H_BG + k]);
    model_from_sums(acc.c + cbg, acc.q, u, lm);
}

// ================================================================================================================
// phase: internal cloud construction + chord features
// ================================================================================================================

static void make_whitening(Gauss* g) {
    // W = S^-1 R^T, column major: W[col*3 + row] = R[row*3 ... ]; R column major (R[c*3 + r] = axis c component r)
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++)
            g->W[col * 3 + row] = g->R[row * 3 + col] / g->s[row];
}

typedef struct { SSSFitter* f; uint32_t* idx; float* val; uint32_t n, cap; } FeatCtx;

static void feat_visit(void* user, uint32_t gi, float c) {
    FeatCtx* x = (FeatCtx*)user;
    if (x->n == x->cap) return;
    x->idx[x->n] = gi;
    x->val[x->n] = c;
    x->n++;
}

static void pack_blob(SSSFitter* f); // forward

static void phase_cloud(SSSFitter* f, double budget, SSSClockFn clock) {
    double start = clock();
    if (!f->gs) {
        uint32_t n = f->ninterior;
        if (!f->cl_centres) {
            uint32_t ngw = f->cfg.internal_gaussians;
            if (ngw > n / 4) ngw = n / 4;
            if (ngw < 1) ngw = 1;
            f->ng = ngw;
            f->cl_centres = SSS_ALLOC((size_t)ngw * 3, sizeof(float));
            f->cl_assign = SSS_ALLOC(n ? n : 1, sizeof(uint32_t));
            f->cl_acc = SSS_ALLOC((size_t)ngw * 4, sizeof(double));
            for (uint32_t c = 0; c < ngw; c++) memcpy(&f->cl_centres[c * 3], &f->interior[(n ? (c * (n / ngw)) % n : 0) * 3], 12);
            // spacing estimate from the bounding volume of the seeds
            float vb0[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, vb1[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
            for (uint32_t i = 0; i < n; i++) for (int k = 0; k < 3; k++) {
                vb0[k] = fminf(vb0[k], f->interior[i * 3 + k]);
                vb1[k] = fmaxf(vb1[k], f->interior[i * 3 + k]);
            }
            double vol = 1.0;
            for (int k = 0; k < 3; k++) vol *= fmax(vb1[k] - vb0[k], 1e-6 * f->diag);
            f->cl_spacing = (float)cbrt(vol / ngw);
            f->cl_iter = 0;
        }
        uint32_t ng = f->ng;
        float* centres = f->cl_centres;
        uint32_t* assign = f->cl_assign;
        double* acc = f->cl_acc;
        float spacing = f->cl_spacing;
        // k-means over the scattering locations, one iteration per time slice
        while (f->cl_iter < 10 && n > 0) {
            Grid grid;
            grid_build(&grid, centres, ng, spacing);
            for (uint32_t i = 0; i < n; i++) {
                uint32_t id = 0;
                float d2;
                grid_knn(&grid, &f->interior[i * 3], 1, &id, &d2);
                assign[i] = id;
            }
            grid_free(&grid);
            memset(acc, 0, (size_t)ng * 4 * sizeof(double));
            for (uint32_t i = 0; i < n; i++) {
                double* a = &acc[assign[i] * 4];
                for (int k = 0; k < 3; k++) a[k] += f->interior[i * 3 + k];
                a[3] += 1.0;
            }
            for (uint32_t c = 0; c < ng; c++) {
                if (acc[c * 4 + 3] < 0.5) { memcpy(&centres[c * 3], &f->interior[(rng_u32(&f->rng) % n) * 3], 12); continue; }
                for (int k = 0; k < 3; k++) centres[c * 3 + k] = (float)(acc[c * 4 + k] / acc[c * 4 + 3]);
            }
            f->cl_iter++;
            set_status(f, "Clustering the internal cloud %u/10", f->cl_iter);
            if (clock() - start >= budget) return;
        }
        f->gs = SSS_ALLOC(ng, sizeof(Gauss));
        f->cl_centres = NULL;
        f->cl_assign = NULL;
        f->cl_acc = NULL;
        // covariance of every cluster -> oriented gaussian
        double* cov = SSS_ALLOC((size_t)ng * 9, sizeof(double));
        memset(acc, 0, (size_t)ng * 4 * sizeof(double));
        for (uint32_t i = 0; i < n; i++) {
            uint32_t c = assign[i];
            float d[3];
            sub3(&f->interior[i * 3], &centres[c * 3], d);
            for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) cov[c * 9 + a * 3 + b] += d[a] * d[b];
            acc[c * 4 + 3] += 1.0;
        }
        Grid cgrid;
        grid_build(&cgrid, centres, ng, spacing);
        for (uint32_t c = 0; c < ng; c++) {
            Gauss* g = &f->gs[c];
            memcpy(g->mu, &centres[c * 3], 12);
            uint32_t ids[2];
            float d2[2];
            uint32_t k = grid_knn(&cgrid, g->mu, 2, ids, d2);
            float nn = k > 1 ? sqrtf(d2[1]) : spacing;
            float iso = fmaxf(0.75f * nn, 0.05f * f->mfp);
            double cnt = acc[c * 4 + 3];
            if (cnt < 6) {
                memset(g->R, 0, sizeof(g->R));
                g->R[0] = g->R[4] = g->R[8] = 1.0f;
                g->s[0] = g->s[1] = g->s[2] = iso;
            } else {
                double a[3][3], ev[3], V[3][3];
                for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) a[i][j] = cov[c * 9 + i * 3 + j] / cnt;
                jacobi3(a, ev, V);
                float smax = 0;
                for (int i = 0; i < 3; i++) { g->s[i] = 1.4f * (float)sqrt(fmax(ev[i], 0.0)); smax = fmaxf(smax, g->s[i]); }
                for (int i = 0; i < 3; i++) g->s[i] = fmaxf(g->s[i], fmaxf(0.3f * smax, 0.05f * f->mfp));
                for (int col = 0; col < 3; col++) for (int row = 0; row < 3; row++) g->R[col * 3 + row] = (float)V[row][col];
            }
            make_whitening(g);
        }
        grid_free(&cgrid);
        SSS_FREE(cov);
        SSS_FREE(acc);
        SSS_FREE(assign);
        SSS_FREE(centres);
        // acceleration structure over the 3 sigma boxes, gaussians reordered into leaf order
        float* mins = SSS_ALLOC((size_t)ng * 3, sizeof(float));
        float* maxs = SSS_ALLOC((size_t)ng * 3, sizeof(float));
        for (uint32_t c = 0; c < ng; c++) {
            Gauss* g = &f->gs[c];
            for (int row = 0; row < 3; row++) {
                double v = 0; // Sigma_rr = sum_k (R_rk s_k)^2
                for (int k = 0; k < 3; k++) { double e = g->R[k * 3 + row] * g->s[k]; v += e * e; }
                float ext = 3.0f * (float)sqrt(v);
                mins[c * 3 + row] = g->mu[row] - ext;
                maxs[c * 3 + row] = g->mu[row] + ext;
            }
        }
        uint32_t* order = SSS_ALLOC(ng, sizeof(uint32_t));
        f->gnodes = build_bvh(ng, mins, maxs, 4, order, &f->ngnodes);
        Gauss* sorted = SSS_ALLOC(ng, sizeof(Gauss));
        for (uint32_t i = 0; i < ng; i++) sorted[i] = f->gs[order[i]];
        SSS_FREE(f->gs);
        f->gs = sorted;
        SSS_FREE(order);
        SSS_FREE(mins);
        SSS_FREE(maxs);
        // starting model: one global regression of the log excess length on log chord length
        f->nfeat = f->holdout;
        f->fstart = SSS_ALLOC(f->nfeat + 1, sizeof(uint32_t));
        f->fbg = SSS_ALLOC(f->nfeat, sizeof(float));
        f->fy = SSS_ALLOC(f->nfeat, sizeof(float));
        f->fu = SSS_ALLOC(f->nfeat, sizeof(float));
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        float epsl = SSS_EPS_FRACTION * f->mfp;
        for (uint32_t i = 0; i < f->nfeat; i++) {
            const WalkRec* w = &f->rec[i];
            float d[3];
            sub3(w->out, w->in, d);
            float D = len3(d);
            f->fy[i] = logf(fmaxf(w->length - D, 0.0f) + epsl);
            f->fu[i] = logf(1.0f + D / f->mfp);
            sx += f->fu[i]; sy += f->fy[i]; sxx += f->fu[i] * f->fu[i]; sxy += f->fu[i] * f->fy[i];
        }
        double nn = f->nfeat > 0 ? f->nfeat : 1;
        double den = sxx - sx * sx / nn;
        double b1 = den > 1e-12 ? (sxy - sx * sy / nn) / den : 0.0;
        double b0 = (sy - b1 * sx) / nn;
        double ss = 0;
        for (uint32_t i = 0; i < f->nfeat; i++) { double r = f->fy[i] - (b0 + b1 * f->fu[i]); ss += r * r; }
        double sd = sqrt(ss / nn);
        // two components straddling the global fit; the optimiser separates short and long walks
        f->init_q[0] = (float)(b0 - 0.6 * sd);
        f->init_q[1] = (float)b1;
        f->init_q[2] = (float)log(fmax(0.7 * sd, 1e-3));
        f->init_q[3] = (float)(b0 + 0.6 * sd);
        f->init_q[4] = (float)b1;
        f->init_q[5] = (float)log(fmax(0.7 * sd, 1e-3));
        f->init_q[6] = 0.0f;
        for (uint32_t c = 0; c < ng; c++) memcpy(f->gs[c].q, f->init_q, sizeof(f->init_q));
        memcpy(f->bg, f->init_q, sizeof(f->init_q));
        f->entrycap = f->nfeat * 8 + 1024;
        f->fidx = SSS_ALLOC(f->entrycap, sizeof(uint32_t));
        f->fval = SSS_ALLOC(f->entrycap, sizeof(float));
        pack_blob(f); // the traversal below reads the packed nodes / gaussians
        f->feat_cursor = 0;
    }
    uint32_t tmpcap = 4096;
    uint32_t tmpidx[4096];
    float tmpval[4096];
    float bgw = SSS_BG_WEIGHT;
    while (f->feat_cursor < f->nfeat) {
        for (int it = 0; it < 512 && f->feat_cursor < f->nfeat; it++) {
            uint32_t i = f->feat_cursor;
            const WalkRec* w = &f->rec[i];
            FeatCtx ctx = { f, tmpidx, tmpval, 0, tmpcap };
            float len = blob_chord(f->blob, w->in, w->out, feat_visit, &ctx, NULL);
            f->fbg[i] = bgw * len;
            if (f->nentries + ctx.n > f->entrycap) {
                uint32_t newcap = (f->nentries + ctx.n) * 2;
                f->fidx = SSS_REALLOC(f->fidx, newcap, sizeof(uint32_t));
                f->fval = SSS_REALLOC(f->fval, newcap, sizeof(float));
                f->entrycap = newcap;
            }
            memcpy(&f->fidx[f->nentries], tmpidx, ctx.n * sizeof(uint32_t));
            memcpy(&f->fval[f->nentries], tmpval, ctx.n * sizeof(float));
            f->fstart[i] = f->nentries;
            f->nentries += ctx.n;
            f->fstart[i + 1] = f->nentries;
            f->feat_cursor++;
        }
        set_status(f, "Integrating chords through the internal cloud %u/%u", f->feat_cursor, f->nfeat);
        if (clock() - start >= budget) return;
    }
    uint32_t P = (f->ng + 1) * SSS_Q;
    f->adam = SSS_ALLOC(P, sizeof(AdamSlot));
    f->grad = SSS_ALLOC(P, sizeof(float));
    f->epoch = 0;
    f->phase = SSSFIT_OPTIMISE;
    set_status(f, "Optimising path length field");
}

// ================================================================================================================
// phase: optimise the path length field (full batch Adam on the log normal negative log likelihood)
// ================================================================================================================

static double evaluate_nll(SSSFitter* f, float* grad, double* r2) {
    uint32_t ng = f->ng;
    uint32_t P = (ng + 1) * SSS_Q;
    if (grad) memset(grad, 0, (size_t)P * sizeof(float));
    double nll = 0, sres = 0, stot = 0, ymean = 0;
    for (uint32_t i = 0; i < f->nfeat; i++) ymean += f->fy[i];
    ymean /= (f->nfeat ? f->nfeat : 1);
    const float LOG_SQRT_2PI = 0.91893853f;
    for (uint32_t i = 0; i < f->nfeat; i++) {
        float C = f->fbg[i], Q[SSS_Q];
        for (int k = 0; k < SSS_Q; k++) Q[k] = f->fbg[i] * f->bg[k];
        for (uint32_t e = f->fstart[i]; e < f->fstart[i + 1]; e++) {
            const Gauss* g = &f->gs[f->fidx[e]];
            float c = f->fval[e];
            C += c;
            for (int k = 0; k < SSS_Q; k++) Q[k] += c * g->q[k];
        }
        float u = f->fu[i], y = f->fy[i];
        LengthModel lm;
        model_from_sums(C, Q, u, &lm);
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
        // derivatives of the nll with respect to the blended quantities
        float dm1 = -r1 * z1 / lm.s1, dm2 = -r2 * z2 / lm.s2;
        float dls1 = (ls1 > -6.0f && ls1 < 4.0f) ? r1 * (1.0f - z1 * z1) : 0.0f;
        float dls2 = (ls2 > -6.0f && ls2 < 4.0f) ? r2 * (1.0f - z2 * z2) : 0.0f;
        float dlg = (lg > -20.0f && lg < 20.0f) ? lm.w1 - r1 : 0.0f;
        float dq[SSS_Q] = { dm1, dm1 * u, dls1, dm2, dm2 * u, dls2, dlg };
        for (uint32_t e = f->fstart[i]; e < f->fstart[i + 1]; e++) {
            float w = f->fval[e] * invC;
            float* gg = &grad[f->fidx[e] * SSS_Q];
            for (int k = 0; k < SSS_Q; k++) gg[k] += dq[k] * w;
        }
        float wb = f->fbg[i] * invC;
        float* gb = &grad[ng * SSS_Q];
        for (int k = 0; k < SSS_Q; k++) gb[k] += dq[k] * wb;
    }
    double n = f->nfeat ? f->nfeat : 1;
    if (grad) for (uint32_t p = 0; p < P; p++) grad[p] /= (float)n;
    if (r2) *r2 = stot > 0 ? 1.0 - sres / stot : 0.0;
    return nll / n;
}

static void phase_optimise(SSSFitter* f, double budget, SSSClockFn clock) {
    double start = clock();
    if (f->epoch == 0) f->stats.nll_initial = evaluate_nll(f, NULL, NULL);
    const float lr = 0.03f, b1 = 0.9f, b2 = 0.999f, reg = 2e-4f;
    while (f->epoch < f->cfg.epochs) {
        evaluate_nll(f, f->grad, NULL);
        f->epoch++;
        float bc1 = 1.0f - powf(b1, (float)f->epoch), bc2 = 1.0f - powf(b2, (float)f->epoch);
        for (uint32_t p = 0; p < (f->ng + 1) * SSS_Q; p++) {
            uint32_t gi = p / SSS_Q, k = p % SSS_Q;
            float* param = gi < f->ng ? &f->gs[gi].q[k] : &f->bg[k];
            float init = f->init_q[k];
            float gr = f->grad[p] + reg * (*param - init); // weak pull towards the global fit for sparse gaussians
            AdamSlot* a = &f->adam[p];
            a->m = b1 * a->m + (1 - b1) * gr;
            a->v = b2 * a->v + (1 - b2) * gr * gr;
            *param -= lr * (a->m / bc1) / (sqrtf(a->v / bc2) + 1e-8f);
        }
        if (f->epoch % 10 == 0) set_status(f, "Optimising path length field %u/%u", f->epoch, f->cfg.epochs);
        if (clock() - start >= budget) return;
    }
    f->stats.nll_final = evaluate_nll(f, NULL, &f->stats.r2_mean);
    pack_blob(f);
    f->val_count = f->cfg.validation;
    uint32_t held = f->nrec - f->holdout;
    if (f->val_count > held) f->val_count = held;
    f->val_cursor = 0;
    if (f->val_count > 0) {
        f->val_model_d = SSS_ALLOC(f->val_count, sizeof(float));
        f->val_model_l = SSS_ALLOC(f->val_count, sizeof(float));
        f->val_walk_d = SSS_ALLOC(f->val_count, sizeof(float));
        f->val_walk_l = SSS_ALLOC(f->val_count, sizeof(float));
        f->phase = SSSFIT_VALIDATE;
        set_status(f, "Validating against held out walks");
    } else {
        f->phase = SSSFIT_DONE;
        set_status(f, "Done");
    }
}

// ================================================================================================================
// packing
// ================================================================================================================

static void pack_blob(SSSFitter* f) {
    uint32_t table = SSSG_HEADER_WORDS;
    uint32_t surf = table + f->nt * SSSG_TABLE_STRIDE;
    uint32_t lobe = surf + f->ns * SSSG_SURF_STRIDE;
    uint32_t gauss = lobe + f->nlobes * SSSG_LOBE_STRIDE;
    uint32_t node = gauss + f->ng * SSSG_GAUSS_STRIDE;
    uint32_t total = node + f->ngnodes * SSSG_NODE_STRIDE;
    if (!f->blob || f->blob_words != total) {
        SSS_FREE(f->blob);
        f->blob = SSS_ALLOC(total, sizeof(uint32_t));
        f->blob_words = total;
    }
    uint32_t* b = f->blob;
    memset(b, 0, (size_t)total * 4);
    b[SSSG_H_MAGIC] = SSSG_MAGIC;
    b[SSSG_H_VERSION] = SSSG_VERSION;
    b[SSSG_H_TCOUNT] = f->nt;
    b[SSSG_H_TABLE] = table;
    b[SSSG_H_SURF] = surf; b[SSSG_H_NSURF] = f->ns;
    b[SSSG_H_LOBE] = lobe; b[SSSG_H_NLOBE] = f->nlobes;
    b[SSSG_H_GAUSS] = gauss; b[SSSG_H_NGAUSS] = f->ng;
    b[SSSG_H_NODE] = node; b[SSSG_H_NNODE] = f->ngnodes;
    b[SSSG_H_SIGMA] = f2u(f->sigma);
    b[SSSG_H_G] = f2u(f->g);
    b[SSSG_H_MFP] = f2u(f->mfp);
    b[SSSG_H_EPS] = f2u(SSS_EPS_FRACTION * f->mfp);
    for (int k = 0; k < SSS_Q; k++) b[SSSG_H_BG + k] = f2u(f->bg[k]);
    b[SSSG_H_BGW] = f2u(SSS_BG_WEIGHT);
    b[SSSG_H_EVK] = f2u(f->ev_k > 0.0f ? f->ev_k : 1.0f);
    b[SSSG_H_EVC] = f2u(f->ev_c);
    b[SSSG_H_DMIN] = f2u(SSS_DMIN_FRACTION * f->mfp);
    b[SSSG_H_TSTART] = SSSG_NONE;
    b[SSSG_H_MATERIAL] = SSSG_NONE;
    b[SSSG_H_SCALE] = f2u(1.0f);
    for (uint32_t t = 0; t < f->nt; t++) {
        uint32_t* r = b + table + t * SSSG_TABLE_STRIDE;
        r[0] = f->table[t * 3]; r[1] = f->table[t * 3 + 1]; r[2] = f->table[t * 3 + 2];
        r[3] = f->flip[t] ? SSSG_TRI_FLIP : 0u;
    }
    for (uint32_t c = 0; c < f->ns; c++) {
        uint32_t* r = b + surf + c * SSSG_SURF_STRIDE;
        for (int k = 0; k < 3; k++) {
            r[k] = f2u(f->sc[c * 3 + k]);
            r[4 + k] = f2u(f->snrm[c * 3 + k]);
            r[8 + k] = f2u(f->daxis ? f->daxis[c * 3 + k] : f->snrm[c * 3 + k]);
        }
        r[3] = f2u(f->st[c]);
        r[7] = f2u(f->sn[c]);
        r[11] = f2u(f->dexp ? f->dexp[c] : 1.0f);
        r[12] = f->lfirst ? f->lfirst[c] : 0;
        r[13] = f->lcount ? f->lcount[c] : 0;
        r[14] = f->nin ? f->nin[c] : 0;
        r[15] = f->nout ? f->nout[c] : 0;
        // cumulative lobe weights within this gaussian
        if (f->lcount) {
            float cum = 0;
            for (uint32_t j = 0; j < f->lcount[c]; j++) {
                const Lobe* l = &f->lobes[f->lfirst[c] + j];
                uint32_t* lr = b + lobe + (f->lfirst[c] + j) * SSSG_LOBE_STRIDE;
                cum += l->weight;
                for (int k = 0; k < 3; k++) { lr[k] = f2u(l->mean[k]); lr[13 + k] = f2u(l->axis[k]); }
                lr[3] = f2u(j + 1 == f->lcount[c] ? 1.0f : cum);
                for (int k = 0; k < 9; k++) lr[4 + k] = f2u(l->L[k]);
            }
        }
    }
    for (uint32_t c = 0; c < f->ng; c++) {
        uint32_t* r = b + gauss + c * SSSG_GAUSS_STRIDE;
        const Gauss* g = &f->gs[c];
        for (int k = 0; k < 3; k++) r[k] = f2u(g->mu[k]);
        for (int k = 0; k < 9; k++) r[4 + k] = f2u(g->W[k]);
        for (int k = 0; k < SSS_Q; k++) r[13 + k] = f2u(g->q[k]);
    }
    for (uint32_t n = 0; n < f->ngnodes; n++) {
        uint32_t* r = b + node + n * SSSG_NODE_STRIDE;
        const TriNode* nd = &f->gnodes[n];
        for (int k = 0; k < 3; k++) { r[k] = f2u(nd->bmin[k]); r[4 + k] = f2u(nd->bmax[k]); }
        if (nd->count) { r[3] = nd->left; r[7] = SSSG_LEAF | nd->count; }
        else { r[3] = nd->left; r[7] = nd->right; }
    }
}

int SSSBlobGaussian(const uint32_t* blob, uint32_t i, float* mu, float* axes, float* scales, float* log_excess) {
    if (i >= blob[SSSG_H_NGAUSS]) return 0;
    const uint32_t* g = blob + blob[SSSG_H_GAUSS] + i * SSSG_GAUSS_STRIDE;
    float W[9];
    for (int k = 0; k < 9; k++) W[k] = u2f(g[4 + k]);
    for (int k = 0; k < 3; k++) mu[k] = u2f(g[k]);
    // W^T W = R S^-2 R^T
    double a[3][3], ev[3], V[3][3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            double acc = 0;
            for (int k = 0; k < 3; k++) acc += (double)W[r * 3 + k] * W[c * 3 + k]; // (W^T W)_rc = sum_k W_kr W_kc
            a[r][c] = acc;
        }
    jacobi3(a, ev, V);
    for (int c = 0; c < 3; c++) {
        scales[c] = (float)(1.0 / sqrt(fmax(ev[c], 1e-30)));
        for (int r = 0; r < 3; r++) axes[c * 3 + r] = (float)V[r][c];
    }
    float c2[3];
    cross3(&axes[0], &axes[3], c2);
    if (dot3(c2, &axes[6]) < 0.0f) for (int r = 0; r < 3; r++) axes[6 + r] = -axes[6 + r];
    float q[SSS_Q];
    for (int k = 0; k < SSS_Q; k++) q[k] = u2f(g[13 + k]);
    LengthModel lm;
    model_from_sums(1.0f, q, logf(2.0f), &lm);
    *log_excess = lm.w1 * lm.m1 + (1.0f - lm.w1) * lm.m2;
    return 1;
}

uint32_t SSSBlobWords(const uint32_t* b, uint32_t available) {
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

// ================================================================================================================
// reference sampler (mirror of sss_splat_scatter in path.comp)
// ================================================================================================================

static uint32_t blob_pick_surface(const uint32_t* blob, uint32_t tri, const float* p, float r) {
    const uint32_t* row = blob + blob[SSSG_H_TABLE] + tri * SSSG_TABLE_STRIDE;
    float w[3] = { 0, 0, 0 }, total = 0;
    for (int k = 0; k < 3; k++) {
        if (row[k] == SSSG_NONE) continue;
        const uint32_t* s = blob + blob[SSSG_H_SURF] + row[k] * SSSG_SURF_STRIDE;
        float c[3] = { u2f(s[0]), u2f(s[1]), u2f(s[2]) }, n[3] = { u2f(s[4]), u2f(s[5]), u2f(s[6]) };
        w[k] = surface_weight(c, n, u2f(s[3]), u2f(s[7]), p);
        total += w[k];
    }
    if (!(total > 1e-30f)) return row[0];
    float x = r * total;
    for (int k = 0; k < 3; k++) {
        if (row[k] == SSSG_NONE) continue;
        if (x <= w[k]) return row[k];
        x -= w[k];
    }
    return row[0];
}

static void blob_tri_normal(const uint32_t* blob, const SSSCaster* cs, uint32_t tri, float* n) {
    const uint32_t* id = &cs->indices[tri * 3];
    float e1[3], e2[3];
    sub3(&cs->positions[id[1] * 3], &cs->positions[id[0] * 3], e1);
    sub3(&cs->positions[id[2] * 3], &cs->positions[id[0] * 3], e2);
    cross3(e1, e2, n);
    norm3(n);
    if (blob[blob[SSSG_H_TABLE] + tri * SSSG_TABLE_STRIDE + 3] & SSSG_TRI_FLIP) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
}

void sssfit_sample(const uint32_t* blob, const SSSCaster* cs, const float* p, uint32_t tri,
    SSSRandomFn rnd, void* st, SSSSample* out) {
    memset(out, 0, sizeof(*out));
    float mfp = u2f(blob[SSSG_H_MFP]);
    // 1. surface gaussian under the entry point
    uint32_t sgi = blob_pick_surface(blob, tri, p, rnd(st));
    const uint32_t* sg = blob + blob[SSSG_H_SURF] + sgi * SSSG_SURF_STRIDE;
    // 2. + 3. sample an exit lobe and project the sampled point onto the surface along the lobe axis.
    //         A sample whose projection misses the mesh is redrawn (up to SSS_PROJECTION_TRIES times).
    float x[3];
    uint32_t xt = tri;
    int projected = 0;
    for (int attempt = 0; attempt < SSS_PROJECTION_TRIES && !projected; attempt++) {
        float u = rnd(st);
        uint32_t li = sg[12];
        for (uint32_t j = 0; j < sg[13]; j++) {
            li = sg[12] + j;
            if (u <= u2f(blob[blob[SSSG_H_LOBE] + li * SSSG_LOBE_STRIDE + 3])) break;
        }
        const uint32_t* lb = blob + blob[SSSG_H_LOBE] + li * SSSG_LOBE_STRIDE;
        float L[9];
        for (int k = 0; k < 9; k++) L[k] = u2f(lb[4 + k]);
        float u1 = fmaxf(rnd(st), 1e-7f), u2 = rnd(st), u3 = fmaxf(rnd(st), 1e-7f), u4 = rnd(st);
        float r1 = sqrtf(-2.0f * logf(u1)), r2 = sqrtf(-2.0f * logf(u3));
        float z[3] = { r1 * cosf(2.0f * (float)SSS_PI * u2), r1 * sinf(2.0f * (float)SSS_PI * u2), r2 * cosf(2.0f * (float)SSS_PI * u4) };
        float q[3], a[3] = { u2f(lb[13]), u2f(lb[14]), u2f(lb[15]) };
        for (int k = 0; k < 3; k++) q[k] = p[k] + u2f(lb[k]) + L[k] * z[0] + L[3 + k] * z[1] + L[6 + k] * z[2];
        // extent of the lobe along its projection axis: |L^T a|
        float lta[3] = { L[0] * a[0] + L[1] * a[1] + L[2] * a[2], L[3] * a[0] + L[4] * a[1] + L[5] * a[2], L[6] * a[0] + L[7] * a[1] + L[8] * a[2] };
        float H = 3.0f * len3(lta) + 0.5f * mfp;
        float o[3] = { q[0] + a[0] * H, q[1] + a[1] * H, q[2] + a[2] * H }, d[3] = { -a[0], -a[1], -a[2] };
        uint32_t ht;
        float t = cs->trace(cs->user, o, d, 2.0f * H, &ht);
        if (t >= 0.0f) {
            for (int k = 0; k < 3; k++) x[k] = o[k] + d[k] * t;
            xt = ht;
            projected = 1;
        } else {
            float best = FLT_MAX;
            for (int sgn = 0; sgn < 2; sgn++) {
                float dd[3] = { sgn ? -a[0] : a[0], sgn ? -a[1] : a[1], sgn ? -a[2] : a[2] };
                float t2 = cs->trace(cs->user, q, dd, H, &ht);
                if (t2 >= 0.0f && t2 < best) {
                    best = t2;
                    for (int k = 0; k < 3; k++) x[k] = q[k] + dd[k] * t2;
                    xt = ht;
                    projected = 1;
                }
            }
        }
    }
    if (!projected) { memcpy(x, p, 12); xt = tri; }
    // 4. path length from the internal cloud along the chord
    LengthModel lm;
    blob_length_params(blob, p, x, &lm);
    float pick = rnd(st);
    float zz = pick < lm.w1 ? lm.m1 + lm.s1 * normal_quantile(rnd(st)) : lm.m2 + lm.s2 * normal_quantile(rnd(st));
    float d3[3];
    sub3(x, p, d3);
    float D = len3(d3);
    float excess = fmaxf(expf(fminf(zz, logf(SSS_MAX_EXCESS_MFP * mfp))) - u2f(blob[SSSG_H_EPS]), 0.0f);
    // 5. exit direction from the surface gaussian under the exit point
    float n[3];
    blob_tri_normal(blob, cs, xt, n);
    uint32_t so = blob_pick_surface(blob, xt, x, rnd(st));
    const uint32_t* sgo = blob + blob[SSSG_H_SURF] + so * SSSG_SURF_STRIDE;
    float axis[3] = { u2f(sgo[8]), u2f(sgo[9]), u2f(sgo[10]) };
    float e = u2f(sgo[11]);
    float xi1 = rnd(st), xi2 = rnd(st);
    float ct = powf(fmaxf(xi1, 1e-7f), 1.0f / (e + 1.0f));
    float stt = sqrtf(fmaxf(0.0f, 1.0f - ct * ct));
    float phi = 2.0f * (float)SSS_PI * xi2;
    float local[3] = { stt * cosf(phi), stt * sinf(phi), ct };
    float w[3];
    onb3(axis, local, w);
    float wn = dot3(w, n);
    if (wn < 0.0f) for (int k = 0; k < 3; k++) w[k] -= 2.0f * wn * n[k];
    memcpy(out->exit, x, 12);
    out->exit_tri = xt;
    memcpy(out->normal, n, 12);
    memcpy(out->dir, w, 12);
    out->chord = D;
    out->length = D + excess;
    out->projected = projected;
}

// ================================================================================================================
// phase: validation against held out walks
// ================================================================================================================

static int cmpf(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static void quantiles(float* v, uint32_t n, double* q) {
    qsort(v, n, sizeof(float), cmpf);
    const double ps[4] = { 0.25, 0.5, 0.75, 0.9 };
    for (int i = 0; i < 4; i++) q[i] = n ? v[(uint32_t)(ps[i] * (n - 1))] : 0.0;
}

static void phase_validate(SSSFitter* f, double budget, SSSClockFn clock) {
    double start = clock();
    SSSCaster cs = { caster_trace, &f->bvh, f->pos, f->idx };
    while (f->val_cursor < f->val_count) {
        for (int it = 0; it < 256 && f->val_cursor < f->val_count; it++) {
            uint32_t i = f->val_cursor;
            const WalkRec* w = &f->rec[f->holdout + i];
            SSSSample s;
            sssfit_sample(f->blob, &cs, w->in, w->tin, rng_cb, &f->rng, &s);
            float d[3];
            sub3(w->out, w->in, d);
            f->val_walk_d[i] = len3(d) / f->mfp;
            f->val_walk_l[i] = w->length / f->mfp;
            f->val_model_d[i] = s.chord / f->mfp;
            f->val_model_l[i] = s.length / f->mfp;
            if (!s.projected) f->stats.projection_failures++;
            for (int k = 0; k < 3; k++) {
                double kk = (k == 0 ? 0.01 : (k == 1 ? 0.1 : 1.0));
                f->stats.walk_atten[k] += exp(-kk * f->val_walk_l[i]);
                f->stats.model_atten[k] += exp(-kk * f->val_model_l[i]);
            }
            f->val_cos_walk += dot3(w->dir, &f->tn[w->tout * 3]);
            f->val_cos_model += dot3(s.dir, s.normal);
            f->val_cursor++;
        }
        set_status(f, "Validating against held out walks %u/%u", f->val_cursor, f->val_count);
        if (clock() - start >= budget) return;
    }
    uint32_t n = f->val_count;
    f->stats.validation_samples = n;
    for (int k = 0; k < 3; k++) { f->stats.walk_atten[k] /= n; f->stats.model_atten[k] /= n; }
    f->stats.walk_cos = f->val_cos_walk / n;
    f->stats.model_cos = f->val_cos_model / n;
    quantiles(f->val_walk_d, n, f->stats.walk_dist_q);
    quantiles(f->val_model_d, n, f->stats.model_dist_q);
    quantiles(f->val_walk_l, n, f->stats.walk_len_q);
    quantiles(f->val_model_l, n, f->stats.model_len_q);
    f->phase = SSSFIT_DONE;
    set_status(f, "Done");
}

// ================================================================================================================
// driver
// ================================================================================================================

SSSFitPhase SSSFitStep(SSSFitter* f, double budget, SSSClockFn clock) {
    if (!f) return SSSFIT_FAILED;
    double start = clock();
    while (f->phase != SSSFIT_DONE && f->phase != SSSFIT_FAILED) {
        double left = budget - (clock() - start);
        if (left <= 0.0) break;
        switch (f->phase) {
            case SSSFIT_SETUP: phase_setup(f); break;
            case SSSFIT_SURFACE: phase_surface(f); break;
            case SSSFIT_WALKS: phase_walks(f, left, clock); break;
            case SSSFIT_LOBES: phase_lobes(f, left, clock); break;
            case SSSFIT_CLOUD: phase_cloud(f, left, clock); break;
            case SSSFIT_OPTIMISE: phase_optimise(f, left, clock); break;
            case SSSFIT_VALIDATE: phase_validate(f, left, clock); break;
            default: break;
        }
    }
    return f->phase;
}
