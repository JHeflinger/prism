#include "subsurface.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include <util/logger.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SSS_RECORD_WORDS 8u
#define SSS_BUFFER_HEADER 4u // words: object count, 3 reserved
#define SSS_ENTRY_WORDS 8u // per object: blob base per channel (3), mesh index, flags, triangle start, channels, 0
#define SSS_SPECTRAL_SPREAD 0.05f // relative spread of sigma_t across rgb above which one cloud per channel is trained

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
    float g;
    float scale; // object scale it was trained at
} Cloud;

typedef struct {
    SubsurfaceTrainPhase phase;
    SSSFitter* fitter;
    size_t mesh;
    uint32_t tstart;
    uint32_t tcount;
    MaterialID material;
    float g;
    float scale;
    uint32_t channels;
    uint32_t channel; // channel currently being fitted
    float sigma[3];
    SSSFitConfig config;
    float* pos; // object space mesh, kept for the per channel fits
    uint32_t* idx;
    uint32_t nverts;
    uint32_t* blob[3];
    uint32_t words[3];
    char status[192];
    SSSFitStats stats;
    BOOL have_stats;
    double started;
} CloudTrainJob;

static Cloud* g_clouds = NULL;
static size_t g_cloud_count = 0;
static CloudTrainJob g_job = { 0 };
static BOOL g_update_gsss = FALSE;

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

static uint32_t MaterialDensities(const SurfaceMaterial* m, float* out) {
    float t[3] = { 0 };
    for (int k = 0; k < 3; k++) t[k] = fmaxf((m->scattering[k] + m->absorbtion[k]) * m->scale, 1e-6f);
    float lo = fminf(t[0], fminf(t[1], t[2])), hi = fmaxf(t[0], fmaxf(t[1], t[2]));
    if ((hi - lo) > SSS_SPECTRAL_SPREAD * hi) {
        for (int k = 0; k < 3; k++) out[k] = t[k];
        return 3;
    }
    vec3 total = { m->scattering[0] + m->absorbtion[0],
                   m->scattering[1] + m->absorbtion[1],
                   m->scattering[2] + m->absorbtion[2] };
    float sigma = Luminance(total) * m->scale;
    float material_sigma = sigma > 1e-6f ? sigma : 1e-6;
    out[0] = out[1] = out[2] = material_sigma;
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
    logtrace("Subsurface splats: %u reference walks (%u lost), %.1f scattering events per walk on average", s->walks_kept, s->walks_lost, s->mean_steps);
    logtrace("    path length field: log likelihood %.4f -> %.4f, explains %.3f percent of the log excess length variance", -s->nll_initial, -s->nll_final, 100.0 * s->r2_mean);
    if (s->validation_samples == 0) return;
    logtrace("    validation on %u held out walks (walk / model):", s->validation_samples);
    logtrace("        exit distance median %.2f / %.2f mfp, 90%% %.2f / %.2f mfp", s->walk_dist_q[1], s->model_dist_q[1], s->walk_dist_q[3], s->model_dist_q[3]);
    logtrace("        path length median %.2f / %.2f mfp, 90%% %.2f / %.2f mfp", s->walk_len_q[1], s->model_len_q[1], s->walk_len_q[3], s->model_len_q[3]);
    logtrace("        throughput walk / length only / cloud: a=0.5 %.4f / %.4f / %.4f, a=0.9 %.4f / %.4f / %.4f, a=0.99 %.4f / %.4f / %.4f", s->tp_walk[0], s->tp_length[0], s->tp_model[0], s->tp_walk[1], s->tp_length[1], s->tp_model[1], s->tp_walk[2], s->tp_length[2], s->tp_model[2]);
    logtrace("        brightness vs walk (length only / cloud): a=0.5 %.3fx / %.3fx, a=0.9 %.3fx / %.3fx, a=0.99 %.3fx / %.3fx", s->tp_length[0] / fmax(s->tp_walk[0], 1e-12), s->tp_model[0] / fmax(s->tp_walk[0], 1e-12), s->tp_length[1] / fmax(s->tp_walk[1], 1e-12), s->tp_model[1] / fmax(s->tp_walk[1], 1e-12), s->tp_length[2] / fmax(s->tp_walk[2], 1e-12), s->tp_model[2] / fmax(s->tp_walk[2], 1e-12));
    logtrace("        mean exit cosine %.3f / %.3f, projection failures %u (%.1f%%)", s->walk_cos, s->model_cos, s->projection_failures, 100.0 * s->projection_failures / (double)s->validation_samples);
}

static void ReleaseJobData(CloudTrainJob* j) {
    if (j->fitter) SSSFitDestroy(j->fitter);
    j->fitter = NULL;
    EZ_FREE(j->pos);
    j->pos = NULL;
    EZ_FREE(j->idx);
    j->idx = NULL;
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
        SSSFitInput in = { j->pos, j->idx, j->nverts, j->tcount, j->sigma[j->channel] * j->scale, j->g };
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
    c.g = j->g;
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

SSSFitConfig SubsurfaceDefaultConfig() {
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

size_t SubsurfaceCloudCount() {
    return g_cloud_count;
}

BOOL SubsurfaceTrainStart(size_t mesh, const SSSFitConfig* config) {
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
    g_job.g = mat->anisotropy;
    g_job.scale = TransformScale(md);
    size_t nv = geo->vertices.size;
    uint32_t* remap = EZ_ALLOC(nv ? nv : 1, sizeof(uint32_t));
    for (size_t i = 0; i < nv; i++) remap[i] = (uint32_t)-1;
    uint32_t* idx = EZ_ALLOC((size_t)g_job.tcount * 3, sizeof(uint32_t));
    float* pos = EZ_ALLOC((size_t)g_job.tcount * 9, sizeof(float));
    uint32_t used = 0;
    for (uint32_t t = 0; t < g_job.tcount; t++) {
        const Triangle* tri = &geo->triangles.data[g_job.tstart + t];
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
    EZ_FREE(remap);
    g_job.pos = pos;
    g_job.idx = idx;
    g_job.nverts = used;
    g_job.config = config ? *config : SubsurfaceDefaultConfig();
    g_job.channel = 0;
    SSSFitInput in = { g_job.pos, g_job.idx, g_job.nverts, g_job.tcount, g_job.sigma[0] * g_job.scale, g_job.g };
    g_job.fitter = SSSFitCreate(&in, &g_job.config);
    if (!g_job.fitter) {
        snprintf(g_job.status, sizeof(g_job.status), "Failed: could not set up the fit");
        logerror(g_job.status);
        g_job.phase = SUBSURFACE_FAILED;
        return FALSE;
    }
    g_job.started = GetTime();
    g_job.phase = SUBSURFACE_TRAINING;
    snprintf(g_job.status, sizeof(g_job.status), "Starting");
    if (g_job.channels == 3) loginfo("Training subsurface splats for \"%s\": %u triangles, coloured scattering -> one cloud per channel, sigma_t %.3f / %.3f / %.3f, g %.2f", *MeshNameReference(mesh), g_job.tcount, g_job.sigma[0], g_job.sigma[1], g_job.sigma[2], g_job.g);
    else loginfo("Training subsurface splats for \"%s\": %u triangles, sigma %.3f (mean free path %.4g), g %.2f", *MeshNameReference(mesh), g_job.tcount, g_job.sigma[0], 1.0 / g_job.sigma[0], g_job.g);
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
    info->anisotropy = c->g;
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
        float mu[3] = { 0 };
        float ax[9] = { 0 };
        float sc[3] = { 0 };
        SSSBlobGaussian(pblob, i, mu, ax, sc, &le[i]);
        if (le[i] < lo) lo = le[i];
        if (le[i] > hi) hi = le[i];
    }
    size_t first = NumSplats();
    for (uint32_t i = 0; i < n; i++) {
        float mu[3] = { 0 };
        float ax[9] = { 0 };
        float sc[3] = { 0 };
        float unused = 0;
        SSSBlobGaussian(pblob, i, mu, ax, sc, &unused);
        GaussianSplat s = { 0 };
        vec3 lp = { mu[0], mu[1], mu[2] };
        glm_mat4_mulv3(M, lp, 1.0f, s.position);
        vec3 cols[3] = { 0 };
        for (int k = 0; k < 3; k++) {
            vec3 a = { ax[k * 3] * sc[k], ax[k * 3 + 1] * sc[k], ax[k * 3 + 2] * sc[k] };
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
        memcpy(&out[w++], &c->g, 4);
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
        memcpy(&c.g, &data[w + 5], 4);
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
