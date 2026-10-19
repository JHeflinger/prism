#include "subsurface.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include <util/logger.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SSS_MATERIAL_MODEL 11u // SUBSURFACE_MODEL in shaders/shared.import
#define SSS_BUFFER_HEADER 4u   // words: object count, 3 reserved
#define SSS_ENTRY_WORDS 4u     // per object: blob base, mesh index, flags, triangle start
#define SSS_FILE_MAGIC "PSSS"
#define SSS_FILE_VERSION 1u

typedef struct {
    uint32_t mesh;
    BOOL enabled;
    uint32_t* blob;
    uint32_t words;
    uint32_t tstart;   // triangle range the cloud was trained on (re-synced when meshes move)
    uint32_t tcount;
    MaterialID material;
    float sigma;       // world space density it was trained for
    float g;
    float scale;       // object scale it was trained at
} Cloud;

typedef struct {
    SubsurfaceTrainPhase phase;
    SSSFitter* fitter;
    size_t mesh;
    uint32_t tstart, tcount;
    MaterialID material;
    float sigma, g, scale;
    char status[192];
    SSSFitStats stats;
    BOOL have_stats;
    double started;
} TrainJob;

static Cloud* g_clouds = NULL;
static size_t g_cloud_count = 0;
static TrainJob g_job = { 0 };
static SubsurfaceSettings g_settings = { TRUE, TRUE };
static ShaderBuffer g_buffer = { 0 };

SubsurfaceSettings* SubsurfaceSettingsRef() {
    return &g_settings;
}

ShaderBuffer* SubsurfaceBuffer() {
    return &g_buffer;
}

SSSFitConfig SubsurfaceDefaultConfig() {
    return SSSFitDefaultConfig();
}

size_t SubsurfaceCloudCount() {
    return g_cloud_count;
}

// ---------------------------------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------------------------------

static BOOL MeshTriangles(const MeshDescriptor* md, uint32_t* start, uint32_t* count) {
    if (md->tend < md->tstart || md->tend == (TriangleID)-1) return FALSE;
    *start = md->tstart;
    *count = md->tend - md->tstart + 1;
    return TRUE;
}

static float TransformScale(const MeshDescriptor* md) {
    const float (*m)[4] = (const float (*)[4])md->transform;
    float det = m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2])
              - m[1][0] * (m[0][1] * m[2][2] - m[2][1] * m[0][2])
              + m[2][0] * (m[0][1] * m[1][2] - m[1][1] * m[0][2]);
    float s = cbrtf(fabsf(det));
    return s > 1e-8f ? s : 1.0f;
}

// free flight density of the random walk in path.comp: max(luminance(sigma_s + sigma_a) * scale, EPS)
static float MaterialSigma(const SurfaceMaterial* m) {
    vec3 t = { m->scattering[0] + m->absorbtion[0], m->scattering[1] + m->absorbtion[1], m->scattering[2] + m->absorbtion[2] };
    float s = Luminance(t) * m->scale;
    return s > 1e-6f ? s : 1e-6f;
}

static MaterialID DominantMaterial(uint32_t start, uint32_t count) {
    Geometry* geo = RendererGeometry();
    MaterialID best = 0;
    size_t bestn = 0;
    size_t nm = geo->materials.size;
    size_t* hist = EZ_ALLOC(nm ? nm : 1, sizeof(size_t));
    for (uint32_t t = start; t < start + count && t < geo->triangles.size; t++) {
        MaterialID m = geo->triangles.data[t].material;
        if (m < nm && ++hist[m] > bestn) { bestn = hist[m]; best = m; }
    }
    EZ_FREE(hist);
    return best;
}

static Cloud* FindCloud(size_t mesh) {
    for (size_t i = 0; i < g_cloud_count; i++) if (g_clouds[i].mesh == mesh) return &g_clouds[i];
    return NULL;
}

static void FreeCloud(Cloud* c) {
    EZ_FREE(c->blob);
    memset(c, 0, sizeof(*c));
}

static void RemoveCloudAt(size_t i) {
    FreeCloud(&g_clouds[i]);
    memmove(&g_clouds[i], &g_clouds[i + 1], (g_cloud_count - i - 1) * sizeof(Cloud));
    g_cloud_count--;
}

static void ResetAccumulation() {
    if (NumCameras() > 0) RendererCamera()->config.reset = TRUE;
}

// Rebuilds the packed buffer: [count, 0, 0, 0] [base, mesh, flags, tstart] * count, blobs...
static void RebuildBuffer() {
    Geometry* geo = RendererGeometry();
    // follow the objects' triangle ranges (other meshes may have been deleted before this one)
    for (size_t i = 0; i < g_cloud_count; i++) {
        Cloud* c = &g_clouds[i];
        if (c->mesh < geo->meshes.size) {
            uint32_t s, n;
            if (MeshTriangles(&geo->meshes.data[c->mesh], &s, &n) && n == c->tcount) c->tstart = s;
        }
    }
    size_t words = SSS_BUFFER_HEADER + SSS_ENTRY_WORDS * g_cloud_count;
    for (size_t i = 0; i < g_cloud_count; i++) words += g_clouds[i].words;
    EZ_FREE(g_buffer.data);
    g_buffer.data = EZ_ALLOC(words, sizeof(uint32_t));
    g_buffer.size = words * sizeof(uint32_t);
    uint32_t* w = (uint32_t*)g_buffer.data;
    w[0] = (uint32_t)g_cloud_count;
    uint32_t base = (uint32_t)(SSS_BUFFER_HEADER + SSS_ENTRY_WORDS * g_cloud_count);
    for (size_t i = 0; i < g_cloud_count; i++) {
        Cloud* c = &g_clouds[i];
        uint32_t* e = w + SSS_BUFFER_HEADER + SSS_ENTRY_WORDS * i;
        e[0] = base;
        e[1] = c->mesh;
        e[2] = c->enabled ? 1u : 0u;
        e[3] = c->tstart;
        memcpy(w + base, c->blob, (size_t)c->words * sizeof(uint32_t));
        w[base + SSSG_H_TSTART] = c->tstart;
        base += c->words;
    }
    g_buffer.update = TRUE;
    ResetAccumulation();
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

// ---------------------------------------------------------------------------------------------------------------------
// training job
// ---------------------------------------------------------------------------------------------------------------------

static double Clock(void) {
    return GetTime();
}

BOOL SubsurfaceTrainStart(size_t mesh, const SSSFitConfig* config) {
    TrainJob* j = &g_job;
    if (j->phase == SUBSURFACE_TRAINING) {
        logwarn("A subsurface cloud is already being trained");
        return FALSE;
    }
    if (j->fitter) SSSFitDestroy(j->fitter);
    memset(j, 0, sizeof(*j));
    Geometry* geo = RendererGeometry();
    j->mesh = mesh;
    if (mesh >= geo->meshes.size) {
        snprintf(j->status, sizeof(j->status), "Failed: no such object");
        j->phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    MeshDescriptor* md = &geo->meshes.data[mesh];
    if (!MeshTriangles(md, &j->tstart, &j->tcount) || j->tstart + j->tcount > geo->triangles.size) {
        snprintf(j->status, sizeof(j->status), "Failed: the object has no triangles");
        j->phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    j->material = DominantMaterial(j->tstart, j->tcount);
    const SurfaceMaterial* mat = &geo->materials.data[j->material];
    if (mat->model != SSS_MATERIAL_MODEL) {
        snprintf(j->status, sizeof(j->status), "Failed: material \"%s\" does not use the subsurface model", MaterialName(j->material));
        logerror("Cannot train subsurface splats: the object's material is not a subsurface material");
        j->phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    j->sigma = MaterialSigma(mat);
    j->g = mat->anisotropy;
    j->scale = TransformScale(md);
    // object space copy of the mesh, vertices compacted to the ones the triangles use
    size_t nv = geo->vertices.size;
    uint32_t* remap = EZ_ALLOC(nv ? nv : 1, sizeof(uint32_t));
    for (size_t i = 0; i < nv; i++) remap[i] = (uint32_t)-1;
    uint32_t* idx = EZ_ALLOC((size_t)j->tcount * 3, sizeof(uint32_t));
    float* pos = EZ_ALLOC((size_t)j->tcount * 9, sizeof(float));
    uint32_t used = 0;
    for (uint32_t t = 0; t < j->tcount; t++) {
        const Triangle* tri = &geo->triangles.data[j->tstart + t];
        VertexID ids[3] = { tri->a, tri->b, tri->c };
        for (int k = 0; k < 3; k++) {
            VertexID v = ids[k] < nv ? ids[k] : 0;
            if (remap[v] == (uint32_t)-1) {
                remap[v] = used;
                memcpy(&pos[used * 3], geo->vertices.data[v], sizeof(float) * 3);
                used++;
            }
            idx[t * 3 + k] = remap[v];
        }
    }
    SSSFitInput in = { pos, idx, used, j->tcount, j->sigma * j->scale, j->g };
    SSSFitConfig cfg = config ? *config : SSSFitDefaultConfig();
    j->fitter = SSSFitCreate(&in, &cfg);
    EZ_FREE(remap);
    EZ_FREE(idx);
    EZ_FREE(pos);
    if (!j->fitter) {
        snprintf(j->status, sizeof(j->status), "Failed: could not set up the fit");
        j->phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    j->started = GetTime();
    j->phase = SUBSURFACE_TRAINING;
    snprintf(j->status, sizeof(j->status), "Starting");
    loginfo("Training subsurface splats for \"%s\": %u triangles, sigma %.3f (mean free path %.4g), g %.2f",
        *MeshNameReference(mesh), j->tcount, j->sigma, 1.0 / j->sigma, j->g);
    return TRUE;
}

static void LogStats(const SSSFitStats* s) {
    loginfo("Subsurface splats: %u reference walks (%u lost), %.1f scattering events per walk on average",
        s->walks_kept, s->walks_lost, s->mean_steps);
    loginfo("  path length field: log likelihood %.4f -> %.4f, explains %.0f%% of the log excess length variance",
        -s->nll_initial, -s->nll_final, 100.0 * s->r2_mean);
    loginfo("  throughput model (E[a^n] walk vs model): a=0.5 %.4f/%.4f, a=0.9 %.4f/%.4f, a=0.99 %.4f/%.4f",
        s->albedo_true[0], s->albedo_model[0], s->albedo_true[1], s->albedo_model[1], s->albedo_true[2], s->albedo_model[2]);
    if (s->validation_samples == 0) return;
    loginfo("  validation on %u held out walks (walk / model):", s->validation_samples);
    loginfo("    exit distance median %.2f / %.2f mfp, 90%% %.2f / %.2f mfp",
        s->walk_dist_q[1], s->model_dist_q[1], s->walk_dist_q[3], s->model_dist_q[3]);
    loginfo("    path length median %.2f / %.2f mfp, 90%% %.2f / %.2f mfp",
        s->walk_len_q[1], s->model_len_q[1], s->walk_len_q[3], s->model_len_q[3]);
    loginfo("    attenuation E[exp(-kL)] k=0.01: %.4f / %.4f, k=0.1: %.4f / %.4f, k=1: %.4f / %.4f (k in units of sigma)",
        s->walk_atten[0], s->model_atten[0], s->walk_atten[1], s->model_atten[1], s->walk_atten[2], s->model_atten[2]);
    loginfo("    mean exit cosine %.3f / %.3f, projection failures %u", s->walk_cos, s->model_cos, s->projection_failures);
}

static void FinishJob(TrainJob* j) {
    uint32_t words = 0;
    const uint32_t* blob = SSSFitBlob(j->fitter, &words);
    j->stats = *SSSFitGetStats(j->fitter);
    j->have_stats = TRUE;
    Geometry* geo = RendererGeometry();
    uint32_t s, n;
    if (!blob || j->mesh >= geo->meshes.size || !MeshTriangles(&geo->meshes.data[j->mesh], &s, &n) || n != j->tcount) {
        snprintf(j->status, sizeof(j->status), "Failed: the object changed while it was being trained");
        j->phase = SUBSURFACE_FAILED;
        SSSFitDestroy(j->fitter);
        j->fitter = NULL;
        return;
    }
    Cloud c = { 0 };
    c.mesh = (uint32_t)j->mesh;
    c.enabled = TRUE;
    c.words = words;
    c.blob = EZ_ALLOC(words, sizeof(uint32_t));
    memcpy(c.blob, blob, (size_t)words * sizeof(uint32_t));
    c.tstart = s;
    c.tcount = n;
    c.material = j->material;
    c.sigma = j->sigma;
    c.g = j->g;
    c.scale = j->scale;
    c.blob[SSSG_H_TSTART] = s;
    c.blob[SSSG_H_MATERIAL] = j->material;
    memcpy(&c.blob[SSSG_H_SCALE], &j->scale, 4);
    SSSFitDestroy(j->fitter);
    j->fitter = NULL;
    StoreCloud(c);
    LogStats(&j->stats);
    snprintf(j->status, sizeof(j->status), "Done in %.0fs: %.1f KB cloud linked to \"%s\"",
        GetTime() - j->started, words * 4.0 / 1024.0, *MeshNameReference(j->mesh));
    loginfo("%s", j->status);
    j->phase = SUBSURFACE_DONE;
}

BOOL SubsurfaceTrainTick(double seconds) {
    TrainJob* j = &g_job;
    if (j->phase != SUBSURFACE_TRAINING || !j->fitter) return FALSE;
    SSSFitPhase p = SSSFitStep(j->fitter, seconds, Clock);
    snprintf(j->status, sizeof(j->status), "%s", SSSFitStatus(j->fitter));
    if (p == SSSFIT_FAILED) {
        logerror("Subsurface splat training failed: %s", SSSFitStatus(j->fitter));
        SSSFitDestroy(j->fitter);
        j->fitter = NULL;
        j->phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    if (p == SSSFIT_DONE) {
        FinishJob(j);
        return FALSE;
    }
    return TRUE;
}

void SubsurfaceTrainCancel() {
    TrainJob* j = &g_job;
    if (j->phase != SUBSURFACE_TRAINING) return;
    SSSFitDestroy(j->fitter);
    j->fitter = NULL;
    j->phase = SUBSURFACE_IDLE;
    snprintf(j->status, sizeof(j->status), "Cancelled");
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
    return g_job.fitter ? SSSFitProgress(g_job.fitter) : 0.0f;
}

const char* SubsurfaceTrainStatus() {
    return g_job.status;
}

const SSSFitStats* SubsurfaceLastStats() {
    return g_job.have_stats ? &g_job.stats : NULL;
}

// ---------------------------------------------------------------------------------------------------------------------
// clouds
// ---------------------------------------------------------------------------------------------------------------------

BOOL SubsurfaceCloudInfoFor(size_t mesh, SubsurfaceCloudInfo* info) {
    memset(info, 0, sizeof(*info));
    Cloud* c = FindCloud(mesh);
    if (!c) return FALSE;
    const uint32_t* b = c->blob;
    info->linked = TRUE;
    info->enabled = c->enabled;
    info->triangles = c->tcount;
    info->surface_gaussians = b[SSSG_H_NSURF];
    info->lobes = b[SSSG_H_NLOBE];
    info->internal_gaussians = b[SSSG_H_NGAUSS];
    info->bytes = c->words * 4;
    info->sigma = c->sigma;
    info->anisotropy = c->g;
    info->scale = c->scale;
    info->material = c->material;
    Geometry* geo = RendererGeometry();
    uint32_t s, n;
    if (mesh >= geo->meshes.size || !MeshTriangles(&geo->meshes.data[mesh], &s, &n) || n != c->tcount) {
        info->stale = TRUE;
        snprintf(info->reason, sizeof(info->reason), "triangles changed, the random walk is used instead");
        return TRUE;
    }
    MaterialID now = geo->triangles.data[s].material;
    if (now >= geo->materials.size) return TRUE;
    const SurfaceMaterial* m = &geo->materials.data[now];
    float sigma = MaterialSigma(m);
    float scale = TransformScale(&geo->meshes.data[mesh]);
    if (m->model != SSS_MATERIAL_MODEL) {
        info->stale = TRUE;
        snprintf(info->reason, sizeof(info->reason), "material is no longer a subsurface material");
    } else if (fabsf(sigma - c->sigma) > 0.01f * c->sigma) {
        info->stale = TRUE;
        snprintf(info->reason, sizeof(info->reason), "scattering density changed (%.3g, trained %.3g), retrain", sigma, c->sigma);
    } else if (fabsf(m->anisotropy - c->g) > 0.01f) {
        info->stale = TRUE;
        snprintf(info->reason, sizeof(info->reason), "anisotropy changed (%.2f, trained %.2f), retrain", m->anisotropy, c->g);
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
    BOOL had = g_cloud_count > 0;
    g_cloud_count = 0;
    if (had || g_buffer.data) {
        EZ_FREE(g_buffer.data);
        g_buffer.data = NULL;
        g_buffer.size = 0;
        g_buffer.update = TRUE;
    }
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
    mat4 M;
    memcpy(M, geo->meshes.data[mesh].transform, sizeof(mat4));
    uint32_t n = c->blob[SSSG_H_NGAUSS];
    float* le = EZ_ALLOC(n ? n : 1, sizeof(float));
    float lo = FLT_MAX, hi = -FLT_MAX;
    for (uint32_t i = 0; i < n; i++) {
        float mu[3], ax[9], sc[3];
        SSSBlobGaussian(c->blob, i, mu, ax, sc, &le[i]);
        if (le[i] < lo) lo = le[i];
        if (le[i] > hi) hi = le[i];
    }
    size_t first = NumSplats();
    for (uint32_t i = 0; i < n; i++) {
        float mu[3], ax[9], sc[3], unused;
        SSSBlobGaussian(c->blob, i, mu, ax, sc, &unused);
        GaussianSplat s = { 0 };
        vec3 lp = { mu[0], mu[1], mu[2] };
        glm_mat4_mulv3(M, lp, 1.0f, s.position);
        // world space axes: M3 * (axis * scale), re-orthonormalised (exact for rotation + uniform scale)
        vec3 cols[3];
        for (int k = 0; k < 3; k++) {
            vec3 a = { ax[k * 3] * sc[k], ax[k * 3 + 1] * sc[k], ax[k * 3 + 2] * sc[k] };
            glm_mat4_mulv3(M, a, 0.0f, cols[k]);
            float len = glm_vec3_norm(cols[k]);
            s.logscale[k] = logf(fmaxf(len, 1e-6f));
            glm_vec3_normalize(cols[k]);
        }
        vec3 c2;
        glm_vec3_cross(cols[0], cols[1], c2);
        glm_vec3_normalize(c2);
        if (glm_vec3_dot(c2, cols[2]) < 0.0f) glm_vec3_negate(c2);
        glm_vec3_cross(c2, cols[0], cols[1]);
        QuaternionFromBasis(cols[0], cols[1], c2, s.rotation);
        s.opacity = logf(0.5f / 0.5f);
        // short expected paths blue, long ones red
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

// ---------------------------------------------------------------------------------------------------------------------
// persistence: clouds are stored inside the PRISM session (see SavePRISM / LoadPRISM)
// layout (uint32 words): count, then per cloud: mesh, enabled, tstart, tcount, material, sigma, g, scale (float bits),
// blob word count, blob
// ---------------------------------------------------------------------------------------------------------------------

#define SSS_RECORD_WORDS 9u

uint32_t* SubsurfaceSerialize(size_t* words) {
    size_t total = 1;
    for (size_t i = 0; i < g_cloud_count; i++) total += SSS_RECORD_WORDS + g_clouds[i].words;
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
        memcpy(&out[w++], &c->sigma, 4);
        memcpy(&out[w++], &c->g, 4);
        memcpy(&out[w++], &c->scale, 4);
        out[w++] = c->words;
        memcpy(&out[w], c->blob, (size_t)c->words * sizeof(uint32_t));
        w += c->words;
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
    for (size_t i = 0; i < count; i++) {
        if (w + SSS_RECORD_WORDS > words) { ok = FALSE; break; }
        Cloud c = { 0 };
        c.mesh = data[w];
        c.enabled = data[w + 1] != 0;
        c.tstart = data[w + 2];
        c.tcount = data[w + 3];
        c.material = data[w + 4];
        memcpy(&c.sigma, &data[w + 5], 4);
        memcpy(&c.g, &data[w + 6], 4);
        memcpy(&c.scale, &data[w + 7], 4);
        c.words = data[w + 8];
        w += SSS_RECORD_WORDS;
        if (c.words > words - w) { ok = FALSE; break; }
        const uint32_t* blob = &data[w];
        w += c.words;
        uint32_t s, n;
        if (SSSBlobWords(blob, c.words) == 0 || blob[SSSG_H_TCOUNT] != c.tcount || c.mesh >= geo->meshes.size ||
            !MeshTriangles(&geo->meshes.data[c.mesh], &s, &n) || n != c.tcount || FindCloud(c.mesh)) {
            logwarn("Skipping a saved subsurface cloud that no longer matches its object");
            continue;
        }
        c.blob = EZ_ALLOC(c.words, sizeof(uint32_t));
        memcpy(c.blob, blob, (size_t)c.words * sizeof(uint32_t));
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
