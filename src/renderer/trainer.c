#include "trainer.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include "trainmask.h"
#include <util/logger.h>

#define PSTRIDE 16
#define GSTRIDE 16
#define RSTRIDE 12
#define SORT_GROUPS 256
#define SCAN_BLOCK 2048
#define META_PAIRS 1
#define META_INDIRECT 8
#define TILE 16
#define SORT_BITS 32
#define MIN_SH_C0 0.28209479177387814f

typedef struct {
    alignas(16) float eye_d[4];
    alignas(16) float right_cx[4];
    alignas(16) float down_cy[4];
    alignas(16) float fwd_near[4];
    alignas(16) uint32_t dims[4];
    alignas(16) uint32_t cnt[4];
    alignas(16) float bgl[4];
    alignas(16) float dr[4];
} PC;

typedef struct {
    alignas(16) float lr_a[4];
    alignas(16) float lr_b[4];
    alignas(16) float bias[4];
    alignas(16) uint32_t cnt[4];
} AdamPC;

typedef struct {
    SceneCamera saved_camera;
    PipelineFlags flags_before;
    TrainingSceneConfig config;
    TrainingScenePhase phase;
    Trainer trainer;
    BOOL trainer_alive;
    uint32_t width, height;
    uint32_t captured;
    SimpleCamera* cameras;
    uint8_t* source; // full resolution capture buffer
    uint8_t* scaled; // training resolution image
    float center[3];
    float radius;
    float background[3];
    // coverage mask + seed colouring (see trainmask.h)
    float (*tris)[3][3]; // enabled scene triangles in world space
    size_t tri_count;
    float* coverage; // per training pixel
    float* depth;
    float* scratch;
    GaussianSplat* seeds;
    float* seed_geo; // 6 floats per seed: position, normal
    float* seed_acc; // 4 floats per seed: weighted rgb, weight
    uint32_t seed_count;
    double bg_sum[3];
    double bg_n;
    uint32_t orient_votes[4]; // captured image orientation: index = flip_x | flip_y << 1
    char status[160];
    char message[160];
    double started_at;
} TrainingJob;

static uint32_t g_seed = 1;
static TrainingJob g_job;
static const char* g_kernel_names[] = {
    "preprocess",
    "scan_block",
    "scan_top",
    "scan_add",
    "setup",
    "duplicate",
    "sort_hist",
    "sort_scan",
    "sort_scatter",
    "ranges",
    "render_fwd",
    "render_bwd",
    "loss_stats",
    "loss_grad",
    "preprocess_bwd",
    "adam",
    "densify_flags",
    "densify_apply",
    "opacity_reset"
};

TrainerConfig DefaultTrainerConfig() {
    TrainerConfig c = { 0 };
    c.iterations = 7000;
    c.max_gaussians = 300000;
    c.lr_position_init = 1.6e-4f;
    c.lr_position_final = 1.6e-6f;
    c.lr_scale = 5.0e-3f;
    c.lr_rotation = 1.0e-3f;
    c.lr_opacity = 5.0e-2f;
    c.lr_color = 2.5e-3f;
    c.lambda_ssim = 0.2f;
    c.lambda_alpha = 1.0f;
    c.densify_from = 500;
    c.densify_until = 4500;
    c.densify_interval = 100;
    c.opacity_reset_interval = 3000;
    c.densify_grad_threshold = 2.0e-4f;
    c.percent_dense = 0.01f;
    c.min_opacity = 0.005f;
    c.max_world_scale = 0.1f;
    c.max_screen_radius = 0.0f;
    c.log_interval = 100;
    c.gpu_index = -1;
    return c;
}

static void BindAllBuffers(Trainer* t) {
    TGPUBuffer* order[24] = {
        &t->buffers.params,
        &t->buffers.grads,
        &t->buffers.m,
        &t->buffers.v,
        &t->buffers.geom,
        &t->buffers.counts,
        &t->buffers.offsets,
        &t->buffers.blocks,
        &t->buffers.keys_a,
        &t->buffers.vals_a,
        &t->buffers.keys_b,
        &t->buffers.vals_b,
        &t->buffers.hist,
        &t->buffers.ranges,
        &t->buffers.meta,
        &t->buffers.render,
        &t->buffers.nconts,
        &t->buffers.gt,
        &t->buffers.dldpix,
        &t->buffers.ssim,
        &t->buffers.grad2d,
        &t->buffers.dens,
        &t->buffers.actions,
        &t->buffers.loss
    };
    for (uint32_t i = 0; i < 24; i++) tgpu_bind(&t->gpu, i, order[i]);
}

static BOOL AllocatePairs(Trainer* t, uint32_t pairs) {
    TGPU* g = &t->gpu;
    vkDeviceWaitIdle(g->device);
    tgpu_buffer_destroy(g, &t->buffers.keys_a);
    tgpu_buffer_destroy(g, &t->buffers.vals_a);
    tgpu_buffer_destroy(g, &t->buffers.keys_b);
    tgpu_buffer_destroy(g, &t->buffers.vals_b);
    VkDeviceSize bytes = (VkDeviceSize)pairs * 4;
    if (tgpu_buffer_create(g, &t->buffers.keys_a, bytes) || tgpu_buffer_create(g, &t->buffers.vals_a, bytes) ||
        tgpu_buffer_create(g, &t->buffers.keys_b, bytes) || tgpu_buffer_create(g, &t->buffers.vals_b, bytes)) return FALSE;
    t->pair_capacity = pairs;
    return TRUE;
}

static BOOL AllocateGaussianBuffers(Trainer* t, uint32_t cap) {
    TGPU* g = &t->gpu;
    uint32_t blocks = cap / SCAN_BLOCK + 2;
    BOOL result = TRUE;
    result |= tgpu_buffer_create(g, &t->buffers.params, (VkDeviceSize)cap * PSTRIDE * 4);
    result |= tgpu_buffer_create(g, &t->buffers.grads, (VkDeviceSize)cap * PSTRIDE * 4);
    result |= tgpu_buffer_create(g, &t->buffers.m, (VkDeviceSize)cap * PSTRIDE * 4);
    result |= tgpu_buffer_create(g, &t->buffers.v, (VkDeviceSize)cap * PSTRIDE * 4);
    result |= tgpu_buffer_create(g, &t->buffers.geom, (VkDeviceSize)cap * GSTRIDE * 4);
    result |= tgpu_buffer_create(g, &t->buffers.counts, (VkDeviceSize)cap * 4);
    result |= tgpu_buffer_create(g, &t->buffers.offsets, (VkDeviceSize)cap * 4);
    result |= tgpu_buffer_create(g, &t->buffers.blocks, (VkDeviceSize)blocks * 4);
    result |= tgpu_buffer_create(g, &t->buffers.grad2d, (VkDeviceSize)cap * RSTRIDE * 4);
    result |= tgpu_buffer_create(g, &t->buffers.dens, (VkDeviceSize)cap * 16);
    result |= tgpu_buffer_create(g, &t->buffers.actions, (VkDeviceSize)cap * 4);
    return result;
}

static void FreeTrainerResources(Trainer* t) {
    TGPU* g = &t->gpu;
    if (g->device) vkDeviceWaitIdle(g->device);
    TGPUBuffer* bufs[] = {
        &t->buffers.params,
        &t->buffers.grads,
        &t->buffers.m,
        &t->buffers.v,
        &t->buffers.geom,
        &t->buffers.counts,
        &t->buffers.offsets,
        &t->buffers.blocks,
        &t->buffers.keys_a,
        &t->buffers.vals_a,
        &t->buffers.keys_b,
        &t->buffers.vals_b,
        &t->buffers.hist,
        &t->buffers.ranges,
        &t->buffers.meta,
        &t->buffers.render,
        &t->buffers.nconts,
        &t->buffers.gt,
        &t->buffers.dldpix,
        &t->buffers.ssim,
        &t->buffers.grad2d,
        &t->buffers.dens,
        &t->buffers.actions,
        &t->buffers.loss
    };
    for (size_t i = 0; i < sizeof(bufs) / sizeof(bufs[0]); i++) if (g->device) tgpu_buffer_destroy(g, bufs[i]);
    TGPUKernel* ks[] = {
        &t->kernels.preprocess,
        &t->kernels.scan_block,
        &t->kernels.scan_top,
        &t->kernels.scan_add,
        &t->kernels.setup,
        &t->kernels.duplicate,
        &t->kernels.sort_hist,
        &t->kernels.sort_scan,
        &t->kernels.sort_scatter,
        &t->kernels.ranges,
        &t->kernels.render_fwd,
        &t->kernels.render_bwd,
        &t->kernels.loss_stats,
        &t->kernels.loss_grad,
        &t->kernels.preprocess_bwd,
        &t->kernels.adam,
        &t->kernels.densify_flags,
        &t->kernels.densify_apply,
        &t->kernels.opacity_reset
    };
    for (size_t i = 0; i < sizeof(ks) / sizeof(ks[0]); i++) if (g->device) tgpu_kernel_destroy(g, ks[i]);
    EZ_FREE(t->views);
    EZ_FREE(t->view_order);
    t->views = NULL;
    t->view_order = NULL;
}

static void BuildPushConstants(const Trainer* t, uint32_t view, PC* pc) {
    memset(pc, 0, sizeof(*pc));
    const TrainerView* v = &t->views[view];
    memcpy(pc->eye_d, v->eye, 12);
    pc->eye_d[3] = v->focal;
    memcpy(pc->right_cx, v->right, 12);
    pc->right_cx[3] = (float)t->width * 0.5f;
    memcpy(pc->down_cy, v->down, 12);
    pc->down_cy[3] = (float)t->height * 0.5f;
    memcpy(pc->fwd_near, v->fwd, 12);
    pc->fwd_near[3] = 0.2f * 0.1f;
    pc->dims[0] = t->width;
    pc->dims[1] = t->height;
    pc->dims[2] = t->tiles_x;
    pc->dims[3] = t->tiles_y;
    pc->cnt[0] = t->count;
    pc->cnt[1] = t->pair_capacity;
    pc->cnt[2] = view;
    pc->bgl[0] = t->cfg.background[0];
    pc->bgl[1] = t->cfg.background[1];
    pc->bgl[2] = t->cfg.background[2];
    pc->bgl[3] = t->cfg.lambda_ssim;
    pc->dr[0] = v->depth_min;
    pc->dr[1] = v->depth_scale;
    pc->dr[2] = t->cfg.lambda_alpha;
}

static uint32_t GroupsFor(uint32_t n, uint32_t per) {
    return (n + per - 1) / per;
}

static void RecordScan(Trainer* t, uint32_t n) {
    PC pc = { 0 };
    pc.cnt[0] = n;
    uint32_t blocks = GroupsFor(n, SCAN_BLOCK);
    tgpu_dispatch(&t->gpu, &t->kernels.scan_block, blocks, 1, 1, &pc, sizeof(pc));
    tgpu_dispatch(&t->gpu, &t->kernels.scan_top, 1, 1, 1, &pc, sizeof(pc));
    tgpu_dispatch(&t->gpu, &t->kernels.scan_add, blocks, 1, 1, &pc, sizeof(pc));
}

static void RecordForward(Trainer* t, uint32_t view) {
    PC pc = { 0 };
    BuildPushConstants(t, view, &pc);
    uint32_t n = t->count;
    tgpu_fill(&t->gpu, &t->buffers.ranges, 0, t->buffers.ranges.size, 0);
    tgpu_dispatch(&t->gpu, &t->kernels.preprocess, GroupsFor(n, 256), 1, 1, &pc, sizeof(pc));
    RecordScan(t, n);
    tgpu_dispatch(&t->gpu, &t->kernels.setup, 1, 1, 1, &pc, sizeof(pc));
    tgpu_dispatch(&t->gpu, &t->kernels.duplicate, GroupsFor(n, 256), 1, 1, &pc, sizeof(pc));
    for (uint32_t pass = 0; pass < SORT_BITS / 4; pass++) {
        PC sp = pc;
        sp.cnt[3] = (pass * 4) | ((pass & 1) << 16);
        tgpu_dispatch(&t->gpu, &t->kernels.sort_hist, SORT_GROUPS, 1, 1, &sp, sizeof(sp));
        tgpu_dispatch(&t->gpu, &t->kernels.sort_scan, 1, 1, 1, &sp, sizeof(sp));
        tgpu_dispatch(&t->gpu, &t->kernels.sort_scatter, SORT_GROUPS, 1, 1, &sp, sizeof(sp));
    }
    tgpu_dispatch_indirect(&t->gpu, &t->kernels.ranges, &t->buffers.meta, META_INDIRECT * 4, &pc, sizeof(pc));
    tgpu_dispatch(&t->gpu, &t->kernels.render_fwd, t->tiles_x, t->tiles_y, 1, &pc, sizeof(pc));
}

static void RecordLossAndBackward(Trainer* t, uint32_t view) {
    PC pc = { 0 };
    BuildPushConstants(t, view, &pc);
    uint32_t pixels = t->width * t->height;
    tgpu_dispatch(&t->gpu, &t->kernels.loss_stats, GroupsFor(pixels, 256), 1, 1, &pc, sizeof(pc));
    tgpu_dispatch(&t->gpu, &t->kernels.loss_grad, GroupsFor(pixels, 256), 1, 1, &pc, sizeof(pc));
    tgpu_fill(&t->gpu, &t->buffers.grad2d, 0, (VkDeviceSize)t->count * RSTRIDE * 4, 0);
    tgpu_dispatch(&t->gpu, &t->kernels.render_bwd, t->tiles_x, t->tiles_y, 1, &pc, sizeof(pc));
    tgpu_dispatch(&t->gpu, &t->kernels.preprocess_bwd, GroupsFor(t->count, 256), 1, 1, &pc, sizeof(pc));
}

static BOOL ReadLoss(Trainer* t, double* loss, double* l1, double* ssim) {
    uint32_t groups = GroupsFor(t->width * t->height, 256);
    float* parts = EZ_ALLOC(1, (size_t)groups * 8);
    if (tgpu_download(&t->gpu, &t->buffers.loss, 0, parts, (VkDeviceSize)groups * 8)) {
        EZ_FREE(parts);
        return FALSE;
    }
    double s1 = 0, s2 = 0;
    for (uint32_t i = 0; i < groups; i++) { s1 += parts[i * 2]; s2 += parts[i * 2 + 1]; }
    EZ_FREE(parts);
    double inv = 1.0 / (3.0 * (double)t->width * (double)t->height);
    double a = s1 * inv, b = s2 * inv;
    if (l1) *l1 = a;
    if (ssim) *ssim = b;
    if (loss) *loss = (1.0 - t->cfg.lambda_ssim) * a + t->cfg.lambda_ssim * b;
    return TRUE;
}

static BOOL RefreshPairStats(Trainer* t) {
    uint32_t meta[2] = { 0, 0 };
    if (tgpu_download(&t->gpu, &t->buffers.meta, 0, meta, 8)) return FALSE;
    t->last_pairs = meta[0];
    t->pairs_truncated = meta[0] > t->pair_capacity;
    if (meta[0] > (uint32_t)((double)t->pair_capacity * 0.85)) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(t->gpu.gpu, &props);
        uint32_t limit = props.limits.maxStorageBufferRange / 4;
        uint64_t want = (uint64_t)((double)meta[0] * 1.6) + 1024;
        if (want > limit) want = limit;
        if (want > t->pair_capacity) {
            fprintf(stderr, "[trainer] growing tile pair buffers to %llu entries\n", (unsigned long long)want);
            if (!AllocatePairs(t, (uint32_t)want)) return FALSE;
            BindAllBuffers(t);
        }
    }
    return TRUE;
}

static BOOL RunFlags(Trainer* t, int grow, uint32_t* out_total) {
    PC pc = { 0 };
    float r = t->scene_radius;
    pc.cnt[0] = t->count;
    pc.cnt[2] = grow ? 1 : 0;
    pc.cnt[3] = 1;
    pc.dr[0] = t->cfg.densify_grad_threshold;
    pc.dr[1] = t->cfg.min_opacity;
    pc.dr[2] = t->cfg.percent_dense * r;
    pc.dr[3] = t->cfg.max_world_scale * r;
    pc.bgl[0] = (t->cfg.max_screen_radius > 0.0f && t->iteration > t->cfg.opacity_reset_interval) ? t->cfg.max_screen_radius * (float)t->width : 0.0f;
    tgpu_begin(&t->gpu);
    tgpu_dispatch(&t->gpu, &t->kernels.densify_flags, GroupsFor(t->count, 256), 1, 1, &pc, sizeof(pc));
    RecordScan(t, t->count);
    if (tgpu_submit(&t->gpu)) return FALSE;
    uint32_t last_off = 0, last_cnt = 0;
    VkDeviceSize idx = (VkDeviceSize)(t->count - 1) * 4;
    if (tgpu_download(&t->gpu, &t->buffers.offsets, idx, &last_off, 4)) return FALSE;
    if (tgpu_download(&t->gpu, &t->buffers.counts, idx, &last_cnt, 4)) return FALSE;
    *out_total = last_off + last_cnt;
    return TRUE;
}

static BOOL DensifyEx(Trainer* t, int grow) {
    uint32_t total = 0;
    if (!RunFlags(t, grow, &total)) return FALSE;
    if (grow && total > t->capacity) {
        if (!RunFlags(t, 0, &total)) return FALSE; // out of room: prune only
    }
    if (!grow && total == t->count) return TRUE; // nothing to prune
    if (total == 0) {
        logwarn("Trainer density control would remove every gaussian, skipping");
        return TRUE;
    }
    PC pc = { 0 };
    pc.cnt[0] = t->count;
    pc.cnt[3] = t->iteration * 2654435761u + 17u;
    pc.dr[0] = 1.6f; // split scale divisor
    uint32_t n = t->count;
    uint64_t bytes = (uint64_t)total * PSTRIDE * 4;
    tgpu_begin(&t->gpu);
    TGPUBuffer* dst[3] = { &t->buffers.params, &t->buffers.m, &t->buffers.v };
    for (uint32_t mode = 0; mode < 3; mode++) {
        PC p = pc;
        p.cnt[2] = mode;
        tgpu_dispatch(&t->gpu, &t->kernels.densify_apply, GroupsFor(n, 256), 1, 1, &p, sizeof(p)); 
        VkBufferCopy c = { 0, 0, bytes }; // temp result lives in the grads buffer; copy only the live range
        vkCmdCopyBuffer(t->gpu.cmd, t->buffers.grads.buffer, dst[mode]->buffer, 1, &c);
        tgpu_barrier(&t->gpu);
    }
    tgpu_fill(&t->gpu, &t->buffers.dens, 0, t->buffers.dens.size, 0);
    if (tgpu_submit(&t->gpu)) return FALSE;
    t->count = total;
    return TRUE;
}

static BOOL Densify(Trainer* t) {
    return DensifyEx(t, 1);
}

// Removes gaussians that ended up (almost) transparent. Density control stops long before training does, so
// without this pass every gaussian that faded out late is still written to the file.
static BOOL FinalPrune(Trainer* t, float min_opacity) {
    float keep_opacity = t->cfg.min_opacity, keep_radius = t->cfg.max_screen_radius;
    t->cfg.min_opacity = min_opacity;
    t->cfg.max_screen_radius = 0.0f; // the radius statistics are stale here
    BOOL ok = DensifyEx(t, 0);
    t->cfg.min_opacity = keep_opacity;
    t->cfg.max_screen_radius = keep_radius;
    return ok;
}

static BOOL ResetOpacity(Trainer* t) {
    PC pc = { 0 };
    pc.cnt[0] = t->count;
    pc.dr[0] = 0.01f;
    tgpu_begin(&t->gpu);
    tgpu_dispatch(&t->gpu, &t->kernels.opacity_reset, GroupsFor(t->count, 256), 1, 1, &pc, sizeof(pc));
    return tgpu_submit(&t->gpu) == 0;
}

static uint32_t NextView(Trainer* t) {
    if (t->order_cursor == 0) {
        for (uint32_t i = t->num_views; i > 1; i--) {
            t->rng = t->rng * 1664525u + 1013904223u;
            uint32_t j = (t->rng >> 8) % i;
            uint32_t tmp = t->view_order[i - 1];
            t->view_order[i - 1] = t->view_order[j];
            t->view_order[j] = tmp;
        }
    }
    uint32_t v = t->view_order[t->order_cursor];
    t->order_cursor = (t->order_cursor + 1) % t->num_views;
    return v;
}

static float RandomOne() {
    g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5;
    return (float)(g_seed >> 8) * (1.0f / 16777216.0f);
}

static GaussianSplat* SeedGaussians(uint32_t want, uint32_t* out_count, vec3 center, float* radius, float** out_geo) {
    Geometry* geo = RendererGeometry();
    size_t tri_count = geo->triangles.size;
    if (tri_count == 0) return NULL;
    int32_t* mesh_of = EZ_ALLOC(tri_count, sizeof(int32_t));
    for (size_t i = 0; i < tri_count; i++) mesh_of[i] = -1;
    for (size_t m = 0; m < geo->meshes.size; m++) {
        const MeshDescriptor* md = &geo->meshes.data[m];
        for (size_t t = md->tstart; t <= md->tend && t < tri_count; t++) mesh_of[t] = (int32_t)m;
    }
    float (*tv)[3][3] = EZ_ALLOC(tri_count, sizeof(*tv));
    double* cumulative = EZ_ALLOC(tri_count, sizeof(double));
    double total_area = 0.0;
    float bmin[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, bmax[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t t = 0; t < tri_count; t++) {
        const Triangle* tri = &geo->triangles.data[t];
        VertexID ids[3] = { tri->a, tri->b, tri->c };
        const MeshDescriptor* md = mesh_of[t] >= 0 ? &geo->meshes.data[mesh_of[t]] : NULL;
        BOOL disabled = md && md->disabled;
        for (int k = 0; k < 3; k++) {
            vec3 p = { geo->vertices.data[ids[k]][0], geo->vertices.data[ids[k]][1], geo->vertices.data[ids[k]][2] };
            vec3 world;
            if (md) {
                mat4 m;
                memcpy(m, md->transform, sizeof(mat4));
                glm_mat4_mulv3(m, p, 1.0f, world);
            } else {
                glm_vec3_copy(p, world);
            }
            memcpy(tv[t][k], world, sizeof(vec3));
        }
        double area = 0.0;
        if (!disabled) {
            vec3 e1 = { tv[t][1][0] - tv[t][0][0], tv[t][1][1] - tv[t][0][1], tv[t][1][2] - tv[t][0][2] };
            vec3 e2 = { tv[t][2][0] - tv[t][0][0], tv[t][2][1] - tv[t][0][1], tv[t][2][2] - tv[t][0][2] };
            vec3 cr;
            glm_vec3_cross(e1, e2, cr);
            area = 0.5 * glm_vec3_norm(cr);
            for (int k = 0; k < 3; k++) {
                for (int a = 0; a < 3; a++) {
                    if (tv[t][k][a] < bmin[a]) bmin[a] = tv[t][k][a];
                    if (tv[t][k][a] > bmax[a]) bmax[a] = tv[t][k][a];
                }
            }
        }
        total_area += area;
        cumulative[t] = total_area;
    }
    if (total_area <= 0.0) {
        EZ_FREE(mesh_of);
        EZ_FREE(tv);
        EZ_FREE(cumulative);
        return NULL;
    }
    for (int a = 0; a < 3; a++) center[a] = 0.5f * (bmin[a] + bmax[a]);
    float dx = bmax[0] - bmin[0], dy = bmax[1] - bmin[1], dz = bmax[2] - bmin[2];
    *radius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
    if (*radius < 1e-4f) *radius = 1.0f;
    GaussianSplat* out = EZ_ALLOC(want, sizeof(GaussianSplat));
    float* seed_geo = EZ_ALLOC((size_t)want * 6, sizeof(float));
    float spacing = (float)sqrt(total_area / (double)want);
    g_seed = 0xC0FFEEu;
    for (uint32_t i = 0; i < want; i++) {
        double target = (double)RandomOne() * total_area;
        size_t lo = 0, hi = tri_count - 1;
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (cumulative[mid] < target) lo = mid + 1; else hi = mid;
        }
        size_t t = lo;
        float r1 = sqrtf(RandomOne()), r2 = RandomOne();
        float u = 1.0f - r1, v = r1 * (1.0f - r2), w = r1 * r2;
        GaussianSplat* g = &out[i];
        for (int a = 0; a < 3; a++) g->position[a] = u * tv[t][0][a] + v * tv[t][1][a] + w * tv[t][2][a];
        vec3 e1 = { tv[t][1][0] - tv[t][0][0], tv[t][1][1] - tv[t][0][1], tv[t][1][2] - tv[t][0][2] };
        vec3 e2 = { tv[t][2][0] - tv[t][0][0], tv[t][2][1] - tv[t][0][1], tv[t][2][2] - tv[t][0][2] };
        vec3 n, t1, t2;
        glm_vec3_cross(e1, e2, n);
        glm_vec3_normalize(n);
        glm_vec3_copy(e1, t1);
        glm_vec3_normalize(t1);
        glm_vec3_cross(n, t1, t2);
        glm_vec3_normalize(t2);
        QuaternionFromBasis(t1, t2, n, g->rotation);
        memcpy(&seed_geo[(size_t)i * 6], g->position, sizeof(vec3));
        memcpy(&seed_geo[(size_t)i * 6 + 3], n, sizeof(vec3));
        float s = 0.6f * spacing;
        g->logscale[0] = logf(s);
        g->logscale[1] = logf(s);
        g->logscale[2] = logf(0.25f * s);
        g->opacity = logf(0.3f / 0.7f);
        float color[3] = { 0.5f, 0.5f, 0.5f };
        MaterialID mid = geo->triangles.data[t].material;
        if (mid < geo->materials.size) {
            const SurfaceMaterial* mat = &geo->materials.data[mid];
            for (int a = 0; a < 3; a++) {
                float c = mat->diffuse[a] + mat->emission[a];
                color[a] = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
            }
        }
        for (int a = 0; a < 3; a++) g->shdc[a] = (color[a] - 0.5f) / MIN_SH_C0;
    }
    EZ_FREE(mesh_of);
    EZ_FREE(tv);
    EZ_FREE(cumulative);
    *out_count = want;
    *out_geo = seed_geo;
    return out;
}

// World space copy of every triangle the path tracer can see (disabled meshes are skipped).
static float (*CollectVisibleTriangles(size_t* out_count))[3][3] {
    Geometry* geo = RendererGeometry();
    size_t tri_count = geo->triangles.size;
    *out_count = 0;
    if (tri_count == 0) return NULL;
    int32_t* mesh_of = EZ_ALLOC(tri_count, sizeof(int32_t));
    for (size_t i = 0; i < tri_count; i++) mesh_of[i] = -1;
    for (size_t m = 0; m < geo->meshes.size; m++) {
        const MeshDescriptor* md = &geo->meshes.data[m];
        for (size_t t = md->tstart; t <= md->tend && t < tri_count; t++) mesh_of[t] = (int32_t)m;
    }
    float (*out)[3][3] = EZ_ALLOC(tri_count, sizeof(*out));
    size_t n = 0;
    for (size_t t = 0; t < tri_count; t++) {
        const MeshDescriptor* md = mesh_of[t] >= 0 ? &geo->meshes.data[mesh_of[t]] : NULL;
        if (md && md->disabled) continue;
        const Triangle* tri = &geo->triangles.data[t];
        VertexID ids[3] = { tri->a, tri->b, tri->c };
        for (int k = 0; k < 3; k++) {
            vec3 p = { geo->vertices.data[ids[k]][0], geo->vertices.data[ids[k]][1], geo->vertices.data[ids[k]][2] };
            vec3 world;
            if (md) {
                mat4 m;
                memcpy(m, md->transform, sizeof(mat4));
                glm_mat4_mulv3(m, p, 1.0f, world);
            } else {
                glm_vec3_copy(p, world);
            }
            memcpy(out[n][k], world, sizeof(vec3));
        }
        n++;
    }
    EZ_FREE(mesh_of);
    *out_count = n;
    return out;
}

static void PlanCameras(TrainingJob* j) {
    const TrainingSceneConfig* c = &j->config;
    j->cameras = EZ_ALLOC(c->views, sizeof(SimpleCamera));
    float fov_h = c->fov * (float)M_PI / 180.0f;
    float half_h = 0.5f * fov_h;
    float half_v = atanf(tanf(half_h) * (float)j->height / (float)j->width);
    float half = half_h < half_v ? half_h : half_v;
    float distance = c->distance_scale * j->radius / sinf(half) * 1.05f;
    float sin_min = sinf(c->min_elevation * (float)M_PI / 180.0f);
    for (uint32_t i = 0; i < c->views; i++) {
        float f = (c->views > 1) ? ((float)i + 0.5f) / (float)c->views : 0.5f;
        float y = 1.0f - f * (1.0f - sin_min);
        float r = sqrtf(fmaxf(0.0f, 1.0f - y * y));
        float theta = 2.399963f * (float)i;
        SimpleCamera* cam = &j->cameras[i];
        cam->position[0] = j->center[0] + distance * r * cosf(theta);
        cam->position[1] = j->center[1] + distance * y;
        cam->position[2] = j->center[2] + distance * r * sinf(theta);
        memcpy(cam->look, j->center, sizeof(float) * 3);
        cam->up[0] = 0; cam->up[1] = 1; cam->up[2] = 0;
        if (fabsf(y) > 0.995f) { cam->up[1] = 0; cam->up[2] = 1; }
        cam->fov = c->fov;
    }
}

static void DownscaleRGBA(const uint8_t* src, uint32_t sw, uint32_t sh, uint8_t* dst, uint32_t dw, uint32_t dh) {
    float fx = (float)sw / (float)dw, fy = (float)sh / (float)dh;
    for (uint32_t y = 0; y < dh; y++) {
        uint32_t y0 = (uint32_t)floorf(y * fy), y1 = (uint32_t)ceilf((y + 1) * fy);
        if (y1 > sh) y1 = sh;
        if (y1 <= y0) y1 = y0 + 1;
        for (uint32_t x = 0; x < dw; x++) {
            uint32_t x0 = (uint32_t)floorf(x * fx), x1 = (uint32_t)ceilf((x + 1) * fx);
            if (x1 > sw) x1 = sw;
            if (x1 <= x0) x1 = x0 + 1;
            float acc[3] = { 0, 0, 0 };
            for (uint32_t yy = y0; yy < y1; yy++)
                for (uint32_t xx = x0; xx < x1; xx++)
                    for (int k = 0; k < 3; k++) acc[k] += src[((size_t)yy * sw + xx) * 4 + k];
            float inv = 1.0f / (float)((y1 - y0) * (x1 - x0));
            for (int k = 0; k < 3; k++) dst[((size_t)y * dw + x) * 4 + k] = (uint8_t)(acc[k] * inv + 0.5f);
            dst[((size_t)y * dw + x) * 4 + 3] = 255;
        }
    }
}

// The exact pinhole the trainer will use for this view (mirrors SetTrainerView).
static void MakeMaskCamera(const TrainingJob* j, const SimpleCamera* cam, MaskCamera* out) {
    vec3 u = { 0 }, v = { 0 }, w = { 0 }, forward = { 0 };
    glm_vec3_sub((float*)(cam->look), (float*)(cam->position), forward);
    glm_vec3_normalize(forward);
    CameraUVW(*cam, u, v, w);
    glm_vec3_copy((float*)(cam->position), out->eye);
    glm_vec3_copy(u, out->right);
    glm_vec3_scale(v, -1.0f, out->down);
    glm_vec3_copy(forward, out->fwd);
    out->focal = ((float)j->width * 0.5f) / tanf(cam->fov * (float)M_PI / 360.0f);
}

static void BeginCaptureState(TrainingJob* j) {
    SceneCamera* cam = RendererCamera();
    j->saved_camera = *cam;
    j->flags_before = cam->config.flags;
    cam->config.flags = j->config.capture == TRAINSCENE_CAPTURE_PATHTRACE ? HEADLESS_PIPELINE_FLAGS : 0x1FF;
    cam->config.grid = FALSE;
    cam->config.wireframe = FALSE;
    cam->config.async = FALSE;
    cam->config.multiplier = 1;
    cam->config.showdof = FALSE;
    cam->config.debug = DEBUG_NONE;
}

static void EndCaptureState(TrainingJob* j) {
    SceneCamera* cam = RendererCamera();
    cam->core = j->saved_camera.core;
    cam->config.flags = j->saved_camera.config.flags;
    cam->config.grid = j->saved_camera.config.grid;
    cam->config.wireframe = j->saved_camera.config.wireframe;
    cam->config.async = j->saved_camera.config.async;
    cam->config.multiplier = j->saved_camera.config.multiplier;
    cam->config.showdof = j->saved_camera.config.showdof;
    cam->config.debug = j->saved_camera.config.debug;
    cam->config.reset = TRUE;
}

// Overlap (IoU) between the scene geometry's silhouette and the image's non black pixels when the image is read with
// the given flips. The silhouette comes from the exact camera the trainer uses, so whichever flip lines the two up
// is the way the captured image really relates to that camera.
static float FlipScore(const uint8_t* img, const float* coverage, uint32_t w, uint32_t h, int fx, int fy) {
    size_t inter = 0, uni = 0;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t sy = fy ? h - 1 - y : y;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t sx = fx ? w - 1 - x : x;
            const uint8_t* p = &img[((size_t)sy * w + sx) * 4];
            int fg = (p[0] > 10 || p[1] > 10 || p[2] > 10);
            int m = coverage[(size_t)y * w + x] > 0.5f;
            inter += (fg && m);
            uni += (fg || m);
        }
    }
    return uni ? (float)inter / (float)uni : 0.0f;
}

static void FlipImage(uint8_t* img, uint32_t w, uint32_t h, int fx, int fy) {
    if (fy) {
        for (uint32_t y = 0; y < h / 2; y++) {
            uint8_t* a = &img[(size_t)y * w * 4];
            uint8_t* b = &img[(size_t)(h - 1 - y) * w * 4];
            for (size_t k = 0; k < (size_t)w * 4; k++) { uint8_t t = a[k]; a[k] = b[k]; b[k] = t; }
        }
    }
    if (fx) {
        for (uint32_t y = 0; y < h; y++) {
            uint8_t* row = &img[(size_t)y * w * 4];
            for (uint32_t x = 0; x < w / 2; x++)
                for (int k = 0; k < 4; k++) { uint8_t t = row[x * 4 + k]; row[x * 4 + k] = row[(w - 1 - x) * 4 + k]; row[(w - 1 - x) * 4 + k] = t; }
        }
    }
}

static BOOL CaptureView(TrainingJob* j, uint32_t index) {
    SceneCamera* cam = RendererCamera();
    cam->core.position[0] = j->cameras[index].position[0];
    cam->core.position[1] = j->cameras[index].position[1];
    cam->core.position[2] = j->cameras[index].position[2];
    memcpy(cam->core.look, j->cameras[index].look, sizeof(float) * 3);
    memcpy(cam->core.up, j->cameras[index].up, sizeof(float) * 3);
    cam->core.fov = j->cameras[index].fov;
    cam->core.aperature = 0.0f;
    cam->core.focus = 0.0f;
    cam->config.reset = TRUE;
    Vector2 res = RenderResolution();
    SetViewportSlice((size_t)res.x, (size_t)res.y);
    uint32_t frames = j->config.capture == TRAINSCENE_CAPTURE_PATHTRACE ? j->config.samples + 4 : 4;
    for (uint32_t f = 0; f < frames; f++) Render();
    if (!CaptureRender(j->source)) return FALSE;
    DownscaleRGBA(j->source, (uint32_t)res.x, (uint32_t)res.y, j->scaled, j->width, j->height);
    // Work out which pixels the scene geometry really covers. Everything else is "the path hit nothing": it must be
    // explained by empty space, not by (black) gaussians. The image becomes premultiplied and its alpha the coverage.
    MaskCamera mc;
    MakeMaskCamera(j, &j->cameras[index], &mc);
    RasterizeCoverage((const float (*)[3][3])j->tris, j->tri_count, &mc, j->width, j->height, j->scratch, j->coverage, j->depth);
    // Make sure the image really is oriented the way the trainer's camera model assumes (see FlipScore). A vertically
    // or horizontally mirrored capture would otherwise make the images and the geometry describe different objects.
    float scores[4];
    int best = 0;
    for (int f = 0; f < 4; f++) {
        scores[f] = FlipScore(j->scaled, j->coverage, j->width, j->height, f & 1, f >> 1);
        if (scores[f] > scores[best]) best = f;
    }
    if (best != 0 && scores[best] < scores[0] + 0.03f) best = 0; // no clear winner, trust the camera model
    j->orient_votes[best]++;
    if (best != 0) FlipImage(j->scaled, j->width, j->height, best & 1, best >> 1);
    float bg[3] = { 0, 0, 0 };
    ApplyCoverageToImage(j->scaled, j->coverage, j->width, j->height, bg);
    for (int k = 0; k < 3; k++) j->bg_sum[k] += bg[k];
    j->bg_n += 1.0;
    // sample this view's colours for the seed gaussians so that they do not all start out as the material colour
    AccumulateSeedColors(j->seed_geo, j->seed_count, &mc, j->width, j->height, j->scaled, j->depth, j->radius, j->seed_acc);
    return SetTrainerView(&j->trainer, index, &j->cameras[index], j->scaled);
}

static void ReleaseJob(TrainingJob* j) {
    if (j->trainer_alive) DestroyTrainer(&j->trainer);
    j->trainer_alive = FALSE;
    EZ_FREE(j->cameras); j->cameras = NULL;
    EZ_FREE(j->source); j->source = NULL;
    EZ_FREE(j->scaled); j->scaled = NULL;
    EZ_FREE(j->tris); j->tris = NULL; j->tri_count = 0;
    EZ_FREE(j->coverage); j->coverage = NULL;
    EZ_FREE(j->depth); j->depth = NULL;
    EZ_FREE(j->scratch); j->scratch = NULL;
    EZ_FREE(j->seeds); j->seeds = NULL;
    EZ_FREE(j->seed_geo); j->seed_geo = NULL;
    EZ_FREE(j->seed_acc); j->seed_acc = NULL;
    j->seed_count = 0;
}

static void FailJob(TrainingJob* j, const char* why) {
    logerror("Gaussian splat training failed: %s", why);
    snprintf(j->status, sizeof(j->status), "Failed: %s", why);
    if (j->captured > 0 || j->phase == TRAINSCENE_CAPTURING) EndCaptureState(j);
    ReleaseJob(j);
    j->phase = TRAINSCENE_FAILED;
}

static void FinishCapture(TrainingJob* j) {
    EndCaptureState(j);
    // The captured images are premultiplied (background removed), so the trainer renders over black.
    for (int k = 0; k < 3; k++) {
        j->background[k] = j->bg_n > 0 ? (float)(j->bg_sum[k] / j->bg_n) : 0.0f; // only reported
        j->trainer.cfg.background[k] = 0.0f;
    }
    // start every seed from the colour the views actually showed instead of the material colour
    uint32_t colored = 0;
    for (uint32_t i = 0; i < j->seed_count; i++) {
        const float* a = &j->seed_acc[(size_t)i * 4];
        if (a[3] <= 1e-4f) continue;
        for (int k = 0; k < 3; k++) {
            float c = a[k] / a[3];
            c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
            j->seeds[i].shdc[k] = (c - 0.5f) / MIN_SH_C0;
        }
        colored++;
    }
    if (!SetTrainerGaussians(&j->trainer, j->seeds, j->seed_count)) logwarn("Could not re-upload the coloured seed gaussians");
    EZ_FREE(j->source); j->source = NULL;
    EZ_FREE(j->scaled); j->scaled = NULL;
    EZ_FREE(j->tris); j->tris = NULL; j->tri_count = 0;
    EZ_FREE(j->coverage); j->coverage = NULL;
    EZ_FREE(j->depth); j->depth = NULL;
    EZ_FREE(j->scratch); j->scratch = NULL;
    EZ_FREE(j->seeds); j->seeds = NULL;
    EZ_FREE(j->seed_geo); j->seed_geo = NULL;
    EZ_FREE(j->seed_acc); j->seed_acc = NULL;
    // Path traced captures went through tonemap.comp (extended Reinhard + gamma), so the colours the trainer learns are
    // display values. The path tracer adds splat albedo into its linear HDR buffer and tonemaps afterwards, so exported
    // colours are converted back to linear radiance; otherwise they get gamma encoded twice and come out too bright.
    if (j->config.capture == TRAINSCENE_CAPTURE_PATHTRACE && j->saved_camera.config.gamma > 0.0f && j->saved_camera.config.whitepoint > 0.0f) {
        j->trainer.export_whitepoint = j->saved_camera.config.whitepoint;
        j->trainer.export_gamma = j->saved_camera.config.gamma;
        loginfo("Exporting splat colours as linear radiance (inverse tonemap: whitepoint %.1f, gamma %.2f)", j->trainer.export_whitepoint, j->trainer.export_gamma);
    }
    j->phase = TRAINSCENE_TRAINING;
    loginfo("Capture orientation check (views per orientation): as-is %u, flipped vertically %u, flipped horizontally %u, both %u",
        j->orient_votes[0], j->orient_votes[2], j->orient_votes[1], j->orient_votes[3]);
    if (j->orient_votes[0] < j->captured)
        logwarn("Some captured views did not line up with the trainer camera and were flipped to match");
    loginfo("Captured %u views (background %.2f %.2f %.2f removed), %u/%u seeds coloured from the views, optimising...",
        j->captured, j->background[0], j->background[1], j->background[2], colored, j->seed_count);
    j->seed_count = 0;
}

static void FinishTraining(TrainingJob* j) {
    uint32_t before = j->trainer.count;
    if (FinalPrune(&j->trainer, 0.02f)) loginfo("Removed %u nearly transparent gaussians", before - j->trainer.count);
    double psnr = 0;
    if (EvaluateTrainerPSNR(&j->trainer, &psnr)) loginfo("Training finished: %u gaussians, mean PSNR %.2f dB over the training views", j->trainer.count, psnr);
    if (j->config.output_ply && j->config.output_ply[0]) {
        if (SaveTrainerPLY(&j->trainer, j->config.output_ply)) loginfo("Saved splats to \"%s\"", j->config.output_ply);
        else logerror("Unable to write \"%s\"", j->config.output_ply);
    }
    if (j->config.replace_splats) {
        ClearScene(FALSE);
        GaussianSplat* result = EZ_ALLOC(1, (size_t)j->trainer.count * sizeof(GaussianSplat));
        uint32_t n = DownloadFromTrainer(&j->trainer, result, j->trainer.count);
        if (n > 0) {
            for (uint32_t i = 0; i < n; i++) {
                GaussianSplat s = { 0 };
                memcpy(s.position, result[i].position, 12);
                memcpy(s.logscale, result[i].logscale, 12);
                memcpy(s.rotation, result[i].rotation, 16);
                s.opacity = result[i].opacity;
                memcpy(s.shdc, result[i].shdc, 12);
                SubmitSplat(s);
            }
            loginfo("Loaded %u trained gaussians into the renderer", n);
        }
        EZ_FREE(result);
    }
    snprintf(j->status, sizeof(j->status), "Done: %u gaussians in %.0fs", j->trainer.count, GetTime() - j->started_at);
    DestroyTrainer(&j->trainer);
    j->trainer_alive = FALSE;
    EZ_FREE(j->cameras); j->cameras = NULL;
    j->phase = TRAINSCENE_DONE;
}

void DestroyTrainer(Trainer* t) {
    FreeTrainerResources(t);
    tgpu_destroy(&t->gpu);
    memset(t, 0, sizeof(*t));
}

BOOL InitializeTrainer(Trainer* t, const TrainerConfig* cfg, uint32_t width, uint32_t height, uint32_t views) {
    memset(t, 0, sizeof(*t));
    t->cfg = *cfg;
    t->width = width;
    t->height = height;
    t->tiles_x = (width + TILE - 1) / TILE;
    t->tiles_y = (height + TILE - 1) / TILE;
    t->num_views = views;
    t->rng = 0x9E3779B9u;
    if (t->tiles_x * t->tiles_y > 65535) {
        logerror("Unable to initialize trainer, image too large: %ux%u needs more than 65535 tiles", width, height);
        return FALSE;
    }
    if (tgpu_create(&t->gpu, cfg->gpu_index, 0) != 0) return FALSE;
    VkPhysicalDeviceProperties props = { 0 };
    vkGetPhysicalDeviceProperties(t->gpu.gpu, &props);
    VkDeviceSize gt_bytes = (VkDeviceSize)width * height * views * 4;
    if (gt_bytes > props.limits.maxStorageBufferRange) {
        logerror("Unable to initialize trainer, training images need %llu bytes but the device allows %u per storage buffer; use fewer views or a lower resolution", (unsigned long long)gt_bytes, props.limits.maxStorageBufferRange);
        tgpu_destroy(&t->gpu);
        return FALSE;
    }
    t->capacity = cfg->max_gaussians;
    t->views = EZ_ALLOC(views ? views : 1, sizeof(TrainerView));
    t->view_order = EZ_ALLOC(views ? views : 1, sizeof(uint32_t));
    for (uint32_t i = 0; i < views; i++) t->view_order[i] = i;
    BOOL result = AllocateGaussianBuffers(t, t->capacity);
    uint32_t pixels = width * height;
    uint32_t groups = (pixels + 255) / 256;
    result |= tgpu_buffer_create(&t->gpu, &t->buffers.hist, 16 * SORT_GROUPS * 4) == 0;
    result |= tgpu_buffer_create(&t->gpu, &t->buffers.ranges, (VkDeviceSize)t->tiles_x * t->tiles_y * 8) == 0;
    result |= tgpu_buffer_create(&t->gpu, &t->buffers.meta, 64) == 0;
    result |= tgpu_buffer_create(&t->gpu, &t->buffers.render, (VkDeviceSize)pixels * 16) == 0;
    result |= tgpu_buffer_create(&t->gpu, &t->buffers.nconts, (VkDeviceSize)pixels * 4) == 0;
    result |= tgpu_buffer_create(&t->gpu, &t->buffers.gt, gt_bytes) == 0;
    result |= tgpu_buffer_create(&t->gpu, &t->buffers.dldpix, (VkDeviceSize)pixels * 16) == 0;
    result |= tgpu_buffer_create(&t->gpu, &t->buffers.ssim, (VkDeviceSize)pixels * 36) == 0;
    result |= tgpu_buffer_create(&t->gpu, &t->buffers.loss, (VkDeviceSize)groups * 8) == 0;
    uint32_t pair_cap = 1u << 21;
    if (result) result |= AllocatePairs(t, pair_cap);
    if (!result) {
        logerror("Unable to initialize trainer, out of GPU memory while allocating training buffers");
        FreeTrainerResources(t);
        tgpu_destroy(&t->gpu);
        return FALSE;
    }
    tgpu_begin(&t->gpu);
    tgpu_fill(&t->gpu, &t->buffers.params, 0, t->buffers.params.size, 0);
    tgpu_fill(&t->gpu, &t->buffers.grads, 0, t->buffers.grads.size, 0);
    tgpu_fill(&t->gpu, &t->buffers.m, 0, t->buffers.m.size, 0);
    tgpu_fill(&t->gpu, &t->buffers.v, 0, t->buffers.v.size, 0);
    tgpu_fill(&t->gpu, &t->buffers.dens, 0, t->buffers.dens.size, 0);
    tgpu_fill(&t->gpu, &t->buffers.meta, 0, t->buffers.meta.size, 0);
    tgpu_fill(&t->gpu, &t->buffers.geom, 0, t->buffers.geom.size, 0);
    if (tgpu_submit(&t->gpu)) {
        logerror("Unable to initialize trainer, something went wrong when zeroing resources");
        FreeTrainerResources(t);
        tgpu_destroy(&t->gpu);
        return FALSE;
    }
    BindAllBuffers(t);
    TGPUKernel* ks[] = {
        &t->kernels.preprocess,
        &t->kernels.scan_block,
        &t->kernels.scan_top,
        &t->kernels.scan_add,
        &t->kernels.setup,
        &t->kernels.duplicate,
        &t->kernels.sort_hist,
        &t->kernels.sort_scan,
        &t->kernels.sort_scatter,
        &t->kernels.ranges,
        &t->kernels.render_fwd,
        &t->kernels.render_bwd,
        &t->kernels.loss_stats,
        &t->kernels.loss_grad,
        &t->kernels.preprocess_bwd,
        &t->kernels.adam,
        &t->kernels.densify_flags,
        &t->kernels.densify_apply,
        &t->kernels.opacity_reset
    };
    for (size_t i = 0; i < sizeof(ks) / sizeof(ks[0]); i++) {
        char path[1024];
        const char* name = g_kernel_names[i];
        snprintf(path, sizeof(path), "shaders/%s.comp.spv", name);
        if (tgpu_kernel_create(&t->gpu, ks[i], path)) {
            logerror("Unable to initialize trainer, could not create kernels");
            FreeTrainerResources(t);
            tgpu_destroy(&t->gpu);
            return FALSE;
        }
    }
    t->ready = 1;
    return TRUE;
}

void SetTrainerScene(Trainer* t, const vec3 center, float radius) {
    memcpy(t->scene_center, center, sizeof(float) * 3);
    t->scene_radius = radius > 1e-6f ? radius : 1.0f;
    for (uint32_t i = 0; i < t->num_views; i++) {
        TrainerView* v = &t->views[i];
        float rel[3] = { center[0] - v->eye[0], center[1] - v->eye[1], center[2] - v->eye[2] };
        float dc = rel[0] * v->fwd[0] + rel[1] * v->fwd[1] + rel[2] * v->fwd[2];
        float margin = t->scene_radius * 1.75f;
        float dmin = dc - margin;
        if (dmin < 0.01f) dmin = 0.01f;
        float dmax = dc + margin;
        if (dmax <= dmin + 1e-3f) dmax = dmin + 1.0f;
        v->depth_min = dmin;
        v->depth_scale = 65535.0f / (dmax - dmin);
    }
}

BOOL SetTrainerView(Trainer* t, uint32_t index, const SimpleCamera* cam, const uint8_t* rgba) {
    if (index >= t->num_views) return FALSE;
    TrainerView* view = &t->views[index];
    vec3 u = { 0 }, v = { 0 }, w = { 0 }, forward = { 0 };
    glm_vec3_sub((float*)(cam->look), (float*)(cam->position), forward);
    glm_vec3_normalize(forward);
    CameraUVW(*cam, u, v, w);
    glm_vec3_copy((float*)(cam->position), view->eye);
    glm_vec3_copy(u, view->right);
    glm_vec3_scale(v, -1.0f, view->down);
    glm_vec3_copy(forward, view->fwd);
    view->focal = ((float)t->width * 0.5f) / tanf(cam->fov * (float)M_PI / 360.0f);
    view->depth_min = 0.01f;
    view->depth_scale = 65535.0f / 100.0f;
    if (t->scene_radius > 0.0f) SetTrainerScene(t, t->scene_center, t->scene_radius);
    size_t bytes = (size_t)t->width * t->height * 4;
    return tgpu_upload(&t->gpu, &t->buffers.gt, (VkDeviceSize)index * bytes, rgba, bytes) == 0;
}

BOOL SetTrainerGaussians(Trainer* t, const GaussianSplat* g, uint32_t count) {
    if (count == 0 || count > t->capacity) {
        logerror("Gaussian count %u is outside of capacity %u\n", count, t->capacity);
        return FALSE;
    }
    float* host = EZ_ALLOC((size_t)count * PSTRIDE, sizeof(float));
    for (uint32_t i = 0; i < count; i++) {
        float* p = host + (size_t)i * PSTRIDE;
        memcpy(p + 0, g[i].position, 12);
        memcpy(p + 3, g[i].logscale, 12);
        memcpy(p + 6, g[i].rotation, 16);
        p[10] = g[i].opacity;
        memcpy(p + 11, g[i].shdc, 12);
    }
    int r = tgpu_upload(&t->gpu, &t->buffers.params, 0, host, (VkDeviceSize)count * PSTRIDE * 4);
    EZ_FREE(host);
    if (r) return FALSE;
    tgpu_begin(&t->gpu);
    tgpu_fill(&t->gpu, &t->buffers.m, 0, t->buffers.m.size, 0);
    tgpu_fill(&t->gpu, &t->buffers.v, 0, t->buffers.v.size, 0);
    tgpu_fill(&t->gpu, &t->buffers.dens, 0, t->buffers.dens.size, 0);
    if (tgpu_submit(&t->gpu)) return FALSE;
    t->count = count;
    t->iteration = 0;
    return TRUE;
}

BOOL TrainerDone(const Trainer* t) {
    return t->iteration >= t->cfg.iterations;
}

BOOL StepTrainer(Trainer* t) {
    if (!t->ready || t->count == 0 || t->num_views == 0) return FALSE;
    uint32_t view = NextView(t);
    uint32_t iter = t->iteration + 1;
    float progress = t->cfg.iterations > 1 ? (float)t->iteration / (float)(t->cfg.iterations - 1) : 1.0f;
    float lrp = t->scene_radius * expf((1.0f - progress) * logf(t->cfg.lr_position_init) + progress * logf(t->cfg.lr_position_final));
    AdamPC ap = { 0 };
    ap.lr_a[0] = lrp;
    ap.lr_a[1] = t->cfg.lr_scale;
    ap.lr_a[2] = t->cfg.lr_rotation;
    ap.lr_a[3] = t->cfg.lr_opacity;
    ap.lr_b[0] = t->cfg.lr_color;
    ap.lr_b[1] = 0.9f;
    ap.lr_b[2] = 0.999f;
    ap.lr_b[3] = 1e-15f;
    ap.bias[0] = 1.0f - powf(0.9f, (float)iter);
    ap.bias[1] = 1.0f - powf(0.999f, (float)iter);
    ap.cnt[0] = t->count;
    tgpu_begin(&t->gpu);
    RecordForward(t, view);
    RecordLossAndBackward(t, view);
    tgpu_dispatch(&t->gpu, &t->kernels.adam, GroupsFor(t->count * PSTRIDE, 256), 1, 1, &ap, sizeof(ap));
    if (tgpu_submit(&t->gpu)) return FALSE;
    t->iteration = iter;
    int logging = t->cfg.log_interval > 0 && (iter % t->cfg.log_interval == 0 || iter == 1);
    if (logging) {
        double loss, l1, ss;
        if (!ReadLoss(t, &loss, &l1, &ss)) return FALSE;
        t->last_loss = (float)loss;
        t->last_l1 = (float)l1;
        t->last_ssim = (float)ss;
    }
    if (iter % 8 == 0 || iter == 1) {
        if (!RefreshPairStats(t)) return FALSE;
    }
    if (iter < t->cfg.densify_until && iter >= t->cfg.densify_from && iter % t->cfg.densify_interval == 0) {
        if (!Densify(t)) return FALSE;
        if (!RefreshPairStats(t)) return FALSE;
    }
    if (t->cfg.opacity_reset_interval > 0 && iter % t->cfg.opacity_reset_interval == 0 && iter < t->cfg.densify_until) {
        if (!ResetOpacity(t)) return FALSE;
    }
    return TRUE;
}

// Exact inverse of tonemap.comp for one channel: extended Reinhard followed by pow(1 / gamma).
// display -> linear radiance. d is clamped just below 1 where the curve gets steep.
static float InverseTonemap(float d, float whitepoint, float gamma) {
    d = d < 0.0f ? 0.0f : (d > 0.999f ? 0.999f : d);
    float x = powf(d, gamma);
    // solve x * (x / wp + 1) / (1 + x) = h  for x
    return 0.5f * whitepoint * ((x - 1.0f) + sqrtf((1.0f - x) * (1.0f - x) + 4.0f * x / whitepoint));
}

uint32_t DownloadFromTrainer(Trainer* t, GaussianSplat* out, uint32_t max) {
    uint32_t n = t->count < max ? t->count : max;
    float* host = EZ_ALLOC(1, (size_t)n * PSTRIDE * 4);
    if (tgpu_download(&t->gpu, &t->buffers.params, 0, host, (VkDeviceSize)n * PSTRIDE * 4)) { EZ_FREE(host); return 0; }
    for (uint32_t i = 0; i < n; i++) {
        const float* p = host + (size_t)i * PSTRIDE;
        memcpy(out[i].position, p + 0, 12);
        memcpy(out[i].logscale, p + 3, 12);
        memcpy(out[i].rotation, p + 6, 16);
        out[i].opacity = p[10];
        memcpy(out[i].shdc, p + 11, 12);
        if (t->export_gamma > 0.0f && t->export_whitepoint > 0.0f) {
            for (int k = 0; k < 3; k++) {
                float display = 0.5f + MIN_SH_C0 * out[i].shdc[k];
                out[i].shdc[k] = (InverseTonemap(display, t->export_whitepoint, t->export_gamma) - 0.5f) / MIN_SH_C0;
            }
        }
    }
    EZ_FREE(host);
    return n;
}

BOOL SaveTrainerPLY(Trainer* t, const char* path) {
    GaussianSplat* g = EZ_ALLOC(1, (size_t)t->count * sizeof(GaussianSplat));
    uint32_t n = DownloadFromTrainer(t, g, t->count);
    if (n != t->count) {
        EZ_FREE(g);
        return FALSE;
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        logerror("Could not write to \"%s\"", path);
        EZ_FREE(g);
        return FALSE;
    }
    fprintf(f, "ply\nformat binary_little_endian 1.0\nelement vertex %u\n", n);
    const char* props[] = {
        "x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
        "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"
    };
    for (size_t i = 0; i < sizeof(props) / sizeof(props[0]); i++) fprintf(f, "property float %s\n", props[i]);
    fprintf(f, "end_header\n");
    for (uint32_t i = 0; i < n; i++) {
        float row[17];
        float qn = sqrtf(g[i].rotation[0] * g[i].rotation[0] + g[i].rotation[1] * g[i].rotation[1] +
                         g[i].rotation[2] * g[i].rotation[2] + g[i].rotation[3] * g[i].rotation[3]);
        if (qn < 1e-12f) qn = 1.0f;
        memcpy(row + 0, g[i].position, 12);
        row[1] = -row[1]; // the loader (loader.c) negates y on read, so write the inverse
        row[3] = row[4] = row[5] = 0.0f;
        memcpy(row + 6, g[i].shdc, 12);
        row[9] = g[i].opacity;
        memcpy(row + 10, g[i].logscale, 12);
        for (int k = 0; k < 4; k++) row[13 + k] = g[i].rotation[k] / qn;
        row[14] = -row[14]; // rot_1 and rot_3 are negated on read as well
        row[16] = -row[16];
        fwrite(row, sizeof(float), 17, f);
    }
    fclose(f);
    EZ_FREE(g);
    return TRUE;
}

BOOL RenderTrainerView(Trainer* t, uint32_t view, float* out) {
    tgpu_begin(&t->gpu);
    RecordForward(t, view);
    if (tgpu_submit(&t->gpu)) return FALSE;
    return tgpu_download(&t->gpu, &t->buffers.render, 0, out, (VkDeviceSize)t->width * t->height * 16) == 0;
}

BOOL EvaluateTrainerPSNR(Trainer* t, double* psnr) {
    size_t pixels = (size_t)t->width * t->height;
    float* img = EZ_ALLOC(1, pixels * 16);
    uint8_t* gt = EZ_ALLOC(1, pixels * 4);
    double total = 0;
    for (uint32_t v = 0; v < t->num_views; v++) {
        if (!RenderTrainerView(t, v, img) || tgpu_download(&t->gpu, &t->buffers.gt, (VkDeviceSize)v * pixels * 4, gt, pixels * 4)) {
            EZ_FREE(img);
            EZ_FREE(gt);
            return FALSE;
        }
        double mse = 0;
        for (size_t i = 0; i < pixels; i++) {
            for (int c = 0; c < 3; c++) {
                double d = (double)img[i * 4 + c] - (double)gt[i * 4 + c] / 255.0;
                mse += d * d;
            }
        }
        mse /= (double)(pixels * 3);
        total += mse < 1e-12 ? 120.0 : -10.0 * log10(mse);
    }
    EZ_FREE(img);
    EZ_FREE(gt);
    *psnr = total / (double)(t->num_views ? t->num_views : 1);
    return TRUE;
}

BOOL LossAndGradients(Trainer* t, uint32_t view, double* loss, float* grads) {
    tgpu_begin(&t->gpu);
    RecordForward(t, view);
    RecordLossAndBackward(t, view);
    if (tgpu_submit(&t->gpu)) return FALSE;
    if (!ReadLoss(t, loss, NULL, NULL)) return FALSE;
    if (grads && tgpu_download(&t->gpu, &t->buffers.grads, 0, grads, (VkDeviceSize)t->count * PSTRIDE * 4)) return FALSE;
    tgpu_begin(&t->gpu);
    tgpu_fill(&t->gpu, &t->buffers.dens, 0, t->buffers.dens.size, 0);
    return tgpu_submit(&t->gpu) == 0;
}

TrainingSceneConfig TrainingSceneDefaultConfig() {
    TrainingSceneConfig c = { 0 };
    c.views = 48;
    c.train_width = 400;
    c.samples = 48;
    c.init_points = 20000;
    c.iterations = 7000;
    c.max_gaussians = 400000;
    c.fov = 50.0f;
    c.distance_scale = 1.0f;
    c.min_elevation = -30.0f;
    c.capture = TRAINSCENE_CAPTURE_PATHTRACE;
    c.replace_splats = TRUE;
    c.output_ply = "trained_splats.ply";
    c.trainer = DefaultTrainerConfig();
    return c;
}

BOOL TrainingSceneStart(const TrainingSceneConfig* config) {
    TrainingJob* j = &g_job;
    if (j->phase == TRAINSCENE_CAPTURING || j->phase == TRAINSCENE_TRAINING) return FALSE;
    ReleaseJob(j);
    memset(j, 0, sizeof(*j));
    j->config = *config;
    TrainingSceneConfig* c = &j->config;
    if (c->views < 4) c->views = 4;
    if (c->train_width < 32) c->train_width = 32;
    if (c->init_points < 100) c->init_points = 100;
    if (c->samples < 1) c->samples = 1;
    c->trainer.iterations = c->iterations;
    c->trainer.max_gaussians = c->max_gaussians;
    if (c->init_points > c->max_gaussians) c->init_points = c->max_gaussians;
    // scale the schedule with the iteration budget
    c->trainer.densify_from = c->iterations / 14 > 20 ? c->iterations / 14 : 20;
    c->trainer.densify_until = (uint32_t)(c->iterations * 0.65f);
    c->trainer.opacity_reset_interval = c->iterations / 2 > 100 ? c->iterations / 2 : 100;
    c->trainer.densify_interval = c->iterations >= 3000 ? 100 : 50;
    c->trainer.max_screen_radius = 0.25f;

    Vector2 res = RenderResolution();
    j->width = c->train_width;
    j->height = (uint32_t)((float)c->train_width * res.y / res.x + 0.5f);
    if (j->height < 16) j->height = 16;
    j->started_at = GetTime();

    uint32_t seeded = 0;
    GaussianSplat* seeds = SeedGaussians(c->init_points, &seeded, j->center, &j->radius, &j->seed_geo);
    if (!seeds) {
        snprintf(j->status, sizeof(j->status), "Failed: scene has no triangles to train on");
        logerror("Cannot train Gaussian splats: the scene has no (enabled) triangles");
        j->phase = TRAINSCENE_FAILED;
        return FALSE;
    }

    if (!InitializeTrainer(&j->trainer, &c->trainer, j->width, j->height, c->views)) {
        EZ_FREE(seeds);
        EZ_FREE(j->seed_geo); j->seed_geo = NULL;
        snprintf(j->status, sizeof(j->status), "Failed: could not set up the GPU trainer");
        logerror("Cannot train Gaussian splats: GPU trainer initialisation failed (see stderr)");
        j->phase = TRAINSCENE_FAILED;
        return FALSE;
    }
    j->trainer_alive = TRUE;
    SetTrainerScene(&j->trainer, j->center, j->radius);
    PlanCameras(j);
    j->source = EZ_ALLOC(1, (size_t)res.x * (size_t)res.y * 4);
    j->scaled = EZ_ALLOC(1, (size_t)j->width * j->height * 4);
    j->tris = CollectVisibleTriangles(&j->tri_count);
    j->coverage = EZ_ALLOC((size_t)j->width * j->height, sizeof(float));
    j->depth = EZ_ALLOC((size_t)j->width * j->height, sizeof(float));
    j->scratch = EZ_ALLOC(MaskScratchSize(j->width, j->height), sizeof(float));
    j->seed_acc = EZ_ALLOC((size_t)seeded * 4, sizeof(float));
    j->seed_count = seeded;
    j->seeds = seeds; // kept until the views are captured so their colours can be updated
    if (!SetTrainerGaussians(&j->trainer, seeds, seeded)) {
        FailJob(j, "could not upload the initial gaussians");
        return FALSE;
    }
    BeginCaptureState(j);
    j->phase = TRAINSCENE_CAPTURING;
    snprintf(j->status, sizeof(j->status), "Capturing views 0/%u", c->views);
    loginfo("Training Gaussian splats: %u views at %ux%u, %u seed gaussians, %u iterations",
        c->views, j->width, j->height, seeded, c->iterations);
    return TRUE;
}

BOOL TrainingSceneTick(double seconds) {
    TrainingJob* j = &g_job;
    double start = GetTime();
    if (j->phase == TRAINSCENE_CAPTURING) {
        while (j->captured < j->config.views) {
            if (!CaptureView(j, j->captured)) { FailJob(j, "could not capture a training view"); return FALSE; }
            j->captured++;
            snprintf(j->status, sizeof(j->status), "Capturing views %u/%u", j->captured, j->config.views);
            if (GetTime() - start >= seconds) return TRUE;
        }
        FinishCapture(j);
        return TRUE;
    }
    if (j->phase == TRAINSCENE_TRAINING) {
        while (!TrainerDone(&j->trainer)) {
            if (!StepTrainer(&j->trainer)) { FailJob(j, "GPU trainer error"); return FALSE; }
            if (j->trainer.cfg.log_interval && j->trainer.iteration % (j->trainer.cfg.log_interval * 10) == 0)
                loginfo("Splat training iteration %u/%u  loss %.4f  gaussians %u", j->trainer.iteration, j->trainer.cfg.iterations, j->trainer.last_loss, j->trainer.count);
            snprintf(j->status, sizeof(j->status), "Training %u/%u (%u gaussians, loss %.4f)",
                j->trainer.iteration, j->trainer.cfg.iterations, j->trainer.count, j->trainer.last_loss);
            if (GetTime() - start >= seconds) return TRUE;
        }
        FinishTraining(j);
        return FALSE;
    }
    return FALSE;
}

void TrainingSceneCancel() {
    TrainingJob* j = &g_job;
    if (j->phase != TRAINSCENE_CAPTURING && j->phase != TRAINSCENE_TRAINING) return;
    if (j->phase == TRAINSCENE_CAPTURING) EndCaptureState(j);
    ReleaseJob(j);
    j->phase = TRAINSCENE_IDLE;
    snprintf(j->status, sizeof(j->status), "Cancelled");
    loginfo("Gaussian splat training cancelled");
}

TrainingScenePhase TrainingScenePhaseNow() {
    return g_job.phase;
}

float TrainingSceneProgress() {
    const TrainingJob* j = &g_job;
    float cap = j->config.views > 0 ? (float)j->captured / (float)j->config.views : 0.0f;
    float tr = j->trainer_alive && j->trainer.cfg.iterations > 0 ? (float)j->trainer.iteration / (float)j->trainer.cfg.iterations : 0.0f;
    switch (j->phase) {
        case TRAINSCENE_CAPTURING: return 0.2f * cap;
        case TRAINSCENE_TRAINING: return 0.2f + 0.8f * tr;
        case TRAINSCENE_DONE: return 1.0f;
        default: return 0.0f;
    }
}

const char* TrainingSceneStatus() {
    return g_job.status;
}

BOOL TrainingSceneRunBlocking(const TrainingSceneConfig* config) {
    if (!TrainingSceneStart(config)) return FALSE;
    while (TrainingSceneTick(0.25)) {}
    return g_job.phase == TRAINSCENE_DONE;
}
