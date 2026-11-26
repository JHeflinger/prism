#include "rig.h"
#include "renderer/animation.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include "renderer/overlay.h"
#include <util/logger.h>

#define WEIGHT_EPSILON 1e-4f
#define HEAT_MAX_ITERATIONS 200
#define HEAT_TOLERANCE 1e-3
#define HEAT_MAX_LEVELS 8
#define HEAT_COARSE_TARGET 3000

typedef struct {
    size_t mesh;
    size_t vstart;
    size_t vcount;
    size_t tcount;
    size_t nnodes;
    uint32_t* v2n;
    uint32_t* nodeptr;
    uint32_t* nodeverts;
    uint32_t* adjptr;
    uint32_t* adj;
} WeldGraph;

typedef struct {
    int64_t* keys;
    uint32_t* values;
    size_t capacity;
} PointHash;

typedef struct {
    size_t n;
    uint32_t* rowptr;
    uint32_t* cols;
    double* vals;
    double* diag;
    double* mh;
    uint32_t* parent;
    double *x, *r, *z, *p, *ap, *rhs;
    double *vx, *vb, *vr;
} HeatLevel;

typedef struct {
    BOOL running;
    size_t anim;
    size_t mesh;
    AutoWeightMethod method;
    BOOL selectedonly;
    size_t nnodes;
    size_t nbones;
    uint32_t* bones;
    BOOL* solve;
    uint32_t* nearest;
    float* result;
    HeatLevel levels[HEAT_MAX_LEVELS];
    size_t nlevels;
    BOOL started;
    size_t current;
    uint32_t iteration;
    double rz;
    double bnorm;
    size_t solved;
    size_t tosolve;
} HeatJob;

typedef struct {
    uint32_t a;
    uint32_t b;
    double w;
} CoarseEdge;

static size_t g_mesh = (size_t)-1;
static RigMode g_mode = RIG_MODE_POSE;
static EditHandle g_handle = EDIT_HANDLE_BONE;
static size_t g_active = (size_t)-1;
static uint32_t g_selected[MAX_BONES / 32] = { 0 };
static WeightBrush g_brush = { BRUSH_ADD, 0.15f, 0.5f, 1.0f, TRUE };
static BOOL g_hit_valid = FALSE;
static vec3 g_hit = { 0 };
static vec3 g_hit_normal = { 0 };
static mat4 g_rest[MAX_BONES];
static size_t g_rest_anim = (size_t)-1;
static BonePose g_clipboard[MAX_BONES];
static char g_clipboard_names[MAX_BONES][256];
static size_t g_clipboard_count = 0;
static const char* g_mode_labels[RIG_MODE_COUNT] = { "Edit Rig", "Pose", "Weights" };
static const char* g_handle_labels[EDIT_HANDLE_COUNT] = { "Whole Bone", "Head", "Tail" };
static const char* g_autoweight_labels[AUTOWEIGHT_COUNT] = { "Bone Heat", "Envelope" };
static const char* g_brush_labels[BRUSH_COUNT] = { "Add", "Subtract", "Replace", "Smooth" };
static WeldGraph g_graph = { (size_t)-1, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL };
static const vec4* g_sort_vertices = NULL;
static HeatJob g_job = { 0 };
static vec3* g_skinned = NULL;
static vec3* g_skinned_normals = NULL;
static size_t g_skinned_cap = 0;

__attribute__((noinline)) static void BlobCopy(void* dst, const void* src, size_t size) {
    memcpy(dst, src, size);
}

static void M4Copy(void* dst, const void* src) {
    BlobCopy(dst, src, sizeof(mat4));
}

static void M4Head(const mat4 m, vec3 out) {
    out[0] = m[3][0];
    out[1] = m[3][1];
    out[2] = m[3][2];
}

static void QuatFromTo(const vec3 a, const vec3 b, versor out) {
    vec3 na, nb, axis;
    glm_vec3_normalize_to((float*)a, na);
    glm_vec3_normalize_to((float*)b, nb);
    float d = glm_vec3_dot(na, nb);
    if (d > 1.0f - 1e-7f) { glm_quat_identity(out); return; }
    if (d < -1.0f + 1e-7f) {
        vec3 ref = { 0, 0, 1 };
        if (fabsf(na[2]) > 0.9f) SETVEC3(ref, 1, 0, 0);
        glm_vec3_cross(na, ref, axis);
        glm_vec3_normalize(axis);
        glm_quatv(out, GLM_PIf, axis);
        return;
    }
    glm_vec3_cross(na, nb, axis);
    out[0] = axis[0];
    out[1] = axis[1];
    out[2] = axis[2];
    out[3] = 1.0f + d;
    glm_quat_normalize(out);
}

static float SignedAngle(const vec3 a, const vec3 b, const vec3 axis) {
    vec3 c;
    glm_vec3_cross((float*)a, (float*)b, c);
    return atan2f(glm_vec3_dot(c, (float*)axis), glm_vec3_dot((float*)a, (float*)b));
}

static float PointSegmentDistance(const vec3 p, const vec3 a, const vec3 b) {
    vec3 ab, ap, closest;
    glm_vec3_sub((float*)b, (float*)a, ab);
    glm_vec3_sub((float*)p, (float*)a, ap);
    float len2 = glm_vec3_dot(ab, ab);
    float t = len2 > 1e-20f ? glm_clamp(glm_vec3_dot(ap, ab) / len2, 0.0f, 1.0f) : 0.0f;
    glm_vec3_scale(ab, t, closest);
    glm_vec3_add((float*)a, closest, closest);
    return glm_vec3_distance((float*)p, closest);
}

static BOOL SelectedBit(size_t b) {
    return b < MAX_BONES && ((g_selected[b / 32] >> (b % 32)) & 1u);
}

static void SetSelectedBit(size_t b, BOOL on) {
    if (b >= MAX_BONES) return;
    if (on) g_selected[b / 32] |= 1u << (b % 32);
    else g_selected[b / 32] &= ~(1u << (b % 32));
}

static MeshDescriptor* ActiveDescriptor() {
    if (g_mesh >= NumMeshes()) return NULL;
    return MeshReference(g_mesh);
}

static size_t MeshVertexCount(const MeshDescriptor* md) {
    if (md->end < md->start || md->end == (VertexID)-1) return 0;
    return (size_t)md->end - md->start + 1;
}

static size_t MeshTriangleCount(const MeshDescriptor* md) {
    if (md->tend < md->tstart || md->tend == (TriangleID)-1) return 0;
    return (size_t)md->tend - md->tstart + 1;
}

static void InvalidateRest() {
    g_rest_anim = (size_t)-1;
}

static mat4* RestGlobals() {
    size_t a = RigActiveAnimation();
    if (a == (size_t)-1) return g_rest;
    if (g_rest_anim != a) {
        SkeletonRestGlobals(&(AnimationReference(a)->skeleton), g_rest);
        g_rest_anim = a;
    }
    return g_rest;
}

static void Touch(MeshAnimation* anim) {
    anim->dirty = TRUE;
    InvalidateRest();
}

static size_t SkinBase() {
    MeshDescriptor* md = ActiveDescriptor();
    return md ? md->skinstart : 0;
}

static VertexSkin* SkinRef(size_t local) {
    return &(RendererGeometry()->skins.data[SkinBase() + local]);
}

static float FrameRoll(const mat4 frame) {
    vec3 dir = { frame[1][0], frame[1][1], frame[1][2] };
    vec3 x = { frame[0][0], frame[0][1], frame[0][2] };
    if (glm_vec3_norm(dir) < 1e-9f) return 0.0f;
    glm_vec3_normalize(dir);
    versor q0;
    vec3 y = { 0, 1, 0 };
    QuatFromTo(y, dir, q0);
    vec3 x0 = { 1, 0, 0 };
    glm_quat_rotatev(q0, x0, x0);
    vec3 px;
    glm_vec3_scale(dir, glm_vec3_dot(x, dir), px);
    glm_vec3_sub(x, px, x);
    if (glm_vec3_norm(x) < 1e-9f) return 0.0f;
    glm_vec3_normalize(x);
    return SignedAngle(x0, x, dir);
}

static void SetRestFrame(MeshAnimation* anim, size_t bone, mat4 frame, float length) {
    Skeleton* sk = &(anim->skeleton);
    static mat4 rest[MAX_BONES];
    SkeletonRestGlobals(sk, rest);
    mat4 parent, inv, local;
    if (sk->bones[bone].parent != (size_t)-1 && sk->bones[bone].parent < sk->bonecount) M4Copy(parent, rest[sk->bones[bone].parent]);
    else glm_mat4_identity(parent);
    glm_mat4_inv(parent, inv);
    glm_mat4_mul(inv, frame, local);
    mat4 finv;
    glm_mat4_inv(frame, finv);
    M4Copy(sk->bones[bone].localbind, local);
    M4Copy(sk->bones[bone].inversebind, finv);
    SETVEC3(sk->bones[bone].tail, 0.0f, length, 0.0f);
    sk->bones[bone].flags |= BONE_FLAG_INITIALIZED;
    vec3 newtail;
    vec3 t = { 0.0f, length, 0.0f };
    glm_mat4_mulv3(frame, t, 1.0f, newtail);
    for (size_t c = 0; c < sk->bonecount; c++) {
        if (c == bone || sk->bones[c].parent != bone) continue;
        mat4 child, cl;
        M4Copy(child, rest[c]);
        glm_mat4_mul(finv, child, cl);
        M4Copy(sk->bones[c].localbind, cl);
    }
    for (size_t c = 0; c < sk->bonecount; c++) {
        if (c == bone || sk->bones[c].parent != bone || !(sk->bones[c].flags & BONE_FLAG_CONNECTED)) continue;
        mat4 child;
        M4Copy(child, rest[c]);
        vec3 ctail, ct;
        glm_vec3_copy(sk->bones[c].tail, ct);
        glm_mat4_mulv3(child, ct, 1.0f, ctail);
        float roll = FrameRoll(child);
        mat4 cf;
        RigBoneFrame(newtail, ctail, roll, cf);
        SetRestFrame(anim, c, cf, glm_vec3_distance(newtail, ctail));
    }
    Touch(anim);
}

static void RemapSkinBone(VertexSkin* skin, uint32_t removed, uint32_t heir) {
    uint32_t idx[MAX_BONE_INFLUENCES + 1];
    float w[MAX_BONE_INFLUENCES + 1];
    size_t n = 0;
    for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) {
        if (!(skin->weights[k] > 0.0f)) continue;
        uint32_t i = skin->indices[k];
        if (i == removed) i = heir;
        else if (i > removed) i--;
        size_t j = 0;
        for (; j < n; j++) if (idx[j] == i) { w[j] += skin->weights[k]; break; }
        if (j == n) { idx[n] = i; w[n] = skin->weights[k]; n++; }
    }
    memset(skin, 0, sizeof(VertexSkin));
    float total = 0.0f;
    for (size_t j = 0; j < n && j < MAX_BONE_INFLUENCES; j++) {
        skin->indices[j] = idx[j];
        skin->weights[j] = w[j];
        total += w[j];
    }
    if (total > 0.0f) for (size_t j = 0; j < MAX_BONE_INFLUENCES; j++) skin->weights[j] /= total;
    else skin->weights[0] = 1.0f;
}

static BOOL ReplaceSuffix(const char* in, const char* a, const char* b, char out[256]) {
    size_t n = strlen(in), la = strlen(a);
    if (n <= la || strcmp(in + n - la, a) != 0) return FALSE;
    snprintf(out, 256, "%.*s%s", (int)(n - la), in, b);
    return TRUE;
}

static BOOL ReplacePrefix(const char* in, const char* a, const char* b, char out[256]) {
    size_t la = strlen(a);
    if (strncmp(in, a, la) != 0 || strlen(in) <= la) return FALSE;
    snprintf(out, 256, "%s%s", b, in + la);
    return TRUE;
}

static BOOL ReplaceInner(const char* in, const char* a, const char* b, char out[256]) {
    const char* at = strstr(in, a);
    if (!at) return FALSE;
    snprintf(out, 256, "%.*s%s%s", (int)(at - in), in, b, at + strlen(a));
    return TRUE;
}

static BOOL MirrorName(const char* in, char out[256]) {
    static const char* suffix[][2] = {
        { ".L", ".R" }, { "_L", "_R" }, { "-L", "-R" }, { ".l", ".r" }, { "_l", "_r" }, { "-l", "-r" },
        { "Left", "Right" }, { "left", "right" }, { "LEFT", "RIGHT" },
    };
    static const char* prefix[][2] = {
        { "L_", "R_" }, { "l_", "r_" }, { "L.", "R." }, { "Left", "Right" }, { "left", "right" },
    };
    for (size_t i = 0; i < sizeof(suffix) / sizeof(suffix[0]); i++) {
        if (ReplaceSuffix(in, suffix[i][0], suffix[i][1], out)) return TRUE;
        if (ReplaceSuffix(in, suffix[i][1], suffix[i][0], out)) return TRUE;
    }
    for (size_t i = 0; i < sizeof(prefix) / sizeof(prefix[0]); i++) {
        if (ReplacePrefix(in, prefix[i][0], prefix[i][1], out)) return TRUE;
        if (ReplacePrefix(in, prefix[i][1], prefix[i][0], out)) return TRUE;
    }
    if (ReplaceInner(in, "Left", "Right", out)) return TRUE;
    if (ReplaceInner(in, "Right", "Left", out)) return TRUE;
    return FALSE;
}

static size_t MirrorBone(const Skeleton* sk, size_t bone) {
    char name[256];
    if (!MirrorName(sk->bones[bone].name, name)) return bone;
    size_t m = SkeletonFindBone(sk, name);
    return m == (size_t)-1 ? bone : m;
}

static void PackSkin(VertexSkin* skin, const uint32_t* idx, const float* w, size_t n, uint32_t fallback) {
    uint32_t bi[MAX_BONE_INFLUENCES] = { 0 };
    float bw[MAX_BONE_INFLUENCES] = { 0 };
    for (size_t i = 0; i < n; i++) {
        if (!(w[i] > WEIGHT_EPSILON)) continue;
        size_t slot = MAX_BONE_INFLUENCES;
        for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) if (bw[k] > 0.0f && bi[k] == idx[i]) { slot = k; break; }
        if (slot < MAX_BONE_INFLUENCES) { bw[slot] += w[i]; continue; }
        size_t weakest = 0;
        for (size_t k = 1; k < MAX_BONE_INFLUENCES; k++) if (bw[k] < bw[weakest]) weakest = k;
        if (w[i] > bw[weakest]) { bw[weakest] = w[i]; bi[weakest] = idx[i]; }
    }
    float total = 0.0f;
    for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) total += bw[k];
    memset(skin, 0, sizeof(VertexSkin));
    if (!(total > 0.0f)) {
        skin->indices[0] = fallback;
        skin->weights[0] = 1.0f;
        return;
    }
    for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) {
        size_t best = k;
        for (size_t j = k + 1; j < MAX_BONE_INFLUENCES; j++) if (bw[j] > bw[best]) best = j;
        float tw = bw[k]; bw[k] = bw[best]; bw[best] = tw;
        uint32_t ti = bi[k]; bi[k] = bi[best]; bi[best] = ti;
        skin->indices[k] = bi[k];
        skin->weights[k] = bw[k] / total;
    }
}

static void SetSkinWeight(VertexSkin* skin, uint32_t bone, float value, uint32_t fallback) {
    value = glm_clamp(value, 0.0f, 1.0f);
    uint32_t idx[MAX_BONE_INFLUENCES + 2];
    float w[MAX_BONE_INFLUENCES + 2];
    size_t n = 0;
    float others = 0.0f;
    for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) {
        if (!(skin->weights[k] > 0.0f) || skin->indices[k] == bone) continue;
        idx[n] = skin->indices[k];
        w[n] = skin->weights[k];
        others += w[n];
        n++;
    }
    if (others > 0.0f) {
        for (size_t i = 0; i < n; i++) w[i] *= (1.0f - value) / others;
    } else if (value < 1.0f) {
        if (fallback == bone || fallback == (uint32_t)-1) value = 1.0f;
        else { idx[n] = fallback; w[n] = 1.0f - value; n++; }
    }
    idx[n] = bone;
    w[n] = value;
    n++;
    PackSkin(skin, idx, w, n, bone);
}

static uint32_t WeightFallback(MeshAnimation* anim, uint32_t bone) {
    size_t p = anim->skeleton.bones[bone].parent;
    if (p < anim->skeleton.bonecount) return (uint32_t)p;
    for (size_t b = 0; b < anim->skeleton.bonecount; b++) if (b != bone) return (uint32_t)b;
    return bone;
}

static int CompareVertexPositions(const void* a, const void* b) {
    uint32_t ia = *(const uint32_t*)a, ib = *(const uint32_t*)b;
    const float* pa = g_sort_vertices[ia];
    const float* pb = g_sort_vertices[ib];
    for (int c = 0; c < 3; c++) {
        if (pa[c] < pb[c]) return -1;
        if (pa[c] > pb[c]) return 1;
    }
    return ia < ib ? -1 : (ia > ib ? 1 : 0);
}

static void FreeGraph() {
    if (g_graph.v2n) EZ_FREE(g_graph.v2n);
    if (g_graph.nodeptr) EZ_FREE(g_graph.nodeptr);
    if (g_graph.nodeverts) EZ_FREE(g_graph.nodeverts);
    if (g_graph.adjptr) EZ_FREE(g_graph.adjptr);
    if (g_graph.adj) EZ_FREE(g_graph.adj);
    memset(&g_graph, 0, sizeof(g_graph));
    g_graph.mesh = (size_t)-1;
}

static int CompareU32(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static WeldGraph* Graph() {
    MeshDescriptor* md = ActiveDescriptor();
    if (!md) return NULL;
    size_t vcount = MeshVertexCount(md), tcount = MeshTriangleCount(md);
    if (vcount == 0) return NULL;
    if (g_graph.mesh == g_mesh && g_graph.vstart == md->start && g_graph.vcount == vcount && g_graph.tcount == tcount) return &g_graph;
    FreeGraph();
    Geometry* geo = RendererGeometry();
    g_graph.mesh = g_mesh;
    g_graph.vstart = md->start;
    g_graph.vcount = vcount;
    g_graph.tcount = tcount;
    uint32_t* sorted = EZ_ALLOC(vcount, sizeof(uint32_t));
    for (size_t i = 0; i < vcount; i++) sorted[i] = (uint32_t)i;
    g_sort_vertices = (const vec4*)&(geo->vertices.data[md->start]);
    qsort(sorted, vcount, sizeof(uint32_t), CompareVertexPositions);
    g_graph.v2n = EZ_ALLOC(vcount, sizeof(uint32_t));
    size_t nodes = 0;
    for (size_t i = 0; i < vcount; i++) {
        if (i > 0) {
            const float* a = g_sort_vertices[sorted[i - 1]];
            const float* b = g_sort_vertices[sorted[i]];
            if (!(a[0] == b[0] && a[1] == b[1] && a[2] == b[2])) nodes++;
        }
        g_graph.v2n[sorted[i]] = (uint32_t)nodes;
    }
    nodes++;
    g_graph.nnodes = nodes;
    g_graph.nodeptr = EZ_ALLOC(nodes + 1, sizeof(uint32_t));
    g_graph.nodeverts = EZ_ALLOC(vcount, sizeof(uint32_t));
    for (size_t v = 0; v < vcount; v++) g_graph.nodeptr[g_graph.v2n[v] + 1]++;
    for (size_t i = 0; i < nodes; i++) g_graph.nodeptr[i + 1] += g_graph.nodeptr[i];
    uint32_t* fill = EZ_ALLOC(nodes, sizeof(uint32_t));
    for (size_t v = 0; v < vcount; v++) {
        uint32_t n = g_graph.v2n[v];
        g_graph.nodeverts[g_graph.nodeptr[n] + fill[n]++] = (uint32_t)v;
    }
    uint32_t* deg = EZ_ALLOC(nodes + 1, sizeof(uint32_t));
    for (size_t t = 0; t < tcount; t++) {
        Triangle* tri = TriangleReference(md->tstart + t);
        uint32_t c[3] = { tri->a, tri->b, tri->c };
        for (int e = 0; e < 3; e++) {
            uint32_t a = c[e], b = c[(e + 1) % 3];
            if (a < md->start || b < md->start || a - md->start >= vcount || b - md->start >= vcount) continue;
            uint32_t na = g_graph.v2n[a - md->start], nb = g_graph.v2n[b - md->start];
            if (na == nb) continue;
            deg[na + 1]++;
            deg[nb + 1]++;
        }
    }
    for (size_t i = 0; i < nodes; i++) deg[i + 1] += deg[i];
    uint32_t* raw = EZ_ALLOC(deg[nodes] + 1, sizeof(uint32_t));
    memset(fill, 0, nodes * sizeof(uint32_t));
    for (size_t t = 0; t < tcount; t++) {
        Triangle* tri = TriangleReference(md->tstart + t);
        uint32_t c[3] = { tri->a, tri->b, tri->c };
        for (int e = 0; e < 3; e++) {
            uint32_t a = c[e], b = c[(e + 1) % 3];
            if (a < md->start || b < md->start || a - md->start >= vcount || b - md->start >= vcount) continue;
            uint32_t na = g_graph.v2n[a - md->start], nb = g_graph.v2n[b - md->start];
            if (na == nb) continue;
            raw[deg[na] + fill[na]++] = nb;
            raw[deg[nb] + fill[nb]++] = na;
        }
    }
    g_graph.adjptr = EZ_ALLOC(nodes + 1, sizeof(uint32_t));
    g_graph.adj = EZ_ALLOC(deg[nodes] + 1, sizeof(uint32_t));
    size_t out = 0;
    for (size_t i = 0; i < nodes; i++) {
        g_graph.adjptr[i] = (uint32_t)out;
        uint32_t* list = &raw[deg[i]];
        size_t len = deg[i + 1] - deg[i];
        qsort(list, len, sizeof(uint32_t), CompareU32);
        for (size_t k = 0; k < len; k++) if (k == 0 || list[k] != list[k - 1]) g_graph.adj[out++] = list[k];
    }
    g_graph.adjptr[nodes] = (uint32_t)out;
    EZ_FREE(raw);
    EZ_FREE(deg);
    EZ_FREE(fill);
    EZ_FREE(sorted);
    return &g_graph;
}

static int64_t CellKey(int64_t x, int64_t y, int64_t z) {
    return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL);
}

static void HashInsert(PointHash* h, int64_t key, uint32_t value) {
    size_t i = (size_t)((uint64_t)key * 11400714819323198485ull) % h->capacity;
    while (h->values[i] != (uint32_t)-1) i = (i + 1) % h->capacity;
    h->keys[i] = key;
    h->values[i] = value;
}

static void FreeLevel(HeatLevel* l) {
    void* ptrs[] = { l->rowptr, l->cols, l->vals, l->diag, l->mh, l->parent, l->x, l->r, l->z, l->p, l->ap, l->rhs, l->vx, l->vb, l->vr };
    for (size_t i = 0; i < sizeof(ptrs) / sizeof(ptrs[0]); i++) if (ptrs[i]) EZ_FREE(ptrs[i]);
    memset(l, 0, sizeof(HeatLevel));
}

static void FreeJob() {
    void* ptrs[] = { g_job.bones, g_job.solve, g_job.nearest, g_job.result };
    for (size_t i = 0; i < sizeof(ptrs) / sizeof(ptrs[0]); i++) if (ptrs[i]) EZ_FREE(ptrs[i]);
    for (size_t l = 0; l < HEAT_MAX_LEVELS; l++) FreeLevel(&(g_job.levels[l]));
    memset(&g_job, 0, sizeof(g_job));
}

static void LevelSpMV(const HeatLevel* l, const double* in, double* out) {
    for (size_t i = 0; i < l->n; i++) {
        double s = l->diag[i] * in[i];
        for (uint32_t k = l->rowptr[i]; k < l->rowptr[i + 1]; k++) s -= l->vals[k] * in[l->cols[k]];
        out[i] = s;
    }
}

static double Dot(const double* a, const double* b, size_t n) {
    double s = 0.0;
    for (size_t i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

static void LevelAllocVectors(HeatLevel* l) {
    l->x = EZ_ALLOC(l->n, sizeof(double));
    l->r = EZ_ALLOC(l->n, sizeof(double));
    l->z = EZ_ALLOC(l->n, sizeof(double));
    l->p = EZ_ALLOC(l->n, sizeof(double));
    l->ap = EZ_ALLOC(l->n, sizeof(double));
    l->rhs = EZ_ALLOC(l->n, sizeof(double));
    l->vx = EZ_ALLOC(l->n, sizeof(double));
    l->vb = EZ_ALLOC(l->n, sizeof(double));
    l->vr = EZ_ALLOC(l->n, sizeof(double));
}

static void GaussSeidel(const HeatLevel* l, double* x, const double* b, BOOL forward) {
    for (size_t k = 0; k < l->n; k++) {
        size_t i = forward ? k : l->n - 1 - k;
        double s = b[i];
        for (uint32_t q = l->rowptr[i]; q < l->rowptr[i + 1]; q++) s += l->vals[q] * x[l->cols[q]];
        x[i] = s / l->diag[i];
    }
}

static void VCycle(size_t level, const double* b, double* x) {
    HeatLevel* l = &(g_job.levels[level]);
    memset(x, 0, l->n * sizeof(double));
    if (level + 1 >= g_job.nlevels) {
        for (int sweep = 0; sweep < 12; sweep++) {
            GaussSeidel(l, x, b, TRUE);
            GaussSeidel(l, x, b, FALSE);
        }
        return;
    }
    GaussSeidel(l, x, b, TRUE);
    for (size_t i = 0; i < l->n; i++) {
        double ax = l->diag[i] * x[i];
        for (uint32_t q = l->rowptr[i]; q < l->rowptr[i + 1]; q++) ax -= l->vals[q] * x[l->cols[q]];
        l->vr[i] = b[i] - ax;
    }
    HeatLevel* c = &(g_job.levels[level + 1]);
    memset(c->vb, 0, c->n * sizeof(double));
    for (size_t i = 0; i < l->n; i++) c->vb[l->parent[i]] += l->vr[i];
    VCycle(level + 1, c->vb, c->vx);
    for (size_t i = 0; i < l->n; i++) x[i] += c->vx[l->parent[i]];
    GaussSeidel(l, x, b, FALSE);
}

static void Precondition(const double* r, double* z) {
    if (g_job.nlevels <= 1) {
        HeatLevel* l = &(g_job.levels[0]);
        for (size_t i = 0; i < l->n; i++) z[i] = r[i] / l->diag[i];
        return;
    }
    VCycle(0, r, z);
}

static int CompareCoarseEdge(const void* x, const void* y) {
    const CoarseEdge* p = (const CoarseEdge*)x;
    const CoarseEdge* q = (const CoarseEdge*)y;
    if (p->a != q->a) return p->a < q->a ? -1 : 1;
    if (p->b != q->b) return p->b < q->b ? -1 : 1;
    return 0;
}

static BOOL CoarsenLevel(HeatLevel* fine, const vec3* pos, float cell, HeatLevel* coarse, vec3** coarsepos) {
    size_t n = fine->n;
    size_t cap = n * 2 + 1;
    int64_t* keys = EZ_ALLOC(cap * 3, sizeof(int64_t));
    uint32_t* ids = EZ_ALLOC(cap, sizeof(uint32_t));
    for (size_t i = 0; i < cap; i++) ids[i] = (uint32_t)-1;
    uint32_t* parent = EZ_ALLOC(n, sizeof(uint32_t));
    size_t nc = 0;
    for (size_t i = 0; i < n; i++) {
        int64_t c[3] = { (int64_t)floorf(pos[i][0] / cell), (int64_t)floorf(pos[i][1] / cell), (int64_t)floorf(pos[i][2] / cell) };
        uint64_t hash = (uint64_t)((c[0] * 73856093LL) ^ (c[1] * 19349663LL) ^ (c[2] * 83492791LL));
        size_t h = (size_t)(hash * 11400714819323198485ull % cap);
        while (ids[h] != (uint32_t)-1 && !(keys[h * 3] == c[0] && keys[h * 3 + 1] == c[1] && keys[h * 3 + 2] == c[2])) h = (h + 1) % cap;
        if (ids[h] == (uint32_t)-1) {
            keys[h * 3] = c[0];
            keys[h * 3 + 1] = c[1];
            keys[h * 3 + 2] = c[2];
            ids[h] = (uint32_t)nc++;
        }
        parent[i] = ids[h];
    }
    EZ_FREE(keys);
    EZ_FREE(ids);
    if (nc * 10 > n * 9) {
        EZ_FREE(parent);
        return FALSE;
    }
    size_t nnz = fine->rowptr[n];
    CoarseEdge* edges = EZ_ALLOC(nnz + 1, sizeof(CoarseEdge));
    size_t ne = 0;
    for (size_t i = 0; i < n; i++) {
        for (uint32_t k = fine->rowptr[i]; k < fine->rowptr[i + 1]; k++) {
            uint32_t a = parent[i], b = parent[fine->cols[k]];
            if (a == b) continue;
            edges[ne++] = (CoarseEdge){ a, b, fine->vals[k] };
        }
    }
    qsort(edges, ne, sizeof(CoarseEdge), CompareCoarseEdge);
    coarse->n = nc;
    coarse->rowptr = EZ_ALLOC(nc + 1, sizeof(uint32_t));
    coarse->cols = EZ_ALLOC(ne + 1, sizeof(uint32_t));
    coarse->vals = EZ_ALLOC(ne + 1, sizeof(double));
    coarse->diag = EZ_ALLOC(nc, sizeof(double));
    coarse->mh = EZ_ALLOC(nc, sizeof(double));
    size_t out = 0;
    for (size_t e = 0; e < ne; e++) {
        if (out > 0 && coarse->cols[out - 1] == edges[e].b && e > 0 && edges[e - 1].a == edges[e].a) {
            coarse->vals[out - 1] += edges[e].w;
            continue;
        }
        coarse->cols[out] = edges[e].b;
        coarse->vals[out] = edges[e].w;
        coarse->rowptr[edges[e].a + 1]++;
        out++;
    }
    for (size_t c = 0; c < nc; c++) coarse->rowptr[c + 1] += coarse->rowptr[c];
    EZ_FREE(edges);
    for (size_t i = 0; i < n; i++) coarse->mh[parent[i]] += fine->mh[i];
    for (size_t c = 0; c < nc; c++) {
        double s = 0.0;
        for (uint32_t k = coarse->rowptr[c]; k < coarse->rowptr[c + 1]; k++) s += coarse->vals[k];
        coarse->diag[c] = s + coarse->mh[c];
    }
    vec3* cp = EZ_ALLOC(nc, sizeof(vec3));
    uint32_t* count = EZ_ALLOC(nc, sizeof(uint32_t));
    for (size_t i = 0; i < n; i++) {
        glm_vec3_add(cp[parent[i]], (float*)pos[i], cp[parent[i]]);
        count[parent[i]]++;
    }
    for (size_t c = 0; c < nc; c++) glm_vec3_scale(cp[c], 1.0f / (float)(count[c] ? count[c] : 1), cp[c]);
    EZ_FREE(count);
    fine->parent = parent;
    *coarsepos = cp;
    return TRUE;
}

static void FinishAutoWeights() {
    MeshAnimation* anim = AnimationReference(g_job.anim);
    MeshDescriptor* md = MeshReference(g_job.mesh);
    WeldGraph* g = Graph();
    size_t nb = g_job.nbones;
    float* w = EZ_ALLOC(nb + MAX_BONE_INFLUENCES, sizeof(float));
    uint32_t* idx = EZ_ALLOC(nb + MAX_BONE_INFLUENCES, sizeof(uint32_t));
    for (size_t n = 0; n < g_job.nnodes; n++) {
        size_t count = 0;
        float newsum = 0.0f;
        for (size_t b = 0; b < nb; b++) {
            if (!g_job.solve[b]) continue;
            float v = g_job.result[n * nb + b];
            if (!(v > 0.0f)) continue;
            if (v > 1.0f) v = 1.0f;
            idx[count] = g_job.bones[b];
            w[count] = v;
            newsum += v;
            count++;
        }
        for (size_t i = 0; i < count; i++) if (w[i] < 0.01f * fmaxf(newsum, 1e-6f)) w[i] = 0.0f;
        uint32_t vfirst = g->nodeverts[g->nodeptr[n]];
        VertexSkin* first = &(RendererGeometry()->skins.data[md->skinstart + vfirst]);
        if (g_job.selectedonly) {
            float sum = 0.0f;
            for (size_t i = 0; i < count; i++) sum += w[i];
            sum = fminf(sum, 1.0f);
            float others = 0.0f;
            for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) {
                uint32_t bi = first->indices[k];
                BOOL solved = FALSE;
                for (size_t b = 0; b < nb; b++) if (g_job.solve[b] && g_job.bones[b] == bi) solved = TRUE;
                if (!solved && first->weights[k] > 0.0f) others += first->weights[k];
            }
            for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) {
                uint32_t bi = first->indices[k];
                BOOL solved = FALSE;
                for (size_t b = 0; b < nb; b++) if (g_job.solve[b] && g_job.bones[b] == bi) solved = TRUE;
                if (solved || !(first->weights[k] > 0.0f) || others <= 0.0f) continue;
                idx[count] = bi;
                w[count] = first->weights[k] / others * (1.0f - sum);
                count++;
            }
        }
        VertexSkin packed;
        PackSkin(&packed, idx, w, count, g_job.bones[g_job.nearest[n]]);
        for (uint32_t k = g->nodeptr[n]; k < g->nodeptr[n + 1]; k++)
            RendererGeometry()->skins.data[md->skinstart + g->nodeverts[k]] = packed;
    }
    EZ_FREE(w);
    EZ_FREE(idx);
    UpdateSkins();
    anim->dirty = TRUE;
    loginfo("Generated %s weights for %zu vertices (%zu bones)", g_autoweight_labels[g_job.method], g->vcount, g_job.tosolve);
    FreeJob();
}

static void HeatStart(size_t bone) {
    HeatLevel* l = &(g_job.levels[0]);
    for (size_t i = 0; i < l->n; i++) {
        BOOL mine = g_job.nearest[i] == bone;
        l->rhs[i] = mine ? l->mh[i] : 0.0;
        l->x[i] = mine ? 1.0 : 0.0;
    }
    LevelSpMV(l, l->x, l->ap);
    for (size_t i = 0; i < l->n; i++) l->r[i] = l->rhs[i] - l->ap[i];
    Precondition(l->r, l->z);
    memcpy(l->p, l->z, l->n * sizeof(double));
    g_job.rz = Dot(l->r, l->z, l->n);
    g_job.bnorm = sqrt(Dot(l->rhs, l->rhs, l->n));
    g_job.iteration = 0;
    g_job.started = TRUE;
}

static void SkinnedPositions(MeshDescriptor* md, size_t aidx) {
    size_t n = MeshVertexCount(md);
    if (n > g_skinned_cap) {
        if (g_skinned) EZ_FREE(g_skinned);
        if (g_skinned_normals) EZ_FREE(g_skinned_normals);
        g_skinned = EZ_ALLOC(n, sizeof(vec3));
        g_skinned_normals = EZ_ALLOC(n, sizeof(vec3));
        g_skinned_cap = n;
    }
    Geometry* geo = RendererGeometry();
    static mat4 pose[MAX_BONES];
    BlobCopy(pose, &(geo->poses.data[aidx * MAX_BONES]), sizeof(mat4) * MAX_BONES);
    mat4 transform;
    M4Copy(transform, md->transform);
    for (size_t v = 0; v < n; v++) {
        float* p = geo->vertices.data[md->start + v];
        vec3 rest = { p[0], p[1], p[2] }, out = { 0, 0, 0 };
        VertexSkin* s = &(geo->skins.data[md->skinstart + v]);
        float total = 0.0f;
        for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) {
            if (!(s->weights[k] > 0.0f) || s->indices[k] >= MAX_BONES) continue;
            vec3 q;
            glm_mat4_mulv3(pose[s->indices[k]], rest, 1.0f, q);
            glm_vec3_muladds(q, s->weights[k], out);
            total += s->weights[k];
        }
        if (!(total > 0.0f)) glm_vec3_copy(rest, out);
        glm_mat4_mulv3(transform, out, 1.0f, g_skinned[v]);
        SETVEC3(g_skinned_normals[v], 0, 0, 0);
    }
    size_t tcount = MeshTriangleCount(md);
    for (size_t t = 0; t < tcount; t++) {
        Triangle* tri = TriangleReference(md->tstart + t);
        uint32_t a = tri->a - md->start, b = tri->b - md->start, c = tri->c - md->start;
        if (a >= n || b >= n || c >= n) continue;
        vec3 e1, e2, fn;
        glm_vec3_sub(g_skinned[b], g_skinned[a], e1);
        glm_vec3_sub(g_skinned[c], g_skinned[a], e2);
        glm_vec3_cross(e1, e2, fn);
        glm_vec3_add(g_skinned_normals[a], fn, g_skinned_normals[a]);
        glm_vec3_add(g_skinned_normals[b], fn, g_skinned_normals[b]);
        glm_vec3_add(g_skinned_normals[c], fn, g_skinned_normals[c]);
    }
}

static BOOL RayTriangle(const vec3 o, const vec3 d, const vec3 a, const vec3 b, const vec3 c, float* t) {
    vec3 e1, e2, h, s, q;
    glm_vec3_sub((float*)b, (float*)a, e1);
    glm_vec3_sub((float*)c, (float*)a, e2);
    glm_vec3_cross((float*)d, e2, h);
    float det = glm_vec3_dot(e1, h);
    if (fabsf(det) < 1e-12f) return FALSE;
    float f = 1.0f / det;
    glm_vec3_sub((float*)o, (float*)a, s);
    float u = f * glm_vec3_dot(s, h);
    if (u < 0.0f || u > 1.0f) return FALSE;
    glm_vec3_cross(s, e1, q);
    float v = f * glm_vec3_dot((float*)d, q);
    if (v < 0.0f || u + v > 1.0f) return FALSE;
    float tt = f * glm_vec3_dot(e2, q);
    if (tt <= 1e-6f) return FALSE;
    *t = tt;
    return TRUE;
}

static BOOL ShowRest() {
    return g_mode == RIG_MODE_EDIT && RigViewportActive();
}

static void BoneFrameMesh(MeshAnimation* anim, size_t bone, mat4 out) {
    if (ShowRest()) M4Copy(out, RestGlobals()[bone]);
    else BoneGlobal(anim, bone, out);
}

static BOOL RigVisible(size_t animid) {
    if (animid >= NumAnimations()) return FALSE;
    MeshAnimation* anim = AnimationReference(animid);
    return anim->enable && anim->meshid < NumMeshes() && !MeshReference(anim->meshid)->disabled;
}

static void MeshSpace(mat4 to_world, mat4 to_mesh, mat3 to_mesh3) {
    RigMeshTransform(to_world);
    glm_mat4_inv(to_world, to_mesh);
    glm_mat4_pick3(to_mesh, to_mesh3);
}

static void SetPoseFromGlobal(MeshAnimation* anim, size_t bone, const mat4 parent, mat4 global) {
    mat4 pinv, pcopy, local, bind, binv, delta;
    M4Copy(pcopy, parent);
    glm_mat4_inv(pcopy, pinv);
    glm_mat4_mul(pinv, global, local);
    M4Copy(bind, anim->skeleton.bones[bone].localbind);
    glm_mat4_inv(bind, binv);
    glm_mat4_mul(binv, local, delta);
    BonePose pose;
    PoseFromMatrix(delta, &pose);
    BoneSetPose(anim, bone, &pose);
}

size_t RigActiveMesh() {
    return g_mesh;
}

size_t RigActiveAnimation() {
    if (g_mesh == (size_t)-1) return (size_t)-1;
    return RigOfMesh(g_mesh);
}

MeshAnimation* RigActive() {
    size_t a = RigActiveAnimation();
    return a == (size_t)-1 ? NULL : AnimationReference(a);
}

void RigSetActiveMesh(size_t meshid) {
    if (meshid == g_mesh) return;
    MeshAnimation* old = RigActive();
    if (old && old->rest) { old->rest = FALSE; old->dirty = TRUE; }
    g_mesh = meshid;
    RigClearSelection();
    InvalidateRest();
    g_hit_valid = FALSE;
}

void RigUpdate() {
    if (g_mesh != (size_t)-1 && g_mesh >= NumMeshes()) g_mesh = (size_t)-1;
    MeshAnimation* anim = RigActive();
    InvalidateRest();
    if (!anim) {
        g_active = (size_t)-1;
        return;
    }
    if (g_active != (size_t)-1 && g_active >= anim->skeleton.bonecount) g_active = (size_t)-1;
    BOOL rest = g_mode == RIG_MODE_EDIT && RigViewportActive();
    if (anim->rest != rest) {
        anim->rest = rest;
        anim->dirty = TRUE;
    }
}

RigMode RigGetMode() {
    return g_mode;
}

void RigSetMode(RigMode mode) {
    if (mode >= RIG_MODE_COUNT) return;
    g_mode = mode;
    SetOverlayMode(ANIMATION_SELECT_MODE);
    RigUpdate();
}

BOOL RigViewportActive() {
    return GetOverlayMode() == ANIMATION_SELECT_MODE;
}

const char** RigModeLabels() {
    return g_mode_labels;
}

EditHandle RigEditHandle() {
    return g_handle;
}

void RigSetEditHandle(EditHandle handle) {
    if (handle < EDIT_HANDLE_COUNT) g_handle = handle;
}

const char** RigEditHandleLabels() {
    return g_handle_labels;
}

size_t RigActiveBone() {
    MeshAnimation* anim = RigActive();
    if (!anim || g_active >= anim->skeleton.bonecount) return (size_t)-1;
    return g_active;
}

void RigSelectBone(size_t bone, BOOL extend) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return;
    if (!extend) memset(g_selected, 0, sizeof(g_selected));
    SetSelectedBit(bone, TRUE);
    g_active = bone;
}

void RigDeselectBone(size_t bone) {
    SetSelectedBit(bone, FALSE);
    if (g_active == bone) {
        g_active = (size_t)-1;
        MeshAnimation* anim = RigActive();
        if (anim) for (size_t b = 0; b < anim->skeleton.bonecount; b++) if (SelectedBit(b)) { g_active = b; break; }
    }
}

BOOL RigBoneSelected(size_t bone) {
    return SelectedBit(bone);
}

void RigClearSelection() {
    memset(g_selected, 0, sizeof(g_selected));
    g_active = (size_t)-1;
}

size_t RigSelectedCount() {
    MeshAnimation* anim = RigActive();
    if (!anim) return 0;
    size_t n = 0;
    for (size_t b = 0; b < anim->skeleton.bonecount; b++) n += SelectedBit(b) ? 1 : 0;
    return n;
}

void RigSelectAll() {
    MeshAnimation* anim = RigActive();
    if (!anim) return;
    for (size_t b = 0; b < anim->skeleton.bonecount; b++) SetSelectedBit(b, TRUE);
    if (g_active >= anim->skeleton.bonecount && anim->skeleton.bonecount > 0) g_active = 0;
}

void RigMeshTransform(mat4 out) {
    MeshDescriptor* md = ActiveDescriptor();
    if (!md) { glm_mat4_identity(out); return; }
    M4Copy(out, md->transform);
}

void RigBoneFrame(const vec3 head, const vec3 tail, float roll, mat4 out) {
    vec3 dir;
    glm_vec3_sub((float*)tail, (float*)head, dir);
    if (glm_vec3_norm(dir) < 1e-9f) SETVEC3(dir, 0, 1, 0);
    glm_vec3_normalize(dir);
    versor q0, qr, q;
    vec3 y = { 0, 1, 0 };
    QuatFromTo(y, dir, q0);
    glm_quatv(qr, roll, dir);
    glm_quat_mul(qr, q0, q);
    glm_quat_mat4(q, out);
    out[3][0] = head[0];
    out[3][1] = head[1];
    out[3][2] = head[2];
}

void RigRestHeadTail(size_t bone, vec3 head, vec3 tail) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return;
    mat4 g;
    M4Copy(g, RestGlobals()[bone]);
    vec3 t;
    glm_vec3_copy(anim->skeleton.bones[bone].tail, t);
    M4Head(g, head);
    glm_mat4_mulv3(g, t, 1.0f, tail);
}

float RigBoneRoll(size_t bone) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return 0.0f;
    mat4 g;
    M4Copy(g, RestGlobals()[bone]);
    vec3 head, tail, dir;
    RigRestHeadTail(bone, head, tail);
    glm_vec3_sub(tail, head, dir);
    if (glm_vec3_norm(dir) < 1e-9f) return 0.0f;
    mat4 frame;
    glm_mat4_identity(frame);
    vec3 x = { g[0][0], g[0][1], g[0][2] };
    glm_vec3_normalize(dir);
    for (int r = 0; r < 3; r++) { frame[0][r] = x[r]; frame[1][r] = dir[r]; }
    return FrameRoll(frame);
}

void RigSetBoneRest(size_t bone, const vec3 head, const vec3 tail, float roll) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return;
    float length = glm_vec3_distance((float*)head, (float*)tail);
    if (length < 1e-6f) return;
    mat4 frame;
    RigBoneFrame(head, tail, roll, frame);
    SetRestFrame(anim, bone, frame, length);
    size_t p = anim->skeleton.bones[bone].parent;
    if ((anim->skeleton.bones[bone].flags & BONE_FLAG_CONNECTED) && p < anim->skeleton.bonecount) {
        vec3 ph, pt;
        RigRestHeadTail(p, ph, pt);
        if (glm_vec3_distance(pt, (float*)head) > 1e-6f && glm_vec3_distance(ph, (float*)head) > 1e-6f) {
            float proll = RigBoneRoll(p);
            mat4 pf;
            RigBoneFrame(ph, head, proll, pf);
            uint32_t saved = anim->skeleton.bones[bone].flags;
            anim->skeleton.bones[bone].flags &= ~BONE_FLAG_CONNECTED; // the bone itself is already placed
            SetRestFrame(anim, p, pf, glm_vec3_distance(ph, (float*)head));
            anim->skeleton.bones[bone].flags = saved;
        }
    }
}

void RigUniqueBoneName(const char* base, char out[256]) {
    MeshAnimation* anim = RigActive();
    const char* root = base && base[0] ? base : "Bone";
    snprintf(out, 256, "%s", root);
    if (!anim) return;
    for (int k = 1; k < 100000; k++) {
        if (SkeletonFindBone(&(anim->skeleton), out) == (size_t)-1) return;
        snprintf(out, 256, "%.240s.%03d", root, k);
    }
}

size_t RigAddBone(size_t parent, const vec3 head, const vec3 tail, const char* name) {
    MeshAnimation* anim = RigActive();
    if (!anim) return (size_t)-1;
    Skeleton* sk = &(anim->skeleton);
    if (sk->bonecount >= MAX_BONES) {
        logerror("A rig can hold at most %d bones", MAX_BONES);
        return (size_t)-1;
    }
    float length = glm_vec3_distance((float*)head, (float*)tail);
    if (length < 1e-6f) return (size_t)-1;
    if (parent != (size_t)-1 && parent >= sk->bonecount) parent = (size_t)-1;
    size_t index = sk->bonecount;
    Bone* bone = &(sk->bones[index]);
    memset(bone, 0, sizeof(Bone));
    char unique[256];
    RigUniqueBoneName(name, unique);
    memcpy(bone->name, unique, sizeof(unique));
    bone->parent = parent;
    bone->flags = BONE_FLAG_DEFORM | BONE_FLAG_INITIALIZED;
    mat4 identity = GLM_MAT4_IDENTITY_INIT;
    M4Copy(bone->localbind, identity);
    M4Copy(bone->inversebind, identity);
    sk->bonecount++;
    if (parent != (size_t)-1) {
        vec3 ph, pt;
        RigRestHeadTail(parent, ph, pt);
        if (glm_vec3_distance(pt, (float*)head) < 1e-5f * fmaxf(1.0f, length)) bone->flags |= BONE_FLAG_CONNECTED;
    }
    InvalidateRest();
    mat4 frame;
    RigBoneFrame(head, tail, 0.0f, frame);
    SetRestFrame(anim, index, frame, length);
    return index;
}

size_t RigExtrudeBone(size_t from) {
    MeshAnimation* anim = RigActive();
    if (!anim || from >= anim->skeleton.bonecount) return (size_t)-1;
    vec3 head, tail, dir, newtail;
    RigRestHeadTail(from, head, tail);
    glm_vec3_sub(tail, head, dir);
    glm_vec3_scale(dir, 0.8f, dir);
    glm_vec3_add(tail, dir, newtail);
    char base[256];
    snprintf(base, sizeof(base), "%s", anim->skeleton.bones[from].name);
    char* dot = strrchr(base, '.');
    if (dot && strlen(dot) == 4 && dot[1] >= '0' && dot[1] <= '9') *dot = '\0';
    return RigAddBone(from, tail, newtail, base);
}

BOOL RigDeleteBone(size_t bone) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return FALSE;
    Skeleton* sk = &(anim->skeleton);
    if (sk->bonecount <= 1) {
        logwarn("A rig needs at least one bone - use \"Remove Rig\" to delete the whole rig");
        return FALSE;
    }
    static mat4 rest[MAX_BONES];
    SkeletonRestGlobals(sk, rest);
    size_t parent = sk->bones[bone].parent < sk->bonecount ? sk->bones[bone].parent : (size_t)-1;
    size_t heir = parent;
    if (heir == (size_t)-1) for (size_t c = 0; c < sk->bonecount; c++) if (c != bone && sk->bones[c].parent == bone) { heir = c; break; }
    if (heir == (size_t)-1) heir = bone == 0 ? 1 : 0;
    for (size_t c = 0; c < sk->bonecount; c++) {
        if (c == bone || sk->bones[c].parent != bone) continue;
        sk->bones[c].parent = parent;
        sk->bones[c].flags &= ~BONE_FLAG_CONNECTED;
        mat4 child, pinv, local;
        M4Copy(child, rest[c]);
        if (parent != (size_t)-1) {
            mat4 p;
            M4Copy(p, rest[parent]);
            glm_mat4_inv(p, pinv);
            glm_mat4_mul(pinv, child, local);
        } else {
            M4Copy(local, child);
        }
        M4Copy(sk->bones[c].localbind, local);
    }
    char name[256];
    memcpy(name, sk->bones[bone].name, sizeof(name));
    MeshDescriptor* md = ActiveDescriptor();
    if (md && RigSkinsValid(RigActiveAnimation())) {
        size_t n = MeshVertexCount(md);
        uint32_t newheir = heir > bone ? (uint32_t)heir - 1 : (uint32_t)heir;
        for (size_t v = 0; v < n; v++) {
            VertexSkin* s = SkinRef(v);
            for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) if (s->indices[k] == bone) s->indices[k] = (uint32_t)heir;
            RemapSkinBone(s, (uint32_t)bone, newheir);
        }
        UpdateSkins();
    }
    memmove(&(sk->bones[bone]), &(sk->bones[bone + 1]), (sk->bonecount - bone - 1) * sizeof(Bone));
    sk->bonecount--;
    for (size_t b = 0; b < sk->bonecount; b++) {
        size_t p = sk->bones[b].parent;
        if (p != (size_t)-1 && p > bone && p < MAX_BONES) sk->bones[b].parent = p - 1;
    }
    memmove(&(anim->overrides[bone]), &(anim->overrides[bone + 1]), (MAX_BONES - bone - 1) * sizeof(BonePose));
    for (size_t b = bone; b + 1 < MAX_BONES; b++) {
        BOOL next = (anim->posed[(b + 1) / 32] >> ((b + 1) % 32)) & 1u;
        if (next) anim->posed[b / 32] |= 1u << (b % 32);
        else anim->posed[b / 32] &= ~(1u << (b % 32));
    }
    anim->posed[(MAX_BONES - 1) / 32] &= ~(1u << ((MAX_BONES - 1) % 32));
    for (uint32_t c = anim->numik; c-- > 0;) {
        IKConstraint* ik = &(anim->ik[c]);
        if (ik->bone == bone || ik->target == bone) {
            memmove(&(anim->ik[c]), &(anim->ik[c + 1]), (anim->numik - c - 1) * sizeof(IKConstraint));
            anim->numik--;
            continue;
        }
        if (ik->pole == bone) ik->pole = IK_NO_BONE;
        if (ik->bone > bone) ik->bone--;
        if (ik->target > bone) ik->target--;
        if (ik->pole != IK_NO_BONE && ik->pole > bone) ik->pole--;
    }
    ChannelRemoveAll(anim, name);
    uint32_t sel[MAX_BONES / 32];
    memcpy(sel, g_selected, sizeof(sel));
    memset(g_selected, 0, sizeof(g_selected));
    for (size_t b = 0; b < MAX_BONES; b++) {
        if (b == bone || !((sel[b / 32] >> (b % 32)) & 1u)) continue;
        SetSelectedBit(b > bone ? b - 1 : b, TRUE);
    }
    if (g_active == bone) g_active = (size_t)-1;
    else if (g_active != (size_t)-1 && g_active > bone) g_active--;
    Touch(anim);
    return TRUE;
}

BOOL RigSetParent(size_t bone, size_t parent) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return FALSE;
    Skeleton* sk = &(anim->skeleton);
    if (parent != (size_t)-1 && (parent >= sk->bonecount || SkeletonIsDescendant(sk, parent, bone))) {
        logwarn("A bone cannot be parented to itself or one of its children");
        return FALSE;
    }
    mat4* rest = RestGlobals();
    mat4 g, local;
    M4Copy(g, rest[bone]);
    if (parent != (size_t)-1) {
        mat4 p, inv;
        M4Copy(p, rest[parent]);
        glm_mat4_inv(p, inv);
        glm_mat4_mul(inv, g, local);
        vec3 ph, pt, h;
        RigRestHeadTail(parent, ph, pt);
        M4Head(g, h);
        if (glm_vec3_distance(h, pt) > 1e-5f) sk->bones[bone].flags &= ~BONE_FLAG_CONNECTED;
    } else {
        M4Copy(local, g);
        sk->bones[bone].flags &= ~BONE_FLAG_CONNECTED;
    }
    M4Copy(sk->bones[bone].localbind, local);
    sk->bones[bone].parent = parent;
    Touch(anim);
    return TRUE;
}

void RigRenameBone(size_t bone, const char* name) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount || !name || !name[0]) return;
    Bone* b = &(anim->skeleton.bones[bone]);
    if (strcmp(b->name, name) == 0) return;
    char old[256], unique[256];
    memcpy(old, b->name, sizeof(old));
    b->name[0] = '\0';
    RigUniqueBoneName(name, unique);
    memcpy(b->name, unique, sizeof(unique));
    ChannelRenameAll(anim, old, unique);
}

void RigSetBoneFlag(size_t bone, uint32_t flag, BOOL on) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return;
    if (on) anim->skeleton.bones[bone].flags |= flag;
    else anim->skeleton.bones[bone].flags &= ~flag;
    if (flag == BONE_FLAG_CONNECTED && on) {
        size_t p = anim->skeleton.bones[bone].parent;
        if (p < anim->skeleton.bonecount) {
            vec3 ph, pt, h, t;
            RigRestHeadTail(p, ph, pt);
            RigRestHeadTail(bone, h, t);
            RigSetBoneRest(bone, pt, t, RigBoneRoll(bone));
        }
    }
    Touch(anim);
}

size_t RigSymmetrize(MirrorDirection direction) {
    MeshAnimation* anim = RigActive();
    if (!anim) return 0;
    Skeleton* sk = &(anim->skeleton);
    size_t order[MAX_BONES];
    size_t n = SkeletonOrder(sk, order);
    size_t touched = 0;
    for (size_t i = 0; i < n; i++) {
        size_t b = order[i];
        if (b >= sk->bonecount) continue;
        vec3 head, tail;
        RigRestHeadTail(b, head, tail);
        float mid = 0.5f * (head[0] + tail[0]);
        if (direction == MIRROR_POSITIVE_TO_NEGATIVE ? mid <= 1e-6f : mid >= -1e-6f) continue;
        char partner[256];
        if (!MirrorName(sk->bones[b].name, partner)) continue;
        vec3 mh = { -head[0], head[1], head[2] };
        vec3 mt = { -tail[0], tail[1], tail[2] };
        float roll = -RigBoneRoll(b);
        size_t m = SkeletonFindBone(sk, partner);
        size_t parent = sk->bones[b].parent < sk->bonecount ? MirrorBone(sk, sk->bones[b].parent) : (size_t)-1;
        if (m == (size_t)-1) {
            m = RigAddBone(parent, mh, mt, partner);
            if (m == (size_t)-1) continue;
        } else {
            if (m != b && parent != (size_t)-1 && !SkeletonIsDescendant(sk, parent, m)) RigSetParent(m, parent);
        }
        uint32_t keep = sk->bones[b].flags;
        sk->bones[m].flags = keep;
        RigSetBoneRest(m, mh, mt, roll);
        touched++;
    }
    if (touched) Touch(anim);
    return touched;
}

BOOL RigCreateForMesh(size_t meshid) {
    if (meshid >= NumMeshes()) return FALSE;
    MeshDescriptor* md = MeshReference(meshid);
    if (md->pose != (uint32_t)-1) {
        RigSetActiveMesh(meshid);
        return TRUE;
    }
    size_t vcount = MeshVertexCount(md);
    if (vcount == 0) {
        logerror("Only objects with vertices can be rigged");
        return FALSE;
    }
    static Skeleton skeleton;
    memset(&skeleton, 0, sizeof(Skeleton));
    Skeleton* sk = &skeleton;
    vec3 center, extents;
    glm_vec3_copy(md->center, center);
    glm_vec3_copy(md->extents, extents);
    float height = extents[1] > 1e-6f ? extents[1] : fmaxf(fmaxf(extents[0], extents[2]), 0.1f);
    vec3 head = { center[0], center[1] - height, center[2] };
    vec3 tail = { center[0], center[1], center[2] };
    mat4 frame, inv;
    RigBoneFrame(head, tail, 0.0f, frame);
    glm_mat4_inv(frame, inv);
    Bone* root = &(sk->bones[0]);
    snprintf(root->name, sizeof(root->name), "Root");
    M4Copy(root->localbind, frame);
    M4Copy(root->inversebind, inv);
    root->parent = (size_t)-1;
    SETVEC3(root->tail, 0.0f, glm_vec3_distance(head, tail), 0.0f);
    root->flags = BONE_FLAG_DEFORM | BONE_FLAG_INITIALIZED;
    sk->bonecount = 1;
    size_t aidx = SubmitRig(meshid, sk);
    md = MeshReference(meshid);
    md->skinstart = (VertexID)NumSkins();
    for (size_t v = 0; v < vcount; v++) {
        VertexSkin skin = { 0 };
        skin.weights[0] = 1.0f;
        SubmitVertexSkin(skin);
    }
    md->pose = (uint32_t)(aidx * MAX_BONES);
    MeshAnimation* anim = AnimationReference(aidx);
    anim->synced = TRUE;
    anim->looping = FALSE;
    ClipCreate(anim, "Action");
    UpdateMeshes();
    UpdateSkins();
    UpdatePoses();
    RigSetActiveMesh((size_t)-1);
    RigSetActiveMesh(meshid);
    RigSelectBone(0, FALSE);
    loginfo("Created a rig for \"%s\" - add bones in Edit Rig mode, then generate weights", *MeshNameReference(meshid));
    return TRUE;
}

BOOL RigRemove(size_t meshid) {
    RigAutoWeightsCancel();
    BOOL ok = DeleteRig(meshid);
    if (ok) {
        RigClearSelection();
        InvalidateRest();
        loginfo("Removed the rig of \"%s\"", *MeshNameReference(meshid));
    }
    return ok;
}

BOOL RigSkinsValid(size_t animid) {
    if (animid >= NumAnimations()) return FALSE;
    MeshAnimation* anim = AnimationReference(animid);
    if (anim->meshid >= NumMeshes()) return FALSE;
    MeshDescriptor* md = MeshReference(anim->meshid);
    size_t n = MeshVertexCount(md);
    return n > 0 && (size_t)md->skinstart + n <= NumSkins();
}

const char** AutoWeightLabels() {
    return g_autoweight_labels;
}

const char** BrushModeLabels() {
    return g_brush_labels;
}

WeightBrush* RigBrush() {
    return &g_brush;
}

float SkinWeight(const VertexSkin* skin, uint32_t bone) {
    float w = 0.0f;
    for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) if (skin->indices[k] == bone && skin->weights[k] > 0.0f) w += skin->weights[k];
    return w;
}

void RigNormalizeWeights() {
    MeshAnimation* anim = RigActive();
    MeshDescriptor* md = ActiveDescriptor();
    if (!anim || !md || !RigSkinsValid(RigActiveAnimation())) return;
    size_t n = MeshVertexCount(md);
    for (size_t v = 0; v < n; v++) {
        VertexSkin* s = SkinRef(v);
        uint32_t idx[MAX_BONE_INFLUENCES];
        float w[MAX_BONE_INFLUENCES];
        for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) {
            idx[k] = s->indices[k] < anim->skeleton.bonecount ? s->indices[k] : 0;
            w[k] = s->weights[k] > 0.0f ? s->weights[k] : 0.0f;
        }
        PackSkin(s, idx, w, MAX_BONE_INFLUENCES, 0);
    }
    UpdateSkins();
    loginfo("Normalized the weights of %zu vertices", n);
}

void RigCleanWeights(float threshold) {
    MeshAnimation* anim = RigActive();
    MeshDescriptor* md = ActiveDescriptor();
    if (!anim || !md || !RigSkinsValid(RigActiveAnimation())) return;
    size_t n = MeshVertexCount(md);
    size_t removed = 0;
    for (size_t v = 0; v < n; v++) {
        VertexSkin* s = SkinRef(v);
        uint32_t idx[MAX_BONE_INFLUENCES];
        float w[MAX_BONE_INFLUENCES];
        size_t strongest = 0;
        for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++) {
            idx[k] = s->indices[k];
            w[k] = s->weights[k];
            if (w[k] > w[strongest]) strongest = k;
        }
        for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++)
            if (k != strongest && w[k] > 0.0f && w[k] < threshold) { w[k] = 0.0f; removed++; }
        PackSkin(s, idx, w, MAX_BONE_INFLUENCES, idx[strongest]);
    }
    UpdateSkins();
    loginfo("Removed %zu weak influences (below %.3f)", removed, threshold);
}

void RigSmoothWeights(BOOL selectedonly, uint32_t iterations, float factor) {
    MeshAnimation* anim = RigActive();
    if (!anim || !RigSkinsValid(RigActiveAnimation())) return;
    WeldGraph* g = Graph();
    if (!g) return;
    size_t nb = anim->skeleton.bonecount;
    size_t nn = g->nnodes;
    BOOL* use = EZ_ALLOC(nb, sizeof(BOOL));
    size_t count = 0;
    for (size_t b = 0; b < nb; b++) {
        use[b] = selectedonly ? SelectedBit(b) : TRUE;
        count += use[b] ? 1 : 0;
    }
    if (count == 0) { EZ_FREE(use); logwarn("Select a bone to smooth its weights"); return; }
    factor = glm_clamp(factor, 0.0f, 1.0f);
    float* w = EZ_ALLOC(nn, sizeof(float));
    float* tmp = EZ_ALLOC(nn, sizeof(float));
    float* result = EZ_ALLOC(nn * nb, sizeof(float));
    for (size_t b = 0; b < nb; b++) {
        BOOL any = FALSE;
        for (size_t n = 0; n < nn; n++) {
            w[n] = SkinWeight(SkinRef(g->nodeverts[g->nodeptr[n]]), (uint32_t)b);
            any |= w[n] > 0.0f;
        }
        if (any && use[b]) {
            for (uint32_t it = 0; it < iterations; it++) {
                for (size_t n = 0; n < nn; n++) {
                    uint32_t s = g->adjptr[n], e = g->adjptr[n + 1];
                    if (e == s) { tmp[n] = w[n]; continue; }
                    float avg = 0.0f;
                    for (uint32_t k = s; k < e; k++) avg += w[g->adj[k]];
                    avg /= (float)(e - s);
                    tmp[n] = w[n] + factor * (avg - w[n]);
                }
                memcpy(w, tmp, nn * sizeof(float));
            }
        }
        for (size_t n = 0; n < nn; n++) result[n * nb + b] = w[n];
    }
    uint32_t* idx = EZ_ALLOC(nb, sizeof(uint32_t));
    for (size_t b = 0; b < nb; b++) idx[b] = (uint32_t)b;
    for (size_t n = 0; n < nn; n++) {
        VertexSkin packed;
        PackSkin(&packed, idx, &result[n * nb], nb, SkinRef(g->nodeverts[g->nodeptr[n]])->indices[0]);
        for (uint32_t k = g->nodeptr[n]; k < g->nodeptr[n + 1]; k++) *SkinRef(g->nodeverts[k]) = packed;
    }
    EZ_FREE(idx);
    EZ_FREE(result);
    EZ_FREE(tmp);
    EZ_FREE(w);
    EZ_FREE(use);
    UpdateSkins();
    loginfo("Smoothed weights (%u iterations)", iterations);
}

void RigMirrorWeights(MirrorDirection direction) {
    MeshAnimation* anim = RigActive();
    MeshDescriptor* md = ActiveDescriptor();
    if (!anim || !md || !RigSkinsValid(RigActiveAnimation())) return;
    size_t n = MeshVertexCount(md);
    Geometry* geo = RendererGeometry();
    vec3 mn = { FLT_MAX, FLT_MAX, FLT_MAX }, mx = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t v = 0; v < n; v++) {
        float* p = geo->vertices.data[md->start + v];
        vec3 q = { p[0], p[1], p[2] };
        glm_vec3_minv(q, mn, mn);
        glm_vec3_maxv(q, mx, mx);
    }
    float extent = glm_vec3_distance(mn, mx);
    float cell = extent > 0.0f ? extent * 1e-3f : 1e-3f;
    float tolerance = cell * 0.5f;
    PointHash h = { 0 };
    h.capacity = n * 2 + 1;
    h.keys = EZ_ALLOC(h.capacity, sizeof(int64_t));
    h.values = EZ_ALLOC(h.capacity, sizeof(uint32_t));
    for (size_t i = 0; i < h.capacity; i++) h.values[i] = (uint32_t)-1;
    float sign = direction == MIRROR_POSITIVE_TO_NEGATIVE ? 1.0f : -1.0f;
    for (size_t v = 0; v < n; v++) {
        float* p = geo->vertices.data[md->start + v];
        if (p[0] * sign <= tolerance) continue;
        HashInsert(&h, CellKey((int64_t)floorf(p[0] / cell), (int64_t)floorf(p[1] / cell), (int64_t)floorf(p[2] / cell)), (uint32_t)v);
    }
    uint32_t* map = EZ_ALLOC(anim->skeleton.bonecount, sizeof(uint32_t));
    for (size_t b = 0; b < anim->skeleton.bonecount; b++) map[b] = (uint32_t)MirrorBone(&(anim->skeleton), b);
    size_t copied = 0;
    for (size_t v = 0; v < n; v++) {
        float* p = geo->vertices.data[md->start + v];
        if (p[0] * sign >= -tolerance) continue;
        vec3 m = { -p[0], p[1], p[2] };
        int64_t cx = (int64_t)floorf(m[0] / cell), cy = (int64_t)floorf(m[1] / cell), cz = (int64_t)floorf(m[2] / cell);
        uint32_t best = (uint32_t)-1;
        float bestd = tolerance * 4.0f;
        for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) {
            int64_t key = CellKey(cx + dx, cy + dy, cz + dz);
            size_t i = (size_t)((uint64_t)key * 11400714819323198485ull) % h.capacity;
            while (h.values[i] != (uint32_t)-1) {
                if (h.keys[i] == key) {
                    float* q = geo->vertices.data[md->start + h.values[i]];
                    vec3 qq = { q[0], q[1], q[2] };
                    float d = glm_vec3_distance(qq, m);
                    if (d < bestd) { bestd = d; best = h.values[i]; }
                }
                i = (i + 1) % h.capacity;
            }
        }
        if (best == (uint32_t)-1) continue;
        VertexSkin src = *SkinRef(best);
        for (size_t k = 0; k < MAX_BONE_INFLUENCES; k++)
            if (src.indices[k] < anim->skeleton.bonecount) src.indices[k] = map[src.indices[k]];
        *SkinRef(v) = src;
        copied++;
    }
    EZ_FREE(map);
    EZ_FREE(h.keys);
    EZ_FREE(h.values);
    UpdateSkins();
    loginfo("Mirrored the weights of %zu vertices", copied);
}

void RigMeshDeleted(size_t meshid) {
    if (g_job.running && g_job.mesh == meshid) RigAutoWeightsCancel();
    else if (g_job.running && g_job.mesh > meshid) g_job.mesh--;
    if (g_mesh == (size_t)-1) return;
    if (g_mesh == meshid) {
        g_mesh = (size_t)-1;
        RigClearSelection();
    } else if (g_mesh > meshid) {
        g_mesh--;
    }
    InvalidateRest();
}

BOOL RigAutoWeightsRunning() {
    return g_job.running;
}

float RigAutoWeightsProgress() {
    if (!g_job.running || g_job.tosolve == 0) return 0.0f;
    float inner = fminf((float)g_job.iteration / 40.0f, 0.95f);
    return 100.0f * fminf(1.0f, ((float)g_job.solved + inner) / (float)g_job.tosolve);
}

void RigAutoWeightsCancel() {
    if (g_job.running) loginfo("Automatic weights cancelled");
    FreeJob();
}

BOOL RigAutoWeightsBegin(AutoWeightMethod method, BOOL selectedonly) {
    RigAutoWeightsCancel();
    MeshAnimation* anim = RigActive();
    MeshDescriptor* md = ActiveDescriptor();
    size_t aidx = RigActiveAnimation();
    if (!anim || !md || !RigSkinsValid(aidx)) {
        logerror("The active object has no valid rig to weight");
        return FALSE;
    }
    WeldGraph* g = Graph();
    if (!g) return FALSE;
    Skeleton* sk = &(anim->skeleton);
    size_t nb = 0;
    uint32_t bones[MAX_BONES];
    for (size_t b = 0; b < sk->bonecount; b++) if (sk->bones[b].flags & BONE_FLAG_DEFORM) bones[nb++] = (uint32_t)b;
    if (nb == 0) {
        logerror("The rig has no deforming bones");
        return FALSE;
    }
    BOOL anysel = FALSE;
    for (size_t b = 0; b < nb; b++) anysel |= SelectedBit(bones[b]);
    if (selectedonly && !anysel) {
        logwarn("No deforming bone is selected");
        return FALSE;
    }
    Geometry* geo = RendererGeometry();
    size_t nn = g->nnodes;
    g_job.anim = aidx;
    g_job.mesh = g_mesh;
    g_job.method = method;
    g_job.selectedonly = selectedonly;
    g_job.nnodes = nn;
    g_job.nbones = nb;
    g_job.bones = EZ_ALLOC(nb, sizeof(uint32_t));
    g_job.solve = EZ_ALLOC(nb, sizeof(BOOL));
    memcpy(g_job.bones, bones, nb * sizeof(uint32_t));
    g_job.tosolve = 0;
    for (size_t b = 0; b < nb; b++) {
        g_job.solve[b] = !selectedonly || SelectedBit(bones[b]);
        g_job.tosolve += g_job.solve[b] ? 1 : 0;
    }
    static mat4 rest[MAX_BONES];
    SkeletonRestGlobals(sk, rest);
    vec3* heads = EZ_ALLOC(nb, sizeof(vec3));
    vec3* tails = EZ_ALLOC(nb, sizeof(vec3));
    for (size_t b = 0; b < nb; b++) {
        mat4 m;
        M4Copy(m, rest[bones[b]]);
        vec3 t;
        glm_vec3_copy(sk->bones[bones[b]].tail, t);
        M4Head(m, heads[b]);
        glm_mat4_mulv3(m, t, 1.0f, tails[b]);
    }
    vec3* pos = EZ_ALLOC(nn, sizeof(vec3));
    for (size_t n = 0; n < nn; n++) {
        float* p = geo->vertices.data[md->start + g->nodeverts[g->nodeptr[n]]];
        SETVEC3(pos[n], p[0], p[1], p[2]);
    }
    vec3 mn = { FLT_MAX, FLT_MAX, FLT_MAX }, mx = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t n = 0; n < nn; n++) { glm_vec3_minv(pos[n], mn, mn); glm_vec3_maxv(pos[n], mx, mx); }
    float extent = fmaxf(glm_vec3_distance(mn, mx), 1e-6f);
    g_job.nearest = EZ_ALLOC(nn, sizeof(uint32_t));
    g_job.result = EZ_ALLOC(nn * nb, sizeof(float));
    float* dmin = EZ_ALLOC(nn, sizeof(float));
    for (size_t n = 0; n < nn; n++) {
        float best = FLT_MAX;
        for (size_t b = 0; b < nb; b++) {
            float d = PointSegmentDistance(pos[n], heads[b], tails[b]);
            if (d < best) { best = d; g_job.nearest[n] = (uint32_t)b; }
        }
        dmin[n] = fmaxf(best, extent * 1e-4f);
    }
    if (method == AUTOWEIGHT_ENVELOPE) {
        for (size_t n = 0; n < nn; n++) {
            float total = 0.0f;
            for (size_t b = 0; b < nb; b++) {
                float d = fmaxf(PointSegmentDistance(pos[n], heads[b], tails[b]), extent * 1e-4f);
                float w = 0.0f;
                if (d <= dmin[n] * 2.0f) {
                    float r = dmin[n] / d;
                    w = r * r * r * r;
                }
                g_job.result[n * nb + b] = w;
                total += w;
            }
            if (total > 0.0f) for (size_t b = 0; b < nb; b++) g_job.result[n * nb + b] /= total;
        }
        EZ_FREE(dmin);
        EZ_FREE(pos);
        EZ_FREE(heads);
        EZ_FREE(tails);
        g_job.running = TRUE;
        FinishAutoWeights();
        return TRUE;
    }
    HeatLevel* fine = &(g_job.levels[0]);
    fine->n = nn;
    fine->rowptr = EZ_ALLOC(nn + 1, sizeof(uint32_t));
    for (size_t n = 0; n < nn; n++) fine->rowptr[n + 1] = fine->rowptr[n] + (g->adjptr[n + 1] - g->adjptr[n]);
    size_t nnz = fine->rowptr[nn];
    fine->cols = EZ_ALLOC(nnz + 1, sizeof(uint32_t));
    fine->vals = EZ_ALLOC(nnz + 1, sizeof(double));
    memcpy(fine->cols, g->adj, nnz * sizeof(uint32_t));
    double* mass = EZ_ALLOC(nn, sizeof(double));
    size_t tcount = MeshTriangleCount(md);
    double edgesum = 0.0;
    size_t edgecount = 0;
    for (size_t t = 0; t < tcount; t++) {
        Triangle* tri = TriangleReference(md->tstart + t);
        uint32_t v[3] = { tri->a, tri->b, tri->c };
        uint32_t node[3];
        BOOL ok = TRUE;
        for (int k = 0; k < 3; k++) {
            if (v[k] < md->start || v[k] - md->start >= g->vcount) { ok = FALSE; break; }
            node[k] = g->v2n[v[k] - md->start];
        }
        if (!ok || node[0] == node[1] || node[1] == node[2] || node[0] == node[2]) continue;
        vec3 e1, e2, c;
        glm_vec3_sub(pos[node[1]], pos[node[0]], e1);
        glm_vec3_sub(pos[node[2]], pos[node[0]], e2);
        glm_vec3_cross(e1, e2, c);
        double area2 = glm_vec3_norm(c);
        if (area2 < 1e-20) continue;
        for (int k = 0; k < 3; k++) mass[node[k]] += area2 / 6.0;
        for (int k = 0; k < 3; k++) {
            uint32_t a = node[(k + 1) % 3], b = node[(k + 2) % 3];
            vec3 u, w;
            glm_vec3_sub(pos[a], pos[node[k]], u);
            glm_vec3_sub(pos[b], pos[node[k]], w);
            double half = 0.5 * glm_vec3_dot(u, w) / area2;
            for (uint32_t q = fine->rowptr[a]; q < fine->rowptr[a + 1]; q++) if (fine->cols[q] == b) { fine->vals[q] += half; break; }
            for (uint32_t q = fine->rowptr[b]; q < fine->rowptr[b + 1]; q++) if (fine->cols[q] == a) { fine->vals[q] += half; break; }
            edgesum += glm_vec3_distance(pos[a], pos[b]);
            edgecount++;
        }
    }
    double avg = 0.0;
    for (size_t q = 0; q < nnz; q++) avg += fabs(fine->vals[q]);
    avg = nnz ? avg / (double)nnz : 1.0;
    for (size_t q = 0; q < nnz; q++) fine->vals[q] = fmax(fine->vals[q], 0.0) + 1e-3 * avg;
    double massavg = 0.0;
    for (size_t n = 0; n < nn; n++) massavg += mass[n];
    massavg = nn ? massavg / (double)nn : 1.0;
    fine->diag = EZ_ALLOC(nn, sizeof(double));
    fine->mh = EZ_ALLOC(nn, sizeof(double));
    for (size_t n = 0; n < nn; n++) {
        double m = mass[n] > 1e-12 * massavg ? mass[n] : fmax(massavg, 1e-12);
        double h = 1.0 / ((double)dmin[n] * (double)dmin[n]);
        fine->mh[n] = m * h;
        double sum = 0.0;
        for (uint32_t q = fine->rowptr[n]; q < fine->rowptr[n + 1]; q++) sum += fine->vals[q];
        fine->diag[n] = sum + fine->mh[n];
    }
    LevelAllocVectors(fine);
    EZ_FREE(mass);
    g_job.nlevels = 1;
    float cell = edgecount ? (float)(edgesum / (double)edgecount) * 2.0f : extent * 0.01f;
    vec3* lpos = pos;
    while (g_job.nlevels < HEAT_MAX_LEVELS && g_job.levels[g_job.nlevels - 1].n > HEAT_COARSE_TARGET) {
        vec3* cpos = NULL;
        if (!CoarsenLevel(&(g_job.levels[g_job.nlevels - 1]), lpos, cell, &(g_job.levels[g_job.nlevels]), &cpos)) {
            cell *= 2.0f;
            if (cell > extent) break;
            continue;
        }
        LevelAllocVectors(&(g_job.levels[g_job.nlevels]));
        if (lpos != pos) EZ_FREE(lpos);
        lpos = cpos;
        g_job.nlevels++;
        cell *= 2.0f;
    }
    if (lpos != pos) EZ_FREE(lpos);
    EZ_FREE(dmin);
    EZ_FREE(pos);
    EZ_FREE(heads);
    EZ_FREE(tails);
    g_job.current = 0;
    g_job.started = FALSE;
    g_job.iteration = 0;
    g_job.solved = 0;
    g_job.running = TRUE;
    loginfo("Generating bone heat weights for %zu surface points and %zu bones (%zu levels)...", nn, g_job.tosolve, g_job.nlevels);
    return TRUE;
}

void RigAutoWeightsTick(double budget) {
    if (!g_job.running) return;
    if (g_job.anim >= NumAnimations() || g_job.mesh >= NumMeshes() || RigOfMesh(g_job.mesh) != g_job.anim || !Graph() || g_mesh != g_job.mesh) {
        logwarn("The rig changed while generating weights - cancelled");
        FreeJob();
        return;
    }
    double start = GetTime();
    size_t nn = g_job.nnodes, nb = g_job.nbones;
    HeatLevel* l = &(g_job.levels[0]);
    while (g_job.current < nb && GetTime() - start < budget) {
        size_t b = g_job.current;
        if (!g_job.solve[b]) { g_job.current++; continue; }
        if (!g_job.started) {
            BOOL any = FALSE;
            for (size_t n = 0; n < nn && !any; n++) any = g_job.nearest[n] == b;
            if (!any) {
                for (size_t n = 0; n < nn; n++) g_job.result[n * nb + b] = 0.0f;
                g_job.current++;
                g_job.solved++;
                continue;
            }
            HeatStart(b);
        }
        BOOL done = g_job.bnorm <= 0.0;
        for (int burst = 0; burst < 4 && !done; burst++) {
            LevelSpMV(l, l->p, l->ap);
            double pap = Dot(l->p, l->ap, l->n);
            if (!(fabs(pap) > 1e-300)) { done = TRUE; break; }
            double alpha = g_job.rz / pap;
            for (size_t n = 0; n < l->n; n++) {
                l->x[n] += alpha * l->p[n];
                l->r[n] -= alpha * l->ap[n];
            }
            g_job.iteration++;
            double rnorm = sqrt(Dot(l->r, l->r, l->n));
            if (rnorm <= HEAT_TOLERANCE * g_job.bnorm || g_job.iteration >= HEAT_MAX_ITERATIONS) { done = TRUE; break; }
            Precondition(l->r, l->z);
            double rz = Dot(l->r, l->z, l->n);
            double beta = rz / g_job.rz;
            g_job.rz = rz;
            for (size_t n = 0; n < l->n; n++) l->p[n] = l->z[n] + beta * l->p[n];
        }
        if (!done) continue;
        for (size_t n = 0; n < nn; n++) g_job.result[n * nb + b] = (float)l->x[n];
        g_job.started = FALSE;
        g_job.current++;
        g_job.solved++;
    }
    if (g_job.current >= nb) FinishAutoWeights();
}

BOOL RigBrushHit(vec3 world, vec3 normal) {
    if (!g_hit_valid) return FALSE;
    glm_vec3_copy(g_hit, world);
    glm_vec3_copy(g_hit_normal, normal);
    return TRUE;
}

BOOL RigWeightOverlay(uint32_t* bone, uint32_t* tstart, uint32_t* tend, uint32_t* vstart, uint32_t* skinstart) {
    if (g_mode != RIG_MODE_WEIGHTS || !RigViewportActive()) return FALSE;
    MeshDescriptor* md = ActiveDescriptor();
    size_t b = RigActiveBone();
    if (!md || md->disabled || b == (size_t)-1 || !RigSkinsValid(RigActiveAnimation()) || MeshTriangleCount(md) == 0) return FALSE;
    *bone = (uint32_t)b;
    *tstart = md->tstart;
    *tend = md->tend;
    *vstart = md->start;
    *skinstart = md->skinstart;
    return TRUE;
}

BOOL RigPaint(const ViewProjection* vp, const vec2 mouse, BOOL apply, BOOL invert, float dt) {
    g_hit_valid = FALSE;
    MeshAnimation* anim = RigActive();
    MeshDescriptor* md = ActiveDescriptor();
    size_t aidx = RigActiveAnimation();
    if (!anim || !md || md->disabled || !RigSkinsValid(aidx)) return FALSE;
    size_t n = MeshVertexCount(md), tcount = MeshTriangleCount(md);
    if (tcount == 0) return FALSE;
    SkinnedPositions(md, aidx);
    vec3 origin, dir;
    ViewRay(vp, mouse, origin, dir);
    float best = FLT_MAX;
    size_t besttri = (size_t)-1;
    for (size_t t = 0; t < tcount; t++) {
        Triangle* tri = TriangleReference(md->tstart + t);
        uint32_t a = tri->a - md->start, b = tri->b - md->start, c = tri->c - md->start;
        if (a >= n || b >= n || c >= n) continue;
        float tt;
        if (RayTriangle(origin, dir, g_skinned[a], g_skinned[b], g_skinned[c], &tt) && tt < best) { best = tt; besttri = t; }
    }
    if (besttri == (size_t)-1) return FALSE;
    g_hit_valid = TRUE;
    glm_vec3_scale(dir, best, g_hit);
    glm_vec3_add(origin, g_hit, g_hit);
    {
        Triangle* tri = TriangleReference(md->tstart + besttri);
        vec3 e1, e2;
        glm_vec3_sub(g_skinned[tri->b - md->start], g_skinned[tri->a - md->start], e1);
        glm_vec3_sub(g_skinned[tri->c - md->start], g_skinned[tri->a - md->start], e2);
        glm_vec3_cross(e1, e2, g_hit_normal);
        glm_vec3_normalize(g_hit_normal);
        if (glm_vec3_dot(g_hit_normal, dir) > 0.0f) glm_vec3_negate(g_hit_normal);
    }
    size_t bone = RigActiveBone();
    if (!apply || bone == (size_t)-1) return TRUE;
    WeldGraph* g = Graph();
    if (!g) return TRUE;
    BrushMode mode = g_brush.mode;
    if (invert) mode = mode == BRUSH_ADD ? BRUSH_SUBTRACT : (mode == BRUSH_SUBTRACT ? BRUSH_ADD : mode);
    float radius = fmaxf(g_brush.radius, 1e-6f);
    float rate = fminf(1.0f, fmaxf(dt, 1.0f / 240.0f) * 12.0f);
    uint32_t fallback = WeightFallback(anim, (uint32_t)bone);
    BOOL changed = FALSE;
    for (size_t node = 0; node < g->nnodes; node++) {
        uint32_t v = g->nodeverts[g->nodeptr[node]];
        float d = glm_vec3_distance(g_skinned[v], g_hit);
        if (d > radius) continue;
        if (g_brush.frontonly) {
            vec3 nsum = { 0, 0, 0 };
            for (uint32_t k = g->nodeptr[node]; k < g->nodeptr[node + 1]; k++) glm_vec3_add(nsum, g_skinned_normals[g->nodeverts[k]], nsum);
            if (glm_vec3_dot(nsum, dir) > 0.0f) continue;
        }
        float x = d / radius;
        float falloff = (1.0f - x * x) * (1.0f - x * x);
        float amount = glm_clamp(g_brush.strength * falloff * rate, 0.0f, 1.0f);
        VertexSkin* s = SkinRef(v);
        float w = SkinWeight(s, (uint32_t)bone);
        float target = w;
        switch (mode) {
            case BRUSH_ADD: target = w + (1.0f - w) * amount; break;
            case BRUSH_SUBTRACT: target = w * (1.0f - amount); break;
            case BRUSH_REPLACE: target = w + (g_brush.weight - w) * amount; break;
            case BRUSH_SMOOTH: {
                uint32_t a0 = g->adjptr[node], a1 = g->adjptr[node + 1];
                if (a1 == a0) break;
                float avg = 0.0f;
                for (uint32_t k = a0; k < a1; k++) avg += SkinWeight(SkinRef(g->nodeverts[g->nodeptr[g->adj[k]]]), (uint32_t)bone);
                avg /= (float)(a1 - a0);
                target = w + (avg - w) * amount;
                break;
            }
            default: break;
        }
        if (fabsf(target - w) < 1e-6f) continue;
        VertexSkin updated = *s;
        SetSkinWeight(&updated, (uint32_t)bone, target, fallback);
        for (uint32_t k = g->nodeptr[node]; k < g->nodeptr[node + 1]; k++) *SkinRef(g->nodeverts[k]) = updated;
        changed = TRUE;
    }
    if (changed) UpdateSkins();
    return TRUE;
}

void RigReleaseCaches() {
    RigAutoWeightsCancel();
    FreeGraph();
    if (g_skinned) EZ_FREE(g_skinned);
    if (g_skinned_normals) EZ_FREE(g_skinned_normals);
    g_skinned = NULL;
    g_skinned_normals = NULL;
    g_skinned_cap = 0;
    g_hit_valid = FALSE;
}

size_t RigAddIK(size_t bone, size_t target) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return (size_t)-1;
    if (anim->numik >= MAX_IK_CONSTRAINTS) {
        logerror("A rig can hold at most %d IK constraints", MAX_IK_CONSTRAINTS);
        return (size_t)-1;
    }
    IKConstraint ik = { 0 };
    ik.enabled = 1;
    ik.bone = (uint32_t)bone;
    ik.target = target < anim->skeleton.bonecount ? (uint32_t)target : IK_NO_BONE;
    ik.pole = IK_NO_BONE;
    ik.chain = 2;
    ik.iterations = 16;
    ik.influence = 1.0f;
    anim->ik[anim->numik++] = ik;
    anim->dirty = TRUE;
    return anim->numik - 1;
}

void RigRemoveIK(size_t index) {
    MeshAnimation* anim = RigActive();
    if (!anim || index >= anim->numik) return;
    memmove(&(anim->ik[index]), &(anim->ik[index + 1]), (anim->numik - index - 1) * sizeof(IKConstraint));
    anim->numik--;
    anim->dirty = TRUE;
}

size_t RigFindIK(size_t bone) {
    MeshAnimation* anim = RigActive();
    if (!anim) return (size_t)-1;
    for (uint32_t i = 0; i < anim->numik; i++) if (anim->ik[i].bone == bone) return i;
    return (size_t)-1;
}

size_t RigCreateIKWithTarget(size_t bone, uint32_t chain) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return (size_t)-1;
    Skeleton* sk = &(anim->skeleton);
    size_t chainroot = bone;
    for (uint32_t i = 1; chain == 0 || i < chain; i++) {
        size_t p = sk->bones[chainroot].parent;
        if (p >= sk->bonecount) break;
        chainroot = p;
    }
    size_t root = bone;
    while (sk->bones[root].parent < sk->bonecount) root = sk->bones[root].parent;
    size_t parent = (chain == 0 || chainroot == root) ? (size_t)-1 : root;
    vec3 head, tail, dir, ttail;
    RigRestHeadTail(bone, head, tail);
    glm_vec3_sub(tail, head, dir);
    float len = glm_vec3_norm(dir) * 0.5f;
    vec3 up = { 0, 1, 0 };
    if (len < 1e-6f) len = 0.05f;
    glm_vec3_scale(up, len, ttail);
    glm_vec3_add(tail, ttail, ttail);
    char name[512];
    snprintf(name, sizeof(name), "%s.IK", sk->bones[bone].name);
    size_t target = RigAddBone(parent, tail, ttail, name);
    if (target == (size_t)-1) return (size_t)-1;
    sk->bones[target].flags &= ~(BONE_FLAG_DEFORM | BONE_FLAG_CONNECTED);
    size_t c = RigAddIK(bone, target);
    if (c != (size_t)-1) anim->ik[c].chain = chain;
    loginfo("Added IK on \"%s\" (chain of %u) with target \"%s\"", sk->bones[bone].name, chain, sk->bones[target].name);
    return c;
}

BOOL RigBoneWorld(size_t bone, vec3 head, vec3 tail) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return FALSE;
    mat4 g, transform;
    BoneFrameMesh(anim, bone, g);
    RigMeshTransform(transform);
    vec3 h, t, bt;
    glm_vec3_copy(anim->skeleton.bones[bone].tail, bt);
    M4Head(g, h);
    glm_mat4_mulv3(g, bt, 1.0f, t);
    glm_mat4_mulv3(transform, h, 1.0f, head);
    glm_mat4_mulv3(transform, t, 1.0f, tail);
    return TRUE;
}

BOOL RigBoneWorldAxes(size_t bone, vec3 axes[3]) {
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return FALSE;
    mat4 g, transform, w;
    BoneFrameMesh(anim, bone, g);
    RigMeshTransform(transform);
    glm_mat4_mul(transform, g, w);
    for (int i = 0; i < 3; i++) {
        glm_vec3_copy(w[i], axes[i]);
        if (glm_vec3_norm(axes[i]) < 1e-12f) SETVEC3(axes[i], i == 0, i == 1, i == 2);
        glm_vec3_normalize(axes[i]);
    }
    return TRUE;
}

size_t RigPickBone(const ViewProjection* vp, const vec2 mouse, float maxpx) {
    MeshAnimation* anim = RigActive();
    if (!anim) return (size_t)-1;
    size_t best = (size_t)-1;
    float bestd = maxpx;
    float bestdepth = FLT_MAX;
    for (size_t b = 0; b < anim->skeleton.bonecount; b++) {
        if (anim->skeleton.bones[b].flags & BONE_FLAG_HIDDEN) continue;
        vec3 head, tail;
        if (!RigBoneWorld(b, head, tail)) continue;
        vec2 h2, t2;
        float dh, dt;
        if (!ViewProject(vp, head, h2, &dh) || !ViewProject(vp, tail, t2, &dt)) continue;
        float d = DistToSegment2D(mouse, h2, t2);
        float depth = fminf(dh, dt);
        if (d < bestd - 1.0f || (d < bestd + 1.0f && depth < bestdepth)) {
            bestd = d;
            bestdepth = depth;
            best = b;
        }
    }
    return best;
}

BOOL RigBoneWorldOf(size_t animid, size_t bone, vec3 head, vec3 tail) {
    if (animid == RigActiveAnimation()) return RigBoneWorld(bone, head, tail);
    if (animid >= NumAnimations()) return FALSE;
    MeshAnimation* anim = AnimationReference(animid);
    if (bone >= anim->skeleton.bonecount || anim->meshid >= NumMeshes()) return FALSE;
    mat4 g, transform;
    BoneGlobal(anim, bone, g);
    M4Copy(transform, MeshReference(anim->meshid)->transform);
    vec3 h, t, bt;
    glm_vec3_copy(anim->skeleton.bones[bone].tail, bt);
    M4Head(g, h);
    glm_mat4_mulv3(g, bt, 1.0f, t);
    glm_mat4_mulv3(transform, h, 1.0f, head);
    glm_mat4_mulv3(transform, t, 1.0f, tail);
    return TRUE;
}

BOOL RigBoneWorldAxesOf(size_t animid, size_t bone, vec3 axes[3]) {
    if (animid == RigActiveAnimation()) return RigBoneWorldAxes(bone, axes);
    if (animid >= NumAnimations()) return FALSE;
    MeshAnimation* anim = AnimationReference(animid);
    if (bone >= anim->skeleton.bonecount || anim->meshid >= NumMeshes()) return FALSE;
    mat4 g, transform, w;
    BoneGlobal(anim, bone, g);
    M4Copy(transform, MeshReference(anim->meshid)->transform);
    glm_mat4_mul(transform, g, w);
    for (int i = 0; i < 3; i++) {
        glm_vec3_copy(w[i], axes[i]);
        if (glm_vec3_norm(axes[i]) < 1e-12f) SETVEC3(axes[i], i == 0, i == 1, i == 2);
        glm_vec3_normalize(axes[i]);
    }
    return TRUE;
}

size_t RigPickAnyBone(const ViewProjection* vp, const vec2 mouse, float maxpx, size_t* animid) {
    size_t best = (size_t)-1;
    float bestd = maxpx, bestdepth = FLT_MAX;
    size_t active = RigActiveAnimation();
    for (size_t a = 0; a < NumAnimations(); a++) {
        if (!RigVisible(a) && a != active) continue;
        MeshAnimation* anim = AnimationReference(a);
        for (size_t b = 0; b < anim->skeleton.bonecount; b++) {
            if (anim->skeleton.bones[b].flags & BONE_FLAG_HIDDEN) continue;
            vec3 head, tail;
            vec2 h2, t2;
            float dh, dt;
            if (!RigBoneWorldOf(a, b, head, tail)) continue;
            if (!ViewProject(vp, head, h2, &dh) || !ViewProject(vp, tail, t2, &dt)) continue;
            float d = DistToSegment2D(mouse, h2, t2);
            float depth = fminf(dh, dt);
            if (a == active) d -= 0.5f;
            if (d < bestd - 1.0f || (d < bestd + 1.0f && depth < bestdepth)) {
                bestd = d;
                bestdepth = depth;
                best = b;
                *animid = a;
            }
        }
    }
    return best;
}

void RigSnapshotTake(size_t bone, RigBoneSnapshot* snap) {
    memset(snap, 0, sizeof(RigBoneSnapshot));
    MeshAnimation* anim = RigActive();
    if (!anim || bone >= anim->skeleton.bonecount) return;
    snap->valid = TRUE;
    snap->bone = bone;
    snap->pose = g_mode != RIG_MODE_EDIT;
    snap->handle = g_handle;
    snap->posed = BoneIsPosed(anim, bone);
    BlobCopy(&(snap->override), &(anim->overrides[bone]), sizeof(BonePose));
    BoneCurrentPose(anim, bone, &(snap->current));
    BoneGlobal(anim, bone, snap->global);
    size_t p = anim->skeleton.bones[bone].parent;
    if (p < anim->skeleton.bonecount) BoneGlobal(anim, p, snap->parent);
    else glm_mat4_identity(snap->parent);
    RigRestHeadTail(bone, snap->head, snap->tail);
    snap->roll = RigBoneRoll(bone);
    snap->parentbone = p < anim->skeleton.bonecount ? p : (size_t)-1;
    if (snap->parentbone != (size_t)-1) {
        RigRestHeadTail(p, snap->parenthead, snap->parenttail);
        snap->parentroll = RigBoneRoll(p);
    }
}

void RigSnapshotRestore(const RigBoneSnapshot* snap) {
    MeshAnimation* anim = RigActive();
    if (!snap->valid || !anim || snap->bone >= anim->skeleton.bonecount) return;
    if (snap->pose) {
        BlobCopy(&(anim->overrides[snap->bone]), &(snap->override), sizeof(BonePose));
        if (snap->posed) BoneSetPose(anim, snap->bone, &(snap->override));
        else BoneClearOverride(anim, snap->bone);
    } else {
        RigSetBoneRest(snap->bone, snap->head, snap->tail, snap->roll);
        if (snap->parentbone != (size_t)-1) RigSetBoneRest(snap->parentbone, snap->parenthead, snap->parenttail, snap->parentroll);
    }
}

void RigApplyTranslate(const RigBoneSnapshot* snap, const vec3 world_delta) {
    MeshAnimation* anim = RigActive();
    if (!snap->valid || !anim) return;
    mat4 to_world, to_mesh;
    mat3 m3;
    MeshSpace(to_world, to_mesh, m3);
    vec3 d;
    glm_mat3_mulv(m3, (float*)world_delta, d);
    if (snap->pose) {
        mat4 g, t;
        M4Copy(g, snap->global);
        glm_translate_make(t, d);
        glm_mat4_mul(t, g, g);
        SetPoseFromGlobal(anim, snap->bone, snap->parent, g);
        return;
    }
    vec3 head, tail;
    glm_vec3_copy((float*)snap->head, head);
    glm_vec3_copy((float*)snap->tail, tail);
    if (snap->handle != EDIT_HANDLE_TAIL) glm_vec3_add(head, d, head);
    if (snap->handle != EDIT_HANDLE_HEAD) glm_vec3_add(tail, d, tail);
    if (snap->parentbone != (size_t)-1) RigSetBoneRest(snap->parentbone, snap->parenthead, snap->parenttail, snap->parentroll);
    RigSetBoneRest(snap->bone, head, tail, snap->roll);
}

void RigApplyRotate(const RigBoneSnapshot* snap, const vec3 world_axis, float angle) {
    MeshAnimation* anim = RigActive();
    if (!snap->valid || !anim) return;
    mat4 to_world, to_mesh;
    mat3 m3;
    MeshSpace(to_world, to_mesh, m3);
    vec3 axis;
    glm_mat3_mulv(m3, (float*)world_axis, axis);
    if (glm_vec3_norm(axis) < 1e-12f || angle == 0.0f) {
        RigSnapshotRestore(snap);
        return;
    }
    glm_vec3_normalize(axis);
    if (glm_mat4_det(to_world) < 0.0f) angle = -angle;
    mat4 r;
    glm_rotate_make(r, angle, axis);
    if (snap->pose) {
        mat4 g;
        M4Copy(g, snap->global);
        vec3 head;
        M4Head(g, head);
        g[3][0] = 0.0f; g[3][1] = 0.0f; g[3][2] = 0.0f;
        glm_mat4_mul(r, g, g);
        g[3][0] = head[0]; g[3][1] = head[1]; g[3][2] = head[2];
        SetPoseFromGlobal(anim, snap->bone, snap->parent, g);
        return;
    }
    mat4 frame;
    RigBoneFrame(snap->head, snap->tail, snap->roll, frame);
    frame[3][0] = 0.0f; frame[3][1] = 0.0f; frame[3][2] = 0.0f;
    glm_mat4_mul(r, frame, frame);
    vec3 dir = { frame[1][0], frame[1][1], frame[1][2] };
    float len = glm_vec3_distance((float*)snap->head, (float*)snap->tail);
    vec3 tail;
    glm_vec3_scale(dir, len, tail);
    glm_vec3_add((float*)snap->head, tail, tail);
    float roll = FrameRoll(frame);
    RigSetBoneRest(snap->bone, snap->head, tail, roll);
}

void RigApplyScale(const RigBoneSnapshot* snap, const vec3 scale) {
    MeshAnimation* anim = RigActive();
    if (!snap->valid || !anim) return;
    if (snap->pose) {
        BonePose pose = snap->current;
        for (int i = 0; i < 3; i++) pose.scale[i] = snap->current.scale[i] * scale[i];
        BoneSetPose(anim, snap->bone, &pose);
        return;
    }
    float s = scale[1];
    if (fabsf(s - 1.0f) < 1e-6f) s = (scale[0] + scale[2]) * 0.5f;
    vec3 dir, tail;
    glm_vec3_sub((float*)snap->tail, (float*)snap->head, dir);
    glm_vec3_scale(dir, fmaxf(s, 1e-3f), dir);
    glm_vec3_add((float*)snap->head, dir, tail);
    RigSetBoneRest(snap->bone, snap->head, tail, snap->roll);
}

void RigSnapshotCommit(const RigBoneSnapshot* snap) {
    MeshAnimation* anim = RigActive();
    if (!snap->valid || !anim || !snap->pose) return;
    if (Timeline()->autokey) KeyInsert(anim, snap->bone, Timeline()->frame, KEY_CHANNEL_ALL);
}

void RigResetPose(BOOL selectedonly, uint32_t channels) {
    MeshAnimation* anim = RigActive();
    if (!anim) return;
    for (size_t b = 0; b < anim->skeleton.bonecount; b++) {
        if (selectedonly && !SelectedBit(b)) continue;
        BonePose pose, identity;
        BoneCurrentPose(anim, b, &pose);
        PoseIdentity(&identity);
        if (channels & KEY_CHANNEL_LOCATION) glm_vec3_copy(identity.location, pose.location);
        if (channels & KEY_CHANNEL_ROTATION) memcpy(pose.rotation, identity.rotation, sizeof(pose.rotation));
        if (channels & KEY_CHANNEL_SCALE) glm_vec3_copy(identity.scale, pose.scale);
        BoneSetPose(anim, b, &pose);
        if (Timeline()->autokey) KeyInsert(anim, b, Timeline()->frame, channels);
    }
}

void RigKeySelected(uint32_t channels) {
    MeshAnimation* anim = RigActive();
    if (!anim) return;
    size_t count = RigSelectedCount();
    size_t keyed = 0;
    for (size_t b = 0; b < anim->skeleton.bonecount; b++) {
        if (count > 0 && !SelectedBit(b)) continue;
        KeyInsert(anim, b, Timeline()->frame, channels);
        keyed++;
    }
    loginfo("Keyed %zu bone%s at frame %.0f", keyed, keyed == 1 ? "" : "s", Timeline()->frame);
}

void RigCopyPose() {
    MeshAnimation* anim = RigActive();
    if (!anim) return;
    size_t count = RigSelectedCount();
    g_clipboard_count = 0;
    for (size_t b = 0; b < anim->skeleton.bonecount; b++) {
        if (count > 0 && !SelectedBit(b)) continue;
        BonePose pose;
        BoneCurrentPose(anim, b, &pose);
        g_clipboard[g_clipboard_count] = pose;
        memcpy(g_clipboard_names[g_clipboard_count], anim->skeleton.bones[b].name, 256);
        g_clipboard_count++;
    }
    loginfo("Copied the pose of %zu bone%s", g_clipboard_count, g_clipboard_count == 1 ? "" : "s");
}

void RigPastePose(BOOL mirrored) {
    MeshAnimation* anim = RigActive();
    if (!anim || g_clipboard_count == 0) return;
    size_t pasted = 0;
    for (size_t i = 0; i < g_clipboard_count; i++) {
        char name[256];
        memcpy(name, g_clipboard_names[i], sizeof(name));
        BonePose pose = g_clipboard[i];
        if (mirrored) {
            char partner[256];
            if (MirrorName(name, partner)) memcpy(name, partner, sizeof(name));
            pose.location[0] = -pose.location[0];
            pose.rotation[1] = -pose.rotation[1];
            pose.rotation[2] = -pose.rotation[2];
        }
        size_t b = SkeletonFindBone(&(anim->skeleton), name);
        if (b == (size_t)-1) continue;
        BoneSetPose(anim, b, &pose);
        if (Timeline()->autokey) KeyInsert(anim, b, Timeline()->frame, KEY_CHANNEL_ALL);
        pasted++;
    }
    loginfo("Pasted %s pose onto %zu bone%s", mirrored ? "a mirrored" : "the", pasted, pasted == 1 ? "" : "s");
}
