#include "renderer.h"
#include "renderer/vulkan/vupdate.h"
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vclean.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/animation.h"
#include "renderer/processor.h"
#include "renderer/simulator.h"
#include "renderer/overlay.h"
#include "renderer/trainer.h"
#include "renderer/rmath.h"
#include "ui/panels/edit.h"
#include <core/config.h>
#include <util/logger.h>
#include <GLFW/glfw3.h>
#include <time.h>

static Renderer g_renderer = { 0 };
static Vector2 g_override_resolution = { 0 };
static float g_rft = 0.0f;
static BOOL g_renderer_init = FALSE;

#ifndef NO_MESH_MANIPULATION_SUPPORT
static cholmod_common g_cholmod = { 0 };
#endif

static void CleanARAP() {
    #ifndef NO_MESH_MANIPULATION_SUPPORT
    {
        EZ_FREE(g_renderer.geometry.arap.values);
        EZ_FREE(g_renderer.geometry.arap.cindices);
        EZ_FREE(g_renderer.geometry.arap.rpointers);
        EZ_FREE(g_renderer.geometry.arap.rcounts);
        EZ_FREE(g_renderer.geometry.arap.cursor);
        EZ_FREE(g_renderer.geometry.arap.diag);
        EZ_FREE(g_renderer.geometry.arap.originals);
        EZ_FREE(g_renderer.geometry.arap.rotations);
        EZ_FREE(g_renderer.geometry.arap.covariance);
        EZ_FREE(g_renderer.geometry.arap.v2f);
        EZ_FREE(g_renderer.geometry.arap.f2v);
        EZ_FREE(g_renderer.geometry.arap.b);
        EZ_FREE(g_renderer.geometry.arap.Ai_back);
        EZ_FREE(g_renderer.geometry.arap.z);
        EZ_FREE(g_renderer.geometry.arap.u);
        EZ_FREE(g_renderer.geometry.arap.normals);
        EZ_FREE(g_renderer.geometry.arap.areas);
        if (g_renderer.geometry.arap.A != NULL) cholmod_free_sparse(&g_renderer.geometry.arap.A, &g_cholmod);
        if (g_renderer.geometry.arap.L != NULL) cholmod_free_factor(&g_renderer.geometry.arap.L, &g_cholmod);
    }
    g_renderer.geometry.arap.values = NULL;
    g_renderer.geometry.arap.cindices = NULL;
    g_renderer.geometry.arap.rpointers = NULL;
    g_renderer.geometry.arap.rcounts = NULL;
    g_renderer.geometry.arap.cursor = NULL;
    g_renderer.geometry.arap.diag = NULL;
    g_renderer.geometry.arap.originals = NULL;
    g_renderer.geometry.arap.rotations = NULL;
    g_renderer.geometry.arap.covariance = NULL;
    g_renderer.geometry.arap.v2f = NULL;
    g_renderer.geometry.arap.f2v = NULL;
    g_renderer.geometry.arap.b = NULL;
    g_renderer.geometry.arap.A = NULL;
    g_renderer.geometry.arap.L = NULL;
    g_renderer.geometry.arap.Ai_back = NULL;
    g_renderer.geometry.arap.z = NULL;
    g_renderer.geometry.arap.u = NULL;
    g_renderer.geometry.arap.normals = NULL;
    g_renderer.geometry.arap.areas = NULL;
    g_renderer.geometry.arap.rows = 0;
    g_renderer.geometry.arap.nnz = 0;
    g_renderer.geometry.arap.max_nnz = 0;
    g_renderer.geometry.arap.max_rows = 0;
    #endif
}

#ifndef NO_MESH_MANIPULATION_SUPPORT
static float EdgeWeight(Edge e) {
    EdgeMeta em = HASHMAP_EdgeGlue_get(&(g_renderer.geometry.glue), e);
    TriangleID tris[2] = { em.a, em.b };
    size_t cp = 0;
    VertexID c[2] = { 0 };
    VertexID a = e.a;
    VertexID b = e.b;
    for (size_t i = 0; i < 2; i++) {
        if (tris[i] == (TriangleID)-1) continue;
        Triangle t = g_renderer.geometry.triangles.data[tris[i]];
        VertexID abc[3] = { t.a, t.b, t.c };
        for (size_t j = 0; j < 3; j++) {
            if (abc[j] != a && abc[j] != b) {
                c[cp] = abc[j];
                cp++;
            }
        }
    }
    EZ_ASSERT(cp == 1 || cp == 2, "Broken connectivity detected");
    float weight = 0.0f;
    for (size_t i = 0; i < cp; i++) {
        vec3 u, v, cross;
        glm_vec3_sub(g_renderer.geometry.vertices.data[a], g_renderer.geometry.vertices.data[c[i]], u);
        glm_vec3_sub(g_renderer.geometry.vertices.data[b], g_renderer.geometry.vertices.data[c[i]], v);
        glm_vec3_cross(u, v, cross);
        weight += glm_vec3_dot(u, v) / glm_vec3_norm(cross);
    }
    em.weight = fmaxf(weight / ((float)cp), 1e-6f);
    glm_vec3_sub(g_renderer.geometry.vertices.data[a], g_renderer.geometry.vertices.data[b], em.pij);
    HASHMAP_EdgeGlue_set(&(g_renderer.geometry.glue), e, em);
    return em.weight;
}
#endif

static void ReconstructARAP() {
    #ifndef NO_MESH_MANIPULATION_SUPPORT
    inline BOOL isunlocked(size_t i) { return g_renderer.geometry.arap.v2f[i] != (size_t)-1; }
    inline void saferealloc(void** ptr, size_t x, size_t y) {
        if (*ptr == NULL) *ptr = EZ_ALLOC(x, y);
        else *ptr = EZ_REALLOC(*ptr, x, y);
    }
    if (g_renderer.geometry.arap.A != NULL) {
        cholmod_free_sparse(&g_renderer.geometry.arap.A, &g_cholmod);
        cholmod_free_factor(&g_renderer.geometry.arap.L, &g_cholmod);
    }
    size_t nnz = 2 * g_renderer.geometry.glue.size + g_renderer.geometry.vertices.size;
    size_t nrows = g_renderer.geometry.vertices.size + 1;
    if (nnz > g_renderer.geometry.arap.max_nnz) {
        saferealloc((void**)&g_renderer.geometry.arap.cindices, nnz, sizeof(size_t));
        saferealloc((void**)&g_renderer.geometry.arap.values, nnz, sizeof(double));
        g_renderer.geometry.arap.max_nnz = nnz;
    }
    if (nrows > g_renderer.geometry.arap.max_rows) {
        saferealloc((void**)&g_renderer.geometry.arap.rpointers, nrows, sizeof(size_t));
        saferealloc((void**)&g_renderer.geometry.arap.rcounts, nrows - 1, sizeof(size_t));
        saferealloc((void**)&g_renderer.geometry.arap.cursor, nrows - 1, sizeof(size_t));
        saferealloc((void**)&g_renderer.geometry.arap.diag, nrows - 1, sizeof(double));
        saferealloc((void**)&g_renderer.geometry.arap.originals, nrows - 1, sizeof(vec4));
        saferealloc((void**)&g_renderer.geometry.arap.v2f, nrows - 1, sizeof(size_t));
        saferealloc((void**)&g_renderer.geometry.arap.f2v, nrows - 1, sizeof(size_t));
        saferealloc((void**)&g_renderer.geometry.arap.Ai_back, nrows - 1, sizeof(int));
        saferealloc((void**)&g_renderer.geometry.arap.rotations, nrows - 1, sizeof(mat3));
        saferealloc((void**)&g_renderer.geometry.arap.b, nrows - 1, sizeof(vec3));
        saferealloc((void**)&g_renderer.geometry.arap.covariance, nrows - 1, sizeof(mat3));
        saferealloc((void**)&g_renderer.geometry.arap.z, nrows - 1, sizeof(vec3));
        saferealloc((void**)&g_renderer.geometry.arap.u, nrows - 1, sizeof(vec3));
        saferealloc((void**)&g_renderer.geometry.arap.normals, nrows - 1, sizeof(vec3));
        saferealloc((void**)&g_renderer.geometry.arap.areas, nrows - 1, sizeof(float));
        g_renderer.geometry.arap.max_rows = nrows;
    }
    memcpy(g_renderer.geometry.arap.originals, g_renderer.geometry.vertices.data, g_renderer.geometry.vertices.size * sizeof(vec4));
    g_renderer.geometry.arap.rows = 0;
    memset(g_renderer.geometry.arap.rcounts, 0, (nrows - 1)*sizeof(size_t));
    for (size_t i = 0; i < g_renderer.geometry.vertices.size; i++) {
        glm_mat3_identity(g_renderer.geometry.arap.rotations[i]);
        glm_vec3_zero(g_renderer.geometry.arap.normals[i]);
        g_renderer.geometry.arap.areas[i] = 0.0f;
        if (HASHMAP_Locks_has(&(g_renderer.geometry.locks), i) && HASHMAP_Locks_get(&(g_renderer.geometry.locks), i)) {
            g_renderer.geometry.arap.v2f[i] = (size_t)-1;
        } else {
            g_renderer.geometry.arap.v2f[i] = g_renderer.geometry.arap.rows;
            g_renderer.geometry.arap.f2v[g_renderer.geometry.arap.rows] = i;
            g_renderer.geometry.arap.rcounts[g_renderer.geometry.arap.rows] = 1;
            g_renderer.geometry.arap.rows++;
        }
    }
    size_t double_free_edges = 0;
    for (size_t i = 0; i < g_renderer.geometry.edges.size; i++) {
        Edge e = g_renderer.geometry.edges.data[i];
        if (isunlocked(e.a) && isunlocked(e.b)) {
            g_renderer.geometry.arap.rcounts[g_renderer.geometry.arap.v2f[e.a]]++;
            g_renderer.geometry.arap.rcounts[g_renderer.geometry.arap.v2f[e.b]]++;
            double_free_edges++;
        }
    }
    g_renderer.geometry.arap.rpointers[0] = 0;
    for (size_t i = 0; i < g_renderer.geometry.arap.rows; i++) {
        g_renderer.geometry.arap.rpointers[i + 1] =
            g_renderer.geometry.arap.rpointers[i] + g_renderer.geometry.arap.rcounts[i];
        g_renderer.geometry.arap.cursor[i] = g_renderer.geometry.arap.rpointers[i];
        g_renderer.geometry.arap.diag[i] = 1e-6;
    }
    for (size_t i = 0; i < g_renderer.geometry.edges.size; i++) {
        Edge e = g_renderer.geometry.edges.data[i];
        double w = EdgeWeight(e);
        size_t fa = g_renderer.geometry.arap.v2f[e.a];
        size_t fb = g_renderer.geometry.arap.v2f[e.b];
        if (isunlocked(e.a) && isunlocked(e.b)) {
            size_t i_index = g_renderer.geometry.arap.cursor[fa]++;
            size_t j_index = g_renderer.geometry.arap.cursor[fb]++;
            g_renderer.geometry.arap.cindices[i_index] = fb;
            g_renderer.geometry.arap.values[i_index] = -w;
            g_renderer.geometry.arap.cindices[j_index] = fa;
            g_renderer.geometry.arap.values[j_index] = -w;
        }
        if (isunlocked(e.a)) g_renderer.geometry.arap.diag[fa] += w;
        if (isunlocked(e.b)) g_renderer.geometry.arap.diag[fb] += w;
    }
    for (size_t i = 0; i < g_renderer.geometry.arap.rows; i++) {
        size_t idx = g_renderer.geometry.arap.cursor[i]++;
        g_renderer.geometry.arap.cindices[idx] = i;
        g_renderer.geometry.arap.values[idx] = g_renderer.geometry.arap.diag[i];
    }
    g_renderer.geometry.arap.nnz = g_renderer.geometry.arap.rows + 2 * double_free_edges;
    g_renderer.geometry.arap.A = cholmod_allocate_sparse(
        g_renderer.geometry.arap.rows,
        g_renderer.geometry.arap.rows,
        g_renderer.geometry.arap.nnz,
        1, 1, -1, CHOLMOD_REAL, &g_cholmod);
    int* Ap = (int*)g_renderer.geometry.arap.A->p;
    int* Ai = (int*)g_renderer.geometry.arap.A->i;
    double* Ax = (double*)g_renderer.geometry.arap.A->x;
    for (size_t i = 0; i <= g_renderer.geometry.arap.rows; i++) Ap[i] = 0;
    for (size_t i = 0; i < g_renderer.geometry.arap.nnz; i++) Ap[g_renderer.geometry.arap.cindices[i] + 1]++;
    for (size_t i = 0; i < g_renderer.geometry.arap.rows; i++) {
        Ap[i + 1] += Ap[i];
        g_renderer.geometry.arap.Ai_back[i] = Ap[i];
    }
    for (size_t i = 0; i < g_renderer.geometry.arap.rows; i++) {
        for (size_t j = g_renderer.geometry.arap.rpointers[i]; j < g_renderer.geometry.arap.rpointers[i + 1]; j++) {
            size_t col = g_renderer.geometry.arap.cindices[j];
            size_t dst = g_renderer.geometry.arap.Ai_back[col]++;
            Ai[dst] = i;
            Ax[dst] = g_renderer.geometry.arap.values[j];
        }
    }
    g_renderer.geometry.arap.L = cholmod_analyze(g_renderer.geometry.arap.A, &g_cholmod);
    cholmod_factorize(g_renderer.geometry.arap.A, g_renderer.geometry.arap.L, &g_cholmod);
    for (size_t i = 0; i < g_renderer.geometry.triangles.size; i++) {
        Triangle tri = g_renderer.geometry.triangles.data[i];
        vec3 p0, p1, p2, e1, e2, cross, n;
        glm_vec3_copy(g_renderer.geometry.arap.originals[tri.a], p0);
        glm_vec3_copy(g_renderer.geometry.arap.originals[tri.b], p1);
        glm_vec3_copy(g_renderer.geometry.arap.originals[tri.c], p2);
        glm_vec3_sub(p1, p0, e1);
        glm_vec3_sub(p2, p0, e2);
        glm_vec3_cross(e1, e2, cross);
        float area = glm_vec3_norm(cross) * 0.5f;
        glm_vec3_normalize_to(cross, n);
        for (int j = 0; j < 3; j++) {
            VertexID vid = (j == 0) ? tri.a : (j == 1) ? tri.b : tri.c;
            glm_vec3_add(g_renderer.geometry.arap.normals[vid], n, g_renderer.geometry.arap.normals[vid]);
            g_renderer.geometry.arap.areas[vid] += area / 3.0f;
        }
    }
    for (size_t i = 0; i < g_renderer.geometry.vertices.size; i++)
        glm_vec3_normalize(g_renderer.geometry.arap.normals[i]);
    #endif
}

static void UnglueTriangle(TriangleID id) {
    Triangle t = *TriangleReference(id);
    VertexID vs[3] = { t.a, t.b, t.c };
    for (size_t i = 0; i < 3; i++) {
        Edge e = { vs[i], vs[(i + 1) % 3] };
        Edge alt = { e.b, e.a };
        Edge key = HASHMAP_EdgeGlue_has(&(g_renderer.geometry.glue), e) ? e : alt;
        EdgeMeta* em = HASHMAP_EdgeGlue_ref(&(g_renderer.geometry.glue), key);
        if (!em) continue;
        if (em->a == id) { em->a = em->b; em->b = (TriangleID)-1; }
        else if (em->b == id) em->b = (TriangleID)-1;
        if (em->a == (TriangleID)-1) {
            HASHMAP_EdgeGlue_remove(&(g_renderer.geometry.glue), key);
            for (size_t j = 0; j < g_renderer.geometry.edges.size; j++) {
                if (g_renderer.geometry.edges.data[j].a == key.a && g_renderer.geometry.edges.data[j].b == key.b) {
                    ARRLIST_Edge_remove(&(g_renderer.geometry.edges), j);
                    break;
                }
            }
        }
    }
    for (size_t i = 0; i < g_renderer.geometry.glue.capacity; i++) {
        if (g_renderer.geometry.glue.entries[i].used != 1) continue;
        EdgeMeta* em = &(g_renderer.geometry.glue.entries[i].value);
        if (em->a != (TriangleID)-1 && em->a > id) em->a--;
        if (em->b != (TriangleID)-1 && em->b > id) em->b--;
    }
}

static void RebuildConnectivity() {
    HASHMAP_EdgeGlue_clear(&(g_renderer.geometry.glue));
    g_renderer.geometry.glue.tombstones = 0;
    ARRLIST_Edge_clear(&(g_renderer.geometry.edges));
    for (size_t id = 0; id < g_renderer.geometry.triangles.size; id++) {
        Triangle t = g_renderer.geometry.triangles.data[id];
        VertexID vs[] = { t.a, t.b, t.c };
        for (size_t i = 0; i < 3; i++) {
            Edge e = { vs[i], vs[(i + 1) % 3] };
            Edge alternate = { e.b, e.a };
            Edge primed = HASHMAP_EdgeGlue_has(&(g_renderer.geometry.glue), e) ? e : alternate;
            if (!HASHMAP_EdgeGlue_has(&(g_renderer.geometry.glue), primed)) {
                HASHMAP_EdgeGlue_set(&(g_renderer.geometry.glue), primed, (EdgeMeta){ (TriangleID)id, (TriangleID)-1, 0.0f, { 0, 0, 0 } });
                ARRLIST_Edge_add(&(g_renderer.geometry.edges), primed);
            } else {
                EdgeMeta em = HASHMAP_EdgeGlue_get(&(g_renderer.geometry.glue), primed);
                em.b = (TriangleID)id;
                HASHMAP_EdgeGlue_set(&(g_renderer.geometry.glue), primed, em);
            }
        }
    }
}

static void RemoveLockRange(VertexID first, size_t count) {
    if (count == 0) return;
    HASHMAP_Locks old = g_renderer.geometry.locks;
    g_renderer.geometry.locks = (HASHMAP_Locks){ 0 };
    for (size_t i = 0; i < old.capacity; i++) {
        if (old.entries[i].used != 1) continue;
        VertexID v = old.entries[i].key;
        if (v >= first && v < first + count) continue;
        HASHMAP_Locks_set(&(g_renderer.geometry.locks), v >= first + count ? (VertexID)(v - count) : v, old.entries[i].value);
    }
    EZ_FREE(old.entries);
}

static void FreeMeshAnimation(MeshAnimation* ref) {
    MeshAnimation ma = { 0 };
    memcpy(&ma, ref, sizeof(MeshAnimation));
    for (size_t j = 0; j < ma.animations.size; j++) {
        ARRLIST_BoneChannel* channels = &(ma.animations.data[j].channels);
        for (size_t k = 0; k < channels->size; k++) {
            ARRLIST_Vec3Key_clear(&(channels->data[k].positions));
            ARRLIST_QuatKey_clear(&(channels->data[k].rotations));
            ARRLIST_Vec3Key_clear(&(channels->data[k].scales));
        }
        ARRLIST_BoneChannel_clear(channels);
        if (j < ma.names.size) EZ_FREE(ma.names.data[j]);
    }
    ARRLIST_Animation_clear(&(ma.animations));
    ARRLIST_DynamicString_clear(&(ma.names));
    memcpy(ref, &ma, sizeof(MeshAnimation));
}

static size_t RemoveFlaggedTriangles(const uint8_t* doomed) {
    size_t n = g_renderer.geometry.triangles.size;
    uint32_t* before = EZ_ALLOC(n + 1, sizeof(uint32_t));
    for (size_t i = 0; i < n; i++) before[i + 1] = before[i] + (doomed[i] ? 1 : 0);
    size_t removed = before[n];
    if (removed > 0) {
        for (size_t m = 0; m < g_renderer.geometry.meshes.size; m++) {
            MeshDescriptor* md = &(g_renderer.geometry.meshes.data[m]);
            BOOL empty = md->tend < md->tstart || md->tend == (TriangleID)-1;
            uint32_t tstart = md->tstart <= n ? md->tstart : (uint32_t)n;
            uint32_t newstart = md->tstart - before[tstart];
            uint32_t kept = empty ? 0 : (md->tend - md->tstart + 1) - (before[md->tend + 1] - before[md->tstart]);
            md->tstart = newstart;
            md->tend = newstart + kept - 1;
        }
        size_t w = 0;
        for (size_t r = 0; r < n; r++) {
            if (doomed[r]) continue;
            if (w != r) memcpy(&(g_renderer.geometry.triangles.data[w]), &(g_renderer.geometry.triangles.data[r]), sizeof(Triangle));
            w++;
        }
        g_renderer.geometry.triangles.size = w;
    }
    EZ_FREE(before);
    return removed;
}

Renderer* RawRendererRef() {
    return &g_renderer;
}

PipelineFlags GetPipelineFlags() {
    return RendererCamera()->config.flags;
}

void SetPipelineFlags(PipelineFlags flags) {
    RendererCamera()->config.flags = flags;
}

void SetViewportSlice(size_t w, size_t h) {
    const float fw = (float)w;
    const float fh = (float)h;
    const float diffw = g_renderer.dimensions.x / fw;
    const float diffh = g_renderer.dimensions.y / fh;
    const float psuedo_w = diffw < diffh ? g_renderer.dimensions.x : g_renderer.dimensions.y * (fw / fh);
    const float psuedo_h = diffh < diffw ? g_renderer.dimensions.y : g_renderer.dimensions.x * (fh / fw);
    g_renderer.viewport = (Vector2) { ceil(psuedo_w), ceil(psuedo_h) };
}

void OverrideResolution(size_t x, size_t y) {
	g_override_resolution = (Vector2){ x, y };
}

BOOL ResizeRenderer(size_t width, size_t height) {
    if (!g_renderer_init) {
        OverrideResolution(width, height);
        return TRUE;
    }
    if (width == 0 || height == 0) {
        logwarn("Cannot resize renderer to %zux%zu - dimensions must be non-zero", width, height);
        return FALSE;
    }
    if (width == (size_t)g_renderer.dimensions.x && height == (size_t)g_renderer.dimensions.y) return TRUE;
    if (TrainingScenePhaseNow() == TRAINSCENE_CAPTURING) {
        logwarn("Cannot resize renderer while training views are being captured");
        return FALSE;
    }
    VkPhysicalDeviceProperties props = { 0 };
    vkGetPhysicalDeviceProperties(g_renderer.vulkan.core.general.gpu, &props);
    if (width > props.limits.maxImageDimension2D || height > props.limits.maxImageDimension2D) {
        logwarn("Cannot resize renderer to %zux%zu - GPU supports at most %u pixels per side",
            width, height, props.limits.maxImageDimension2D);
        return FALSE;
    }
    VkDevice device = g_renderer.vulkan.core.general.interface;
    vkDeviceWaitIdle(device);
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_DestroyImage(g_renderer.vulkan.core.context.targets[i]);
        VUTIL_DestroyImage(g_renderer.vulkan.core.context.hdr[i]);
        VUTIL_DestroyBuffer(g_renderer.vulkan.core.context.renderdata.ssbos[i]);
        UnloadRenderTexture(g_renderer.swapchain.target[i]);
    }
    VCLEAN_DenoiseTargets(&(g_renderer.vulkan.core.context));
    VCLEAN_Bridge(g_renderer.vulkan.core.bridge);
    g_renderer.dimensions = (Vector2){ (float)width, (float)height };
    g_renderer.pixels = width * height;
    BOOL result =
        VINIT_TargetsHDR(g_renderer.vulkan.core.context.hdr) &&
        VINIT_Targets(g_renderer.vulkan.core.context.targets) &&
        VINIT_DenoiseTargets(&(g_renderer.vulkan.core.context)) &&
        VINIT_ShaderStorageBuffers(g_renderer.vulkan.core.context.renderdata.ssbos) &&
        VINIT_Bridge(g_renderer.vulkan.core.bridge);
    EZ_ASSERT(result, "Failed to rebuild renderer resources at %zux%zu", width, height);
    size_t bytes = width * height * 4;
    VkMappedMemoryRange flush[CPUSWAP_LENGTH] = { 0 };
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        memset(g_renderer.swapchain.reference[i], 0, bytes);
        flush[i].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        flush[i].memory = g_renderer.vulkan.core.bridge[i].memory;
        flush[i].offset = 0;
        flush[i].size = VK_WHOLE_SIZE;
    }
    vkFlushMappedMemoryRanges(device, CPUSWAP_LENGTH, flush);
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        g_renderer.swapchain.target[i] = LoadRenderTexture(width, height);
        EZ_ASSERT(IsRenderTextureValid(g_renderer.swapchain.target[i]), "Unable to load target texture");
        glBindTexture(GL_TEXTURE_2D, g_renderer.swapchain.target[i].texture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, g_renderer.swapchain.reference[i]);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    VUPDT_DescriptorSetsAll(g_renderer.vulkan.core.context.renderdata.descriptors);
    SetViewportSlice(width, height);
    RendererCamera()->config.reset = TRUE;
    loginfo("Renderer resolution set to %zux%zu", width, height);
    return TRUE;
}

void InitializeRenderer() {
    g_renderer_init = TRUE;

	// init rand
	srand(time(NULL));

    // init cholmod
    #ifndef NO_MESH_MANIPULATION_SUPPORT
    cholmod_start(&g_cholmod);
    #endif

    // default camera
    SubmitNamedCamera(DefaultCamera(), "Default");

    // initialize sim
    g_renderer.geometry.fluid.timestep = 0.016f;
    g_renderer.geometry.fluid.diffusion = 0.000001;
    g_renderer.geometry.fluid.dissipation = 0.005f;
    g_renderer.geometry.fluid.viscosity = 0.000001f;
    g_renderer.geometry.fluid.iterations = 20;
    g_renderer.geometry.fluid.style = 0;

    // initialize min/max BB
    SETVEC3(g_renderer.geometry.bounds.min, FLT_MAX, FLT_MAX, FLT_MAX);
    SETVEC3(g_renderer.geometry.bounds.max, -FLT_MAX, -FLT_MAX, -FLT_MAX);
    SETVEC3(g_renderer.geometry.splatbounds.min, FLT_MAX, FLT_MAX, FLT_MAX);
    SETVEC3(g_renderer.geometry.splatbounds.max, -FLT_MAX, -FLT_MAX, -FLT_MAX);

    // set up dimensions
    g_renderer.dimensions = (Vector2){
		g_override_resolution.x == 0 ? CONFGET(Float, "default_render_width", GetScreenWidth()) : g_override_resolution.x,
		g_override_resolution.y == 0 ? CONFGET(Float, "default_render_height", GetScreenHeight()) : g_override_resolution.y };
    g_renderer.pixels = (size_t)g_renderer.dimensions.x * (size_t)g_renderer.dimensions.y;

    // initialize vulkan resources
	VUTIL_SetVulkanUtilsContext(&g_renderer);
	VINIT_SetVulkanInitContext(&g_renderer);
	VUPDT_SetVulkanUpdateContext(&g_renderer);
	VCLEAN_SetVulkanCleanContext(&g_renderer);
	BOOL result = VINIT_Vulkan(&(g_renderer.vulkan));
	EZ_ASSERT(result, "Failed to initialize vulkan");

    // set up cpu swap
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
	    g_renderer.swapchain.target[i] = LoadRenderTexture(g_renderer.dimensions.x, g_renderer.dimensions.y);
	    EZ_ASSERT(IsRenderTextureValid(g_renderer.swapchain.target[i]), "Unable to load target texture");
    }

    // configure stat profiler
    ez_configure_profile(&(g_renderer.stats.profile), "Renderer", 10);

    // configure GPU stat cache
    g_renderer.stats.cache.update_interval = 1.0;
    PollGPUCache(TRUE);

    // default material
    SurfaceMaterial dsm = { 0 };
    #define DSMSETV3(name, default) {\
    const char* n1 = "default_material_" #name "r"; \
    dsm.name[0] = CONFGET(Float, n1, default); \
    const char* n2 = "default_material_" #name "g"; \
    dsm.name[1] = CONFGET(Float, n2, default); \
    const char* n3 = "default_material_" #name "b"; \
    dsm.name[2] = CONFGET(Float, n3, default);}
    #define DSMSETF(name, default) {\
    const char* n = "default_material_" #name; \
    dsm.name = CONFGET(Float, n, default);}
    DSMSETV3(emission, 0.0f);
    DSMSETV3(ambient, 1.0f);
    DSMSETV3(diffuse, 1.0f);
    DSMSETV3(specular, 0.0f);
    DSMSETV3(absorbtion, 0.0f);
    DSMSETV3(dispersion, 0.0f);
    DSMSETV3(scattering, 0.0f);
    DSMSETF(anisotropy, 0.0f);
    DSMSETF(scale, 1.0f);
    DSMSETF(subsurface, 0.0f);
    DSMSETF(ior, 0.0f);
    DSMSETF(shiny, 10.0f);
    dsm.model = CONFGET(Int, "default_material_model", 2);
    #undef DSMSETV3
    #undef DSMSETF
    SubmitNamedMaterial(dsm, "Default");

    // set overlay context
    SetOverlayContext(&g_renderer);
}

void DestroyRenderer() {
    // clean geometry
    ClearScene(TRUE);
    CleanManifoldMesh(&(g_renderer.geometry.manifold));
    CleanARAP();

    // destroy cholmod
    #ifndef NO_MESH_MANIPULATION_SUPPORT
    cholmod_finish(&g_cholmod);
    #endif

    // destroy vulkan resources
    VCLEAN_Vulkan(&(g_renderer.vulkan));

    // unload cpu swap textures
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++)
	    UnloadRenderTexture(g_renderer.swapchain.target[i]);

    // clean externals
    ARRLIST_ShaderLocation_clear(&(g_renderer.externals));
    for (size_t i = 0; i < g_renderer.ebuffers.size; i++)
        if (g_renderer.ebuffers.data[i].size != 0)
            EZ_FREE(g_renderer.ebuffers.data[i].data);
    ARRLIST_ShaderBuffer_clear(&(g_renderer.ebuffers));
}

SceneCamera* RendererCamera() {
    return &(g_renderer.geometry.cameras.data[g_renderer.geometry.primarycamera]);
}

SceneCamera* GetCamera(size_t cid) {
    EZ_ASSERT(cid < g_renderer.geometry.cameras.size, "Invalid camera ID detected");
    return &(g_renderer.geometry.cameras.data[cid]);
}

SceneCamera DefaultCamera() {
    SceneCamera sc = { 0 };
    sc.config.whitepoint = 20.0f;
    sc.config.gamma = 2.2f;
    sc.config.maxbounces = 10;
    sc.config.maxsssbounces = 32;
    sc.config.multiplier = 1;
    sc.config.direct = FALSE;
    sc.config.grid = TRUE;
    sc.config.async = TRUE;
    sc.config.showdof = FALSE;
    sc.config.directonly = FALSE;
    sc.config.scenelighting = TRUE;
    sc.config.scenelightingonly = FALSE;
    sc.config.scenelightshadows = TRUE;
    sc.config.spectral = FALSE;
    sc.config.screenspace = FALSE;
    sc.config.splatshadows = TRUE;
    sc.config.wireframe = TRUE;
    sc.config.normals = TRUE;
    sc.config.reset = FALSE;
    sc.config.denoise = FALSE;
    sc.config.denoiseiterations = 4;
    sc.config.denoisecolor = 4.0f;
    sc.config.denoisenormal = 64.0f;
    sc.config.denoisedepth = 3.0f;
    sc.config.debug = DEBUG_NONE;
    sc.config.flags = PREVIEW_PIPELINE_FLAGS;
    sc.config.arap.iterations = 10;
    sc.config.arap.style = 0;
    sc.config.arap.cube_lambda = 0.5f;
    sc.config.arap.cube_rho = 1e-4f;
    sc.config.arap.addm = 5;
    sc.core = (SimpleCamera){
        { 0.0f, 2.133f, 2.11f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        90.0f, 0.0f, 0.0f,
        FALSE, 4.0f
    };
    return sc;
}

void FitCamera() {
    vec3 min, max;
    if (RendererCamera()->config.flags & FLUID_SHADER_FLAG) {
        min[0] = -(float)(g_renderer.geometry.fluid.width) * 0.5f;
        min[1] = -(float)(g_renderer.geometry.fluid.height) * 0.5f;
        min[2] = -(float)(g_renderer.geometry.fluid.length) * 0.5f;
        max[0] = (float)(g_renderer.geometry.fluid.width) * 0.5f;
        max[1] = (float)(g_renderer.geometry.fluid.height) * 0.5f;
        max[2] = (float)(g_renderer.geometry.fluid.length) * 0.5f;
    } else {
        glm_vec3_copy(g_renderer.geometry.bounds.min, min);
        glm_vec3_copy(g_renderer.geometry.bounds.max, max);
    }
    if (min[0] >= max[0]) return;
    vec3 l2p;
    SimpleCamera camera = RendererCamera()->core;
    glm_vec3_sub(camera.position, camera.look, l2p);
    glm_vec3_normalize(l2p);
    vec3 extend, min2o, newo;
    glm_vec3_sub(max, min, extend);
    glm_vec3_scale(extend, 0.5f, min2o);
    glm_vec3_add(min2o, min, newo);
    float width = glm_vec3_norm(extend);
    glm_vec3_copy(newo, camera.look);
    glm_vec3_scale(l2p, width, l2p);
    glm_vec3_add(l2p, newo, camera.position);
    RendererCamera()->core = camera;
}

void ReorientCamera() {
    for (int i = 0; i < 3; i++) {
        float sign = RendererCamera()->core.up[i] > 0 ? 1.0f : (RendererCamera()->core.up[i] < 0 ? -1.0f : 0.0f);
        RendererCamera()->core.look[i] += sign*1e-6f;
    }
    RendererCamera()->core.up[0] = 0;
    RendererCamera()->core.up[1] = 1;
    RendererCamera()->core.up[2] = 0;
}

void GetVertex(size_t index, vec3 out) {
    EZ_ASSERT(index < g_renderer.geometry.vertices.size, "Vertex does not exist for requested index");
    glm_vec3_copy(g_renderer.geometry.vertices.data[index], out);
}

float* VertexReference(VertexID vertex) {
    EZ_ASSERT(vertex < g_renderer.geometry.vertices.size, "Vertex reference does not exist for requested index");
    return g_renderer.geometry.vertices.data[vertex];
}

void LockVertex(VertexID vertex) {
    EZ_ASSERT(vertex < g_renderer.geometry.vertices.size, "Vertex does not exist for requested index");
    HASHMAP_Locks_set(&(g_renderer.geometry.locks), vertex, TRUE);
    g_renderer.geometry.vertices.data[vertex][3] = 1.0f;
    UpdateVertices();
    SavePose();
}

void UnlockVertex(VertexID vertex) {
    EZ_ASSERT(vertex < g_renderer.geometry.vertices.size, "Vertex does not exist for requested index");
    if (HASHMAP_Locks_has(&(g_renderer.geometry.locks), vertex)) {
        HASHMAP_Locks_set(&(g_renderer.geometry.locks), vertex, FALSE);
        g_renderer.geometry.vertices.data[vertex][3] = 0.0f;
        UpdateVertices();
        SavePose();
    }
}

BOOL VertexLocked(VertexID vertex) {
    EZ_ASSERT(vertex < g_renderer.geometry.vertices.size, "Vertex does not exist for requested index");
    if (HASHMAP_Locks_has(&(g_renderer.geometry.locks), vertex)) 
        return HASHMAP_Locks_get(&(g_renderer.geometry.locks), vertex);
    return FALSE;
}

void SubmitVertex(vec3 vertex) {
    vec4 v = { 0 };
    glm_vec3_copy(vertex, v);
    ARRLIST_vec4_add(&(g_renderer.geometry.vertices), v);
    glm_vec3_minv(g_renderer.geometry.bounds.min, vertex, g_renderer.geometry.bounds.min);
    glm_vec3_maxv(g_renderer.geometry.bounds.max, vertex, g_renderer.geometry.bounds.max);
    UpdateVertices();
}

void ClearVertices() {
    if (g_renderer.geometry.vertices.maxsize == 0) return;
    ARRLIST_vec4_clear(&(g_renderer.geometry.vertices));
    HASHMAP_Locks_clear(&(g_renderer.geometry.locks));
    ARRLIST_Edge_clear(&(g_renderer.geometry.edges));
    SETVEC3(g_renderer.geometry.bounds.min, FLT_MAX, FLT_MAX, FLT_MAX);
    SETVEC3(g_renderer.geometry.bounds.max, -FLT_MAX, -FLT_MAX, -FLT_MAX);
    UpdateVertices();
}

void SubmitNormal(vec3 normal) {
    vec4 n = { 0 };
    glm_vec3_copy(normal, n);
    ARRLIST_vec4_add(&(g_renderer.geometry.normals), n);
    UpdateNormals();
}

void ClearNormals() {
    if (g_renderer.geometry.normals.maxsize == 0) return;
    ARRLIST_vec4_clear(&(g_renderer.geometry.normals));
    UpdateNormals();
}

TriangleID SubmitTriangle(Triangle triangle) {
    EZ_ASSERT(triangle.a < g_renderer.geometry.vertices.size &&
              triangle.b < g_renderer.geometry.vertices.size &&
              triangle.c < g_renderer.geometry.vertices.size, "Triangle vertex does not exist");
    vec3 emission; 
    glm_vec3_copy(g_renderer.geometry.materials.data[triangle.material].emission, emission);
    if (emission[0] != 0 || emission[1] != 0 || emission[2] != 0) {
        ARRLIST_TriangleID_add(&(g_renderer.geometry.emissives), g_renderer.geometry.triangles.size);
        UpdateEmissives();
    }
    TriangleID id = g_renderer.geometry.triangles.size;
    ARRLIST_Triangle_add(&(g_renderer.geometry.triangles), triangle);
    VertexID vs[] = { triangle.a, triangle.b, triangle.c };
    for (size_t i = 0; i < 3; i++) {
        VertexID a = vs[i];
        VertexID b = vs[(i + 1)%3];
        Edge e = { a, b };
        Edge alternate = { b, a };
        Edge primed = HASHMAP_EdgeGlue_has(&(g_renderer.geometry.glue), e) ? e : alternate;
        if (!HASHMAP_EdgeGlue_has(&(g_renderer.geometry.glue), primed)) {
            HASHMAP_EdgeGlue_set(&(g_renderer.geometry.glue), primed, (EdgeMeta){ id, (TriangleID)-1, 0.0f, {0, 0, 0} });
            ARRLIST_Edge_add(&(g_renderer.geometry.edges), primed);
        } else {
            EdgeMeta em = HASHMAP_EdgeGlue_get(&(g_renderer.geometry.glue), primed);
            em.b = id;
            HASHMAP_EdgeGlue_set(&(g_renderer.geometry.glue), primed, em);
        }
    }
    UpdateTriangles();
    return id;
}

void ClearTriangles() {
    if (g_renderer.geometry.triangles.maxsize == 0) return;
    ARRLIST_Triangle_clear(&(g_renderer.geometry.triangles));
    ARRLIST_TriangleID_clear(&(g_renderer.geometry.emissives));
    ARRLIST_EmissionAlias_clear(&(g_renderer.geometry.aliases));
    HASHMAP_EdgeGlue_clear(&(g_renderer.geometry.glue));
    ARRLIST_Edge_clear(&(g_renderer.geometry.edges));
    UpdateTriangles();
}

LightID SubmitLight(SceneLight light) {
    char buf[MAX_LIGHT_NAME_SIZE] = { 0 };
    sprintf(buf, "Light #%d", (int)g_renderer.geometry.lights.size);
    return SubmitNamedLight(light, buf);
}

LightID SubmitNamedLight(SceneLight light, const char* name) {
    if (light.direction[0] != 0 || light.direction[1] != 0 || light.direction[2] != 0)
        glm_vec3_normalize(light.direction);
    ARRLIST_SceneLight_add(&(g_renderer.geometry.lights), light);
    char* b = EZ_ALLOC(MAX_LIGHT_NAME_SIZE + 1, sizeof(char));
    strncpy(b, name, MAX_LIGHT_NAME_SIZE);
    ARRLIST_DynamicString_add(&(g_renderer.geometry.lightnames), b);
    UpdateLights();
    return g_renderer.geometry.lights.size - 1;
}

char* LightName(LightID lid) {
    EZ_ASSERT(lid < g_renderer.geometry.lights.size, "Invalid light ID detected");
    return g_renderer.geometry.lightnames.data[lid];
}

char** LightNameReference(LightID lid) { 
    return &(g_renderer.geometry.lightnames.data[lid]);
}

void ClearLights() {
    ARRLIST_SceneLight_clear(&(g_renderer.geometry.lights));
    for (size_t i = 0; i < g_renderer.geometry.lightnames.size; i++)
        EZ_FREE(g_renderer.geometry.lightnames.data[i]);
    ARRLIST_DynamicString_clear(&(g_renderer.geometry.lightnames));
    UpdateLights();
}

MaterialID SubmitMaterial(SurfaceMaterial material) {
    char buf[MAX_MATERIAL_NAME_SIZE] = { 0 };
    sprintf(buf, "Material #%d", (int)g_renderer.geometry.materials.size);
    return SubmitNamedMaterial(material, buf);
}

MaterialID SubmitNamedMaterial(SurfaceMaterial material, const char* name) {
    ARRLIST_SurfaceMaterial_add(&(g_renderer.geometry.materials), material);
    char* b = EZ_ALLOC(MAX_MATERIAL_NAME_SIZE + 1, sizeof(char));
    strncpy(b, name, MAX_MATERIAL_NAME_SIZE);
    ARRLIST_DynamicString_add(&(g_renderer.geometry.materialnames), b);
    UpdateMaterials();
    if (material.emission[0] != 0 || material.emission[1] != 0 || material.emission[2] != 0) UpdateEmissives();
    return g_renderer.geometry.materials.size - 1;
}

char* MaterialName(MaterialID mid) {
    EZ_ASSERT(mid < g_renderer.geometry.materials.size, "Invalid material ID detected");
    return g_renderer.geometry.materialnames.data[mid];
}

char** MaterialNameReference(MaterialID mid) {
    return &(g_renderer.geometry.materialnames.data[mid]);
}

void ClearMaterials() {
    if (g_renderer.geometry.materials.maxsize == 0) return;
    ARRLIST_SurfaceMaterial_clear(&(g_renderer.geometry.materials));
    for (size_t i = 0; i < g_renderer.geometry.materialnames.size; i++)
        EZ_FREE(g_renderer.geometry.materialnames.data[i]);
    ARRLIST_DynamicString_clear(&(g_renderer.geometry.materialnames));
    UpdateMaterials();
}

size_t SubmitCamera(SceneCamera camera) {
    char buf[MAX_CAMERA_NAME_SIZE] = { 0 };
    sprintf(buf, "Camera #%d", (int)g_renderer.geometry.cameras.size);
    return SubmitNamedCamera(camera, buf);
}

size_t SubmitNamedCamera(SceneCamera camera, const char* name) {
    ARRLIST_SceneCamera_add(&(g_renderer.geometry.cameras), camera);
    char* b = EZ_ALLOC(MAX_CAMERA_NAME_SIZE + 1, sizeof(char));
    strncpy(b, name, MAX_CAMERA_NAME_SIZE);
    ARRLIST_DynamicString_add(&(g_renderer.geometry.cameranames), b);
    return g_renderer.geometry.cameras.size - 1;
}

void SetCamera(size_t cid) {
    EZ_ASSERT(cid < g_renderer.geometry.cameras.size, "Invalid camera ID detected");
    g_renderer.geometry.primarycamera = cid;
}

size_t PrimaryCameraID() {
    return g_renderer.geometry.primarycamera;
}

char* CameraName(size_t cid) {
    EZ_ASSERT(cid < g_renderer.geometry.cameras.size, "Invalid camera ID detected");
    return g_renderer.geometry.cameranames.data[cid];
}

char** CameraNameReference(size_t cid) {
    return &(g_renderer.geometry.cameranames.data[cid]);
}

void ClearCameras() {
    ARRLIST_SceneCamera_clear(&(g_renderer.geometry.cameras));
    for (size_t i = 0; i < g_renderer.geometry.cameranames.size; i++)
        EZ_FREE(g_renderer.geometry.cameranames.data[i]);
    ARRLIST_DynamicString_clear(&(g_renderer.geometry.cameranames));
}

void Render() {
    static BOOL async_update = TRUE;
    static size_t dupdate_queue = 0;
	size_t new_ind = (g_renderer.swapchain.index + 1) % CPUSWAP_LENGTH;
    BOOL resized_buffers = FALSE;
    BOOL is_transferring = FALSE;
    BOOL is_pathtraciing = RendererCamera()->config.flags & PATHTRACE_SHADER_FLAG;

    // update calculated sizes
    g_renderer.geometry.maxprimitives = g_renderer.geometry.triangles.size + g_renderer.geometry.splats.size;
    g_renderer.geometry.mostprimitives = g_renderer.geometry.triangles.size > g_renderer.geometry.splats.size ? g_renderer.geometry.triangles.size : g_renderer.geometry.splats.size;

    // update external buffers
    for (size_t i = 0; i < g_renderer.ebuffers.size; i++) {
        if (g_renderer.ebuffers.data[i].update) {
            g_renderer.ebuffers.data[i].update = FALSE;
            VUPDT_ExternalBuffer(&(g_renderer.ebuffers.data[i]));
        }
    }

    // animations
    g_renderer.geometry.changes.update_poses |= PlayAnimations(&(g_renderer.geometry));

    // emissive alias refresh
    BOOL updated_emissives = FALSE;
    if (g_renderer.geometry.changes.refresh_emissives && is_pathtraciing) {
        g_renderer.geometry.changes.refresh_emissives = FALSE;
        ARRLIST_TriangleID_wipe(&(g_renderer.geometry.emissives));
        for (size_t i = 0; i < g_renderer.geometry.triangles.size; i++)
            if (MaterialReference(TriangleReference(i)->material)->emission[0] != 0.0f ||
                MaterialReference(TriangleReference(i)->material)->emission[1] != 0.0f ||
                MaterialReference(TriangleReference(i)->material)->emission[2] != 0.0f)
                ARRLIST_TriangleID_add(&(g_renderer.geometry.emissives), i);
        UpdateEmissives();
    }
    if (g_renderer.geometry.changes.update_emissives && is_pathtraciing) {
        g_renderer.geometry.changes.update_emissives = FALSE;
        updated_emissives = TRUE;
        ARRLIST_EmissionAlias_wipe(&(g_renderer.geometry.aliases));
        float total = 0.0f;
        size_t ns = 0;
        size_t nl = 0;
        float* areas = EZ_ALLOC(g_renderer.geometry.emissives.size, sizeof(float));
        float* q = EZ_ALLOC(g_renderer.geometry.emissives.size, sizeof(float));
        size_t* small = EZ_ALLOC(g_renderer.geometry.emissives.size, sizeof(size_t));
        size_t* large = EZ_ALLOC(g_renderer.geometry.emissives.size, sizeof(size_t));
        for (size_t i = 0; i < g_renderer.geometry.emissives.size; i++) {
            Triangle* triangle = TriangleReference(g_renderer.geometry.emissives.data[i]);
            mat4 transform = { 0 };
            if (!FindTriangleTransform(g_renderer.geometry.emissives.data[i], transform)) continue;
            float area = Luminance(g_renderer.geometry.materials.data[triangle->material].emission) * TriangleTransformedArea(VertexReference(triangle->a), VertexReference(triangle->b), VertexReference(triangle->c), transform);
            total += area;
            areas[i] = area;
        }
        for (size_t i = 0; i < g_renderer.geometry.emissives.size; i++) {
            EmissionAlias alias = { 0 };
            alias.pmf = areas[i] / total;
            q[i] = alias.pmf * (float)(g_renderer.geometry.emissives.size);
            if (q[i] < 1.0f) small[ns++] = i;
            else large[nl++] = i;
            ARRLIST_EmissionAlias_add(&(g_renderer.geometry.aliases), alias);
        }
        while (ns && nl) {
            size_t s = small[--ns];
            size_t l = large[nl - 1];
            g_renderer.geometry.aliases.data[s].probability = q[s];
            g_renderer.geometry.aliases.data[s].alias = (uint32_t)l;
            q[l] -= 1.0f - q[s];
            if (q[l] < 1.0f) {
                nl--;
                small[ns++] = l;
            }
        }
        while (nl) {
            size_t l = large[--nl];
            g_renderer.geometry.aliases.data[l].probability = 1.0f;
            g_renderer.geometry.aliases.data[l].alias = (uint32_t)l;
        }
        while (ns) {
            size_t s = small[--ns];
            g_renderer.geometry.aliases.data[s].probability = 1.0f;
            g_renderer.geometry.aliases.data[s].alias = (uint32_t)s;
        }
        EZ_FREE(areas);
        EZ_FREE(q);
        EZ_FREE(small);
        EZ_FREE(large);
    }
    BOOL upload_emissives = updated_emissives;

    // helpers for readability
    #define TRANSFER_UPDATE(cname, sname, gname, gsname, oname, vname, tri) { \
        if (g_renderer.geometry.changes.update_##cname) { \
            g_renderer.geometry.changes.update_##cname = FALSE; \
            if (g_renderer.geometry.changes.max_##sname != gname) { \
                g_renderer.geometry.changes.max_##sname = gname; \
                if (!resized_buffers) { \
                    resized_buffers = TRUE; \
                    if (is_transferring) VUTIL_EndTransferCommands(); \
                    is_transferring = TRUE; \
                    VUTIL_BeginTransferCommands(); \
                    vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface); \
                } \
                VCLEAN_##vname(&(g_renderer.vulkan.core.geometry.oname)); \
                VINIT_##vname(&(g_renderer.vulkan.core.geometry.oname)); \
                if (tri) { \
                    VCLEAN_BVH(&(g_renderer.vulkan.core.geometry.bvh)); \
                    VINIT_BVH(&(g_renderer.vulkan.core.geometry.bvh)); \
                } \
            } else { \
                if (g_renderer.geometry.changes.num_##sname != gsname) { \
                    dupdate_queue = CPUSWAP_LENGTH; \
                    g_renderer.geometry.changes.num_##sname = gsname; \
                } \
                if (!is_transferring) { is_transferring = TRUE; VUTIL_BeginTransferCommands(); } \
                VUPDT_##vname(&(g_renderer.vulkan.core.geometry.oname)); \
            } \
            if (tri) upload_emissives = TRUE; \
        } \
    }

    // update render frame time;
    g_rft += GetFrameTime() / ((float)(RendererCamera()->config.multiplier));

    // ARAP pose saving
    if (g_renderer.geometry.changes.update_triangles) SavePose();

    // detect changes in described data
    if (async_update) {
        // profile for stats
        ez_begin_profile(&(g_renderer.stats.profile));

        // recompute min/max
        if (g_renderer.geometry.changes.update_meshes || g_renderer.geometry.changes.update_vertices || g_renderer.geometry.changes.update_splats) {
            SETVEC3(g_renderer.geometry.bounds.min, FLT_MAX, FLT_MAX, FLT_MAX);
            SETVEC3(g_renderer.geometry.bounds.max, -FLT_MAX, -FLT_MAX, -FLT_MAX);
            for (size_t i = 0; i < g_renderer.geometry.meshes.size; i++) {
                MeshDescriptor* md = MeshReference(i);
                if (md->disabled) continue;
                vec3 center, extents, worldcenter, worldextents, worldmin, worldmax;
                mat4 transform;
                mat3 m3;
                memcpy(transform, md->transform, sizeof(mat4));
                memcpy(center, md->center, sizeof(vec3));
                memcpy(extents, md->extents, sizeof(vec3));
                glm_mat4_mulv3(transform, center, 1.0f, worldcenter);
                glm_mat4_pick3(transform, m3);
                for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) m3[r][c] = fabsf(m3[r][c]);
                worldextents[0] =
                    m3[0][0] * extents[0] +
                    m3[0][1] * extents[1] +
                    m3[0][2] * extents[2];
                worldextents[1] =
                    m3[1][0] * extents[0] +
                    m3[1][1] * extents[1] +
                    m3[1][2] * extents[2];
                worldextents[2] =
                    m3[2][0] * extents[0] +
                    m3[2][1] * extents[1] +
                    m3[2][2] * extents[2];
                glm_vec3_sub(worldcenter, worldextents, worldmin);
                glm_vec3_add(worldcenter, worldextents, worldmax);
                glm_vec3_minv(worldmin, g_renderer.geometry.bounds.min, g_renderer.geometry.bounds.min);
                glm_vec3_maxv(worldmax, g_renderer.geometry.bounds.max, g_renderer.geometry.bounds.max);
            }
            glm_vec3_minv(g_renderer.geometry.splatbounds.min, g_renderer.geometry.bounds.min, g_renderer.geometry.bounds.min);
            glm_vec3_maxv(g_renderer.geometry.splatbounds.max, g_renderer.geometry.bounds.max, g_renderer.geometry.bounds.max);
        }

        // set bvh reconstruction
        if (g_renderer.geometry.changes.update_vertices ||
            g_renderer.geometry.changes.update_triangles ||
            g_renderer.geometry.changes.update_meshes ||
            g_renderer.geometry.changes.update_poses ||
            g_renderer.geometry.changes.update_splats)
            g_renderer.geometry.changes.update_bvh = CPUSWAP_LENGTH;
        if (RendererCamera()->config.screenspace)
            g_renderer.geometry.changes.update_bvh = CPUSWAP_LENGTH * 2; // double, so if we turn off itll update bvh back to original

        // transfer updates
        TRANSFER_UPDATE(normals, normals, g_renderer.geometry.normals.maxsize, g_renderer.geometry.normals.size, normals, Normals, FALSE);
        TRANSFER_UPDATE(vertices, vertices, g_renderer.geometry.vertices.maxsize, g_renderer.geometry.vertices.size, vertices, Vertices, FALSE);
        TRANSFER_UPDATE(triangles, triangles, g_renderer.geometry.triangles.maxsize, g_renderer.geometry.triangles.size, triangles, Triangles, TRUE);
        TRANSFER_UPDATE(splats, splats, g_renderer.geometry.splats.maxsize, g_renderer.geometry.splats.size, splats, Splats, TRUE);
        TRANSFER_UPDATE(materials, materials, g_renderer.geometry.materials.maxsize, g_renderer.geometry.materials.size, materials, Materials, FALSE);
        TRANSFER_UPDATE(lights, lights, g_renderer.geometry.lights.maxsize, g_renderer.geometry.lights.size, lights, Lights, FALSE);
        TRANSFER_UPDATE(simulation, sim_size, SimSize(g_renderer.geometry.fluid), SimSize(g_renderer.geometry.fluid), fluid, Simulation, FALSE);
        TRANSFER_UPDATE(meshes, meshes, g_renderer.geometry.meshes.maxsize, g_renderer.geometry.meshes.size, transforms, Transforms, FALSE);
        TRANSFER_UPDATE(skins, skins, g_renderer.geometry.skins.maxsize, g_renderer.geometry.skins.size, skins, Skins, FALSE);
        TRANSFER_UPDATE(poses, poses, g_renderer.geometry.poses.maxsize, g_renderer.geometry.poses.size, poses, Poses, FALSE);

        // upload emissives if needed
        if (upload_emissives) {
            if (g_renderer.geometry.changes.max_emissives != g_renderer.geometry.emissives.maxsize ||
                g_renderer.geometry.changes.max_aliases != g_renderer.geometry.aliases.maxsize) {
                g_renderer.geometry.changes.max_emissives = g_renderer.geometry.emissives.maxsize;
                g_renderer.geometry.changes.max_aliases = g_renderer.geometry.aliases.maxsize;
                if (!resized_buffers) {
                    resized_buffers = TRUE;
                    if (is_transferring) VUTIL_EndTransferCommands();
                    is_transferring = TRUE;
                    VUTIL_BeginTransferCommands();
                    vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);
                }
                VCLEAN_Emissives(&(g_renderer.vulkan.core.geometry.emissives));
                VINIT_Emissives(&(g_renderer.vulkan.core.geometry.emissives));
                VCLEAN_Aliases(&(g_renderer.vulkan.core.geometry.aliases));
                VINIT_Aliases(&(g_renderer.vulkan.core.geometry.aliases));
            } else {
                if (g_renderer.geometry.changes.num_emissives != g_renderer.geometry.emissives.size) {
                    dupdate_queue = CPUSWAP_LENGTH;
                    g_renderer.geometry.changes.num_emissives = g_renderer.geometry.emissives.size;
                }
                if (!is_transferring) { is_transferring = TRUE; VUTIL_BeginTransferCommands(); }
                VUPDT_Emissives(&(g_renderer.vulkan.core.geometry.emissives));
                VUPDT_Aliases(&(g_renderer.vulkan.core.geometry.aliases));
            }
        }

        // dispatch transfer commands
        if (is_transferring) VUTIL_EndTransferCommands();

        // update all descriptor sets if needed
        if (resized_buffers) {
            dupdate_queue = 0;
            VUPDT_DescriptorSetsAll(g_renderer.vulkan.core.context.renderdata.descriptors);
        }

        // update only required descriptors if needed
        if (dupdate_queue > 0) {
            size_t oldind = g_renderer.swapchain.index;
            g_renderer.swapchain.index = new_ind;
            VUPDT_DescriptorSets(g_renderer.vulkan.core.context.renderdata.descriptors);
            g_renderer.swapchain.index = oldind;
            dupdate_queue--;
        }

        // update uniform buffers
        VUPDT_UniformBuffers(&(g_renderer.vulkan.core.context.renderdata.ubos));

        // reset renderer frame time
        g_rft = 0.0f;

        // reset command buffer and record it
        vkResetCommandBuffer(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index], 0);
        VUPDT_RecordCommand(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index]);

        // submit command buffer
        uint64_t waitVal = g_renderer.vulkan.core.transfer.signal;
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        g_renderer.vulkan.core.scheduler.signal++;
        uint64_t graphicsSignalVal = g_renderer.vulkan.core.scheduler.signal;
        VkTimelineSemaphoreSubmitInfo tsi = { 0 };
        tsi.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        tsi.waitSemaphoreValueCount = 1;
        tsi.pWaitSemaphoreValues = &waitVal;
        tsi.signalSemaphoreValueCount = 1;
        tsi.pSignalSemaphoreValues = &graphicsSignalVal;
        VkSubmitInfo submitInfo = { 0 };
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = &tsi;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &g_renderer.vulkan.core.transfer.semaphore;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index]);
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &g_renderer.vulkan.core.scheduler.semaphore; 
        VkResult result = vkQueueSubmit(g_renderer.vulkan.core.scheduler.queue, 1, &submitInfo, g_renderer.vulkan.core.scheduler.syncro.fences[g_renderer.swapchain.index]);
        EZ_ASSERT(result == VK_SUCCESS, "failed to submit draw command buffer!");
    }

    // wait for and reset rendering fence
    if (!RendererCamera()->config.async || RendererCamera()->config.multiplier > 1)
        vkWaitForFences(g_renderer.vulkan.core.general.interface, 1, &(g_renderer.vulkan.core.scheduler.syncro.fences[new_ind]), VK_TRUE, UINT64_MAX);
    if (vkGetFenceStatus(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.scheduler.syncro.fences[new_ind]) == VK_SUCCESS) {
        VkMappedMemoryRange invalidate[2] = { { 0 }, { 0 } };
        invalidate[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        invalidate[0].memory = g_renderer.vulkan.core.bridge[new_ind].memory;
        invalidate[0].offset = 0;
        invalidate[0].size = VK_WHOLE_SIZE;
        invalidate[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        invalidate[1].memory = g_renderer.vulkan.core.context.renderdata.overlay_bridge[new_ind].memory;
        invalidate[1].offset = 0;
        invalidate[1].size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(g_renderer.vulkan.core.general.interface, 2, invalidate);

        // copy overlay results to host (from the buffer this frame's copy wrote into)
        memcpy((void*)ExposedOverlaySSBO(), g_renderer.vulkan.core.context.renderdata.overlay_mapped[new_ind], sizeof(OverlaySSBO));

        // reset fences and update swapchain index
        vkResetFences(g_renderer.vulkan.core.general.interface, 1, &(g_renderer.vulkan.core.scheduler.syncro.fences[new_ind]));
        g_renderer.swapchain.index = new_ind;
        async_update = TRUE;

        // update render target
        glBindTexture(GL_TEXTURE_2D, g_renderer.swapchain.target[new_ind].texture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_renderer.dimensions.x, g_renderer.dimensions.y, GL_RGBA, GL_UNSIGNED_BYTE, g_renderer.swapchain.reference[new_ind]);
        glBindTexture(GL_TEXTURE_2D, 0);

        // end profiling
        ez_end_profile(&(g_renderer.stats.profile));
    } else {
        async_update = FALSE;
    }

    #undef TRANSFER_UPDATE
}

static void DrawHelper(float x, float y, float w, float h, float maxw, float maxh) {
    ClearBackground(BLACK);
    BeginBlendMode(BLEND_ADDITIVE);
    float diffw = g_renderer.dimensions.x / w;
    float diffh = g_renderer.dimensions.y / h;
    float psuedo_w = diffw < diffh ? g_renderer.dimensions.x : g_renderer.dimensions.y * (w / h);
    float psuedo_h = diffh < diffw ? g_renderer.dimensions.y : g_renderer.dimensions.x * (h / w);
    if (RendererCamera()->config.flags & PATHTRACE_SHADER_FLAG) {
        for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
            DrawTexturePro(
                g_renderer.swapchain.target[i].texture,
                (Rectangle){
                    (g_renderer.swapchain.target[i].texture.width / 2.0f) - (psuedo_w/2.0f),
                    (g_renderer.swapchain.target[i].texture.height / 2.0f) - (psuedo_h/2.0f),
                    psuedo_w,
                    psuedo_h },
                (Rectangle){ x, y, w, h },
                (Vector2){ 0, 0 },
                0.0f,
                WHITE);
        }
    } else {
        DrawTexturePro(
            g_renderer.swapchain.target[g_renderer.swapchain.index].texture,
            (Rectangle){
                (g_renderer.swapchain.target[g_renderer.swapchain.index].texture.width / 2.0f) - (psuedo_w/2.0f),
                (g_renderer.swapchain.target[g_renderer.swapchain.index].texture.height / 2.0f) - (psuedo_h/2.0f),
                psuedo_w,
                psuedo_h },
            (Rectangle){ x, y, w, h},
            (Vector2){ 0, 0 },
            0.0f,
            WHITE);
    }
    EndBlendMode();
}

void Draw(float x, float y, float w, float h) {
    DrawHelper(x, y, w, h, (float)GetScreenWidth(), (float)GetScreenHeight());
}

float RenderTime() {
    return ez_profile_result(&(g_renderer.stats.profile));
}

size_t NumNormals() {
    return g_renderer.geometry.normals.size;
}

size_t NumVertices() {
    return g_renderer.geometry.vertices.size;
}

size_t NumTriangles() {
    return g_renderer.geometry.triangles.size;
}

size_t NumMaterials() {
    return g_renderer.geometry.materials.size;
}

size_t NumCameras() {
    return g_renderer.geometry.cameras.size;
}

size_t NumEmissives() {
    return g_renderer.geometry.emissives.size;
}

void UpdateNormals() {
    g_renderer.geometry.changes.update_normals = TRUE;
}

void UpdateVertices() {
    g_renderer.geometry.changes.update_vertices = TRUE;
    UpdateEmissives();
}

SurfaceMaterial* MaterialReference(size_t index) {
    EZ_ASSERT(index < g_renderer.geometry.materials.size, "Invalid material index requested");
    return &(g_renderer.geometry.materials.data[index]);
}

void UpdateMaterials() {
    g_renderer.geometry.changes.update_materials = TRUE;
}

size_t NumLights() {
    return g_renderer.geometry.lights.size;
}

SceneLight* LightReference(size_t index) {
    EZ_ASSERT(index < g_renderer.geometry.lights.size, "Invalid light index requested");
    return &(g_renderer.geometry.lights.data[index]);
}

void UpdateLights() {
    g_renderer.geometry.changes.update_lights = TRUE;
}

Vector2 RenderResolution() {
    return g_renderer.dimensions;
}

RendererConfig* RenderConfig() {
    return &(RendererCamera()->config);
}

Geometry* RendererGeometry() {
    return &(g_renderer.geometry);
}

float RenderFrameTime() {
    return g_rft;
}

Triangle* TriangleReference(size_t index) {
    EZ_ASSERT(index < g_renderer.geometry.triangles.size, "Invalid triangle index requested");
    return &(g_renderer.geometry.triangles.data[index]);
}

void UpdateTriangles() {
    g_renderer.geometry.changes.update_triangles = TRUE;
}

BOOL Subdivide() {
    CleanManifoldMesh(&(g_renderer.geometry.manifold));
    g_renderer.geometry.manifold = GenerateManifoldMesh(
        g_renderer.geometry.vertices,
        g_renderer.geometry.normals,
        g_renderer.geometry.triangles);
    if (!IsManifoldValid(&(g_renderer.geometry.manifold))) {
        logerror("Mesh was not detected to be a valid manifold");
        CleanManifoldMesh(&(g_renderer.geometry.manifold));
        return FALSE;
    } else {
        SerialSubdivide(&(g_renderer.geometry.manifold));
        ReformatFromManifold(&(g_renderer.geometry));
        CleanManifoldMesh(&(g_renderer.geometry.manifold));
        return TRUE;
    }
}

BOOL Simplify(size_t faces) {
    size_t iterations = ceil(((float)faces)/2.0f);
    g_renderer.geometry.manifold = GenerateManifoldMesh(
        g_renderer.geometry.vertices,
        g_renderer.geometry.normals,
        g_renderer.geometry.triangles);
    if (!IsManifoldValid(&(g_renderer.geometry.manifold))) {
        logerror("Mesh was not detected to be a valid manifold");
        CleanManifoldMesh(&(g_renderer.geometry.manifold));
        return FALSE;
    } else {
        SerialSimplify(&(g_renderer.geometry.manifold), iterations);
        ReformatFromManifold(&(g_renderer.geometry));
        CleanManifoldMesh(&(g_renderer.geometry.manifold));
        return TRUE;
    }
}

void Displace(float displacement) {
    vec3* normals = EZ_ALLOC(g_renderer.geometry.vertices.size, sizeof(vec3));
    for (size_t i = 0; i < g_renderer.geometry.triangles.size; i++) {
        vec3 e1, e2, normal;
        uint32_t av = g_renderer.geometry.triangles.data[i].a;
        uint32_t bv = g_renderer.geometry.triangles.data[i].b;
        uint32_t cv = g_renderer.geometry.triangles.data[i].c;
        glm_vec3_sub(g_renderer.geometry.vertices.data[bv], g_renderer.geometry.vertices.data[av], e1);
        glm_vec3_sub(g_renderer.geometry.vertices.data[cv], g_renderer.geometry.vertices.data[av], e2);
        glm_vec3_cross(e1, e2, normal);
        glm_vec3_normalize(normal);
        glm_vec3_add(normal, normals[av], normals[av]);
        glm_vec3_add(normal, normals[bv], normals[bv]);
        glm_vec3_add(normal, normals[cv], normals[cv]);
    }
    for (size_t i = 0; i < g_renderer.geometry.vertices.size; i++) {
        glm_vec3_normalize(normals[i]);
        float dval = ((((float)rand()) / ((float)RAND_MAX)) * displacement) - (displacement/2.0f);
        glm_vec3_scale(normals[i], dval, normals[i]);
        glm_vec3_add(g_renderer.geometry.vertices.data[i], normals[i], g_renderer.geometry.vertices.data[i]);
    }
    EZ_FREE(normals);
    UpdateVertices();
}

BOOL Smoothen(float smoothening) {
    CleanManifoldMesh(&(g_renderer.geometry.manifold));
    g_renderer.geometry.manifold = GenerateManifoldMesh(
        g_renderer.geometry.vertices,
        g_renderer.geometry.normals,
        g_renderer.geometry.triangles);
    if (!IsManifoldValid(&(g_renderer.geometry.manifold))) {
        logerror("Mesh was not detected to be a valid manifold");
        CleanManifoldMesh(&(g_renderer.geometry.manifold));
        return FALSE;
    } else {
        SerialFilter(&(g_renderer.geometry.manifold), smoothening);
        ReformatFromManifold(&(g_renderer.geometry));
        CleanManifoldMesh(&(g_renderer.geometry.manifold));
        return TRUE;
    }
}

BOOL Remesh(float nudge) {
    CleanManifoldMesh(&(g_renderer.geometry.manifold));
    g_renderer.geometry.manifold = GenerateManifoldMesh(
        g_renderer.geometry.vertices,
        g_renderer.geometry.normals,
        g_renderer.geometry.triangles);
    if (!IsManifoldValid(&(g_renderer.geometry.manifold))) {
        logerror("Mesh was not detected to be a valid manifold");
        CleanManifoldMesh(&(g_renderer.geometry.manifold));
        return FALSE;
    } else {
        SerialRemesh(&(g_renderer.geometry.manifold), nudge);
        ReformatFromManifold(&(g_renderer.geometry));
        CleanManifoldMesh(&(g_renderer.geometry.manifold));
        return TRUE;
    }
}

void SavePose() {
    ReconstructARAP();
}

void RigidDeform() {
    #ifndef NO_MESH_MANIPULATION_SUPPORT
    inline BOOL isunlocked(size_t i) { return g_renderer.geometry.arap.v2f[i] != (size_t)-1; }
    inline void outer(vec3 a, vec3 b, mat3 result) {
        for (int col = 0; col < 3; col++) for (int row = 0; row < 3; row++) result[col][row] = a[row] * b[col];
    }
    for (size_t i = 0; i < g_renderer.geometry.vertices.size; i++) {
        if (isunlocked(i)) glm_vec3_copy(g_renderer.geometry.arap.originals[i], g_renderer.geometry.vertices.data[i]);
        if (RendererCamera()->config.arap.style == 1) {
            glm_vec3_copy(g_renderer.geometry.arap.normals[i], g_renderer.geometry.arap.z[i]);
            glm_vec3_zero(g_renderer.geometry.arap.u[i]);
        }
    }
    for (size_t i = 0; i < RendererCamera()->config.arap.iterations; i++) {
        // compute rotations
        for (size_t j = 0; j < g_renderer.geometry.vertices.size; j++) {
            glm_mat3_zero(g_renderer.geometry.arap.covariance[j]);
            glm_vec3_zero(g_renderer.geometry.arap.b[j]);
        }
        for (size_t j = 0; j < g_renderer.geometry.edges.size; j++) {
            Edge e = g_renderer.geometry.edges.data[j];
            if (!isunlocked(e.a) && !isunlocked(e.b)) continue;
            EdgeMeta em = HASHMAP_EdgeGlue_get(&(g_renderer.geometry.glue), e);
            vec3 xij;
            mat3 C;
            glm_vec3_sub(g_renderer.geometry.vertices.data[e.a], g_renderer.geometry.vertices.data[e.b], xij);
            outer(xij, em.pij, C);
            glm_mat3_scale(C, em.weight);
            if (isunlocked(e.a))Mat3Add(C, g_renderer.geometry.arap.covariance[e.a], g_renderer.geometry.arap.covariance[e.a]);
            if (isunlocked(e.b))Mat3Add(C, g_renderer.geometry.arap.covariance[e.b], g_renderer.geometry.arap.covariance[e.b]);
        }
        for (size_t j = 0; j < g_renderer.geometry.vertices.size; j++) {
            if (!isunlocked(j)) continue;
            if (RendererCamera()->config.arap.style == 1) {
                vec3* z = &g_renderer.geometry.arap.z[j];
                vec3* u = &g_renderer.geometry.arap.u[j];
                vec3* n = &g_renderer.geometry.arap.normals[j];
                float a = g_renderer.geometry.arap.areas[j];
                float lambda = RendererCamera()->config.arap.cube_lambda;
                float rho = RendererCamera()->config.arap.cube_rho;
                for (size_t admm = 0; admm < RendererCamera()->config.arap.addm; admm++) {
                    vec3 z_minus_u;
                    glm_vec3_sub(*z, *u, z_minus_u);
                    mat3 S, aug, R;
                    glm_mat3_copy(g_renderer.geometry.arap.covariance[j], S);
                    for (int col = 0; col < 3; col++)
                        for (int row = 0; row < 3; row++)
                            aug[col][row] = z_minus_u[row] * (*n)[col];
                    for (int col = 0; col < 3; col++)
                        for (int row = 0; row < 3; row++)
                            S[col][row] += (rho / 2.0f) * aug[col][row];
                    mat3 reg;
                    glm_mat3_copy(g_renderer.geometry.arap.rotations[j], reg);
                    glm_mat3_scale(reg, 1e-6f);
                    for (int c = 0; c < 3; c++)
                        for (int r = 0; r < 3; r++)
                            S[c][r] += reg[c][r];
                    PolarDecompose(S, R);
                    glm_mat3_copy(R, g_renderer.geometry.arap.rotations[j]);
                    vec3 Rn;
                    glm_mat3_mulv(R, *n, Rn);
                    float kappa = (lambda * a) / rho;
                    for (int k = 0; k < 3; k++) {
                        float val = Rn[k] + (*u)[k];
                        float sign = (val > 0.0f) ? 1.0f : (val < 0.0f) ? -1.0f : 0.0f;
                        (*z)[k] = sign * fmaxf(fabsf(val) - kappa, 0.0f);
                    }
                    for (int k = 0; k < 3; k++)
                        (*u)[k] += Rn[k] - (*z)[k];
                }
            } else {
                mat3 S, reg, R;
                glm_mat3_copy(g_renderer.geometry.arap.covariance[j], S);
                glm_mat3_copy(g_renderer.geometry.arap.rotations[j], reg);
                glm_mat3_scale(reg, 1e-6f);
                for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) S[c][r] += reg[c][r];
                PolarDecompose(S, R);
                glm_mat3_copy(R, g_renderer.geometry.arap.rotations[j]);
            }
        }

        // build RHS
        for (size_t j = 0; j < g_renderer.geometry.edges.size; j++) {
            Edge e = g_renderer.geometry.edges.data[j];
            EdgeMeta em = HASHMAP_EdgeGlue_get(&(g_renderer.geometry.glue), e);
            vec3 ti = { 0 }, ri_pij = { 0 }, rj_pij = { 0 }, neg_pij = { 0 }, rot_contrib = { 0 }, locked_contrib = { 0 };
            glm_vec3_negate_to(em.pij, neg_pij);
            if (isunlocked(e.a)) glm_mat3_mulv(g_renderer.geometry.arap.rotations[e.a], em.pij, ri_pij);
            if (isunlocked(e.b)) glm_mat3_mulv(g_renderer.geometry.arap.rotations[e.b], em.pij, rj_pij);
            glm_vec3_add(ri_pij, rj_pij, ti);
            glm_vec3_scale(ti, 0.5f * em.weight, ti);
            if (isunlocked(e.a)) glm_vec3_add(g_renderer.geometry.arap.b[e.a], ti, g_renderer.geometry.arap.b[e.a]);
            glm_vec3_negate_to(ti, ti);
            if (isunlocked(e.b)) glm_vec3_add(g_renderer.geometry.arap.b[e.b], ti, g_renderer.geometry.arap.b[e.b]);
            if (!isunlocked(e.a) && isunlocked(e.b)) {
                glm_mat3_mulv(g_renderer.geometry.arap.rotations[e.b], em.pij, rot_contrib);
                glm_vec3_scale(rot_contrib, -0.5f * em.weight, rot_contrib);
                glm_vec3_add(g_renderer.geometry.arap.b[e.b], rot_contrib, g_renderer.geometry.arap.b[e.b]);
                glm_vec3_scale(g_renderer.geometry.vertices.data[e.a], em.weight, locked_contrib);
                glm_vec3_add(g_renderer.geometry.arap.b[e.b], locked_contrib, g_renderer.geometry.arap.b[e.b]);
            } else if (isunlocked(e.a) && !isunlocked(e.b)) {
                glm_mat3_mulv(g_renderer.geometry.arap.rotations[e.a], neg_pij, rot_contrib);
                glm_vec3_scale(rot_contrib, -0.5f * em.weight, rot_contrib);
                glm_vec3_add(g_renderer.geometry.arap.b[e.a], rot_contrib, g_renderer.geometry.arap.b[e.a]);
                glm_vec3_scale(g_renderer.geometry.vertices.data[e.b], em.weight, locked_contrib);
                glm_vec3_add(g_renderer.geometry.arap.b[e.a], locked_contrib, g_renderer.geometry.arap.b[e.a]);
            }
        }

        // solve
        int rows = g_renderer.geometry.arap.L->n;
        cholmod_dense* b_dense = cholmod_allocate_dense(rows, 3, rows, CHOLMOD_REAL, &g_cholmod);
        double* b_ptr = (double*)b_dense->x;
        int free_idx = 0;
        for (size_t j = 0; j < g_renderer.geometry.vertices.size; j++) {
            if (isunlocked(j)) {
                b_ptr[free_idx + 0*rows] = g_renderer.geometry.arap.b[j][0];
                b_ptr[free_idx + 1*rows] = g_renderer.geometry.arap.b[j][1];
                b_ptr[free_idx + 2*rows] = g_renderer.geometry.arap.b[j][2];
                free_idx++;
            }
        }
        cholmod_dense* x_dense = cholmod_solve(CHOLMOD_A, g_renderer.geometry.arap.L, b_dense, &g_cholmod);
        double* x_ptr = (double*)x_dense->x;
        free_idx = 0;
        for (size_t j = 0; j < g_renderer.geometry.vertices.size; j++) {
            if (isunlocked(j)) {
                g_renderer.geometry.vertices.data[j][0] = x_ptr[free_idx + 0*rows];
                g_renderer.geometry.vertices.data[j][1] = x_ptr[free_idx + 1*rows];
                g_renderer.geometry.vertices.data[j][2] = x_ptr[free_idx + 2*rows];
                free_idx++;
            }
        }
        cholmod_free_dense(&b_dense, &g_cholmod);
        cholmod_free_dense(&x_dense, &g_cholmod);
    }
    #endif
}

void UpdateSimulation() {
    g_renderer.geometry.changes.update_simulation = TRUE;
}

void StepSimulation() {
    BOOL dynamicts = g_renderer.geometry.fluid.timestep == 0.0f;
    if (dynamicts) g_renderer.geometry.fluid.timestep = GetFrameTime();
    SimulateFluidStep(&(g_renderer.geometry.fluid));
    if (dynamicts) g_renderer.geometry.fluid.timestep = 0.0f;
    UpdateSimulation();
}

void ClearSimulation() {
    for (size_t i = 0; i < g_renderer.geometry.fluid.sourcenames.size; i++)
        EZ_FREE(g_renderer.geometry.fluid.sourcenames.data[i]);
    for (size_t i = 0; i < g_renderer.geometry.fluid.forcenames.size; i++)
        EZ_FREE(g_renderer.geometry.fluid.forcenames.data[i]);
    ARRLIST_FluidForce_clear(&(g_renderer.geometry.fluid.forces));
    ARRLIST_FluidSource_clear(&(g_renderer.geometry.fluid.sources));
    ARRLIST_DynamicString_clear(&(g_renderer.geometry.fluid.forcenames));
    ARRLIST_DynamicString_clear(&(g_renderer.geometry.fluid.sourcenames));
    if (g_renderer.geometry.fluid.velocity != NULL) {
        EZ_FREE(g_renderer.geometry.fluid.velocity);
        EZ_FREE(g_renderer.geometry.fluid.vswap);
        EZ_FREE(g_renderer.geometry.fluid.density);
        EZ_FREE(g_renderer.geometry.fluid.dswap);
        EZ_FREE(g_renderer.geometry.fluid.pressure);
        EZ_FREE(g_renderer.geometry.fluid.divergence);
    }
}

void ConfigureSimulation(size_t w, size_t h, size_t l, float dt) {
    ClearSimulation();
    g_renderer.geometry.fluid.width = w;
    g_renderer.geometry.fluid.length = h;
    g_renderer.geometry.fluid.height = l;
    g_renderer.geometry.fluid.timestep = dt;
    size_t ss = SimSize(g_renderer.geometry.fluid);
    g_renderer.geometry.fluid.velocity = EZ_ALLOC(ss, sizeof(vec3));
    g_renderer.geometry.fluid.vswap = EZ_ALLOC(ss, sizeof(vec3));
    g_renderer.geometry.fluid.density = EZ_ALLOC(ss, sizeof(float));
    g_renderer.geometry.fluid.dswap = EZ_ALLOC(ss, sizeof(float));
    g_renderer.geometry.fluid.pressure = EZ_ALLOC(ss, sizeof(float));
    g_renderer.geometry.fluid.divergence = EZ_ALLOC(ss, sizeof(float));
    UpdateSimulation();
}

void RestartSimulation() {
    size_t ss = SimSize(g_renderer.geometry.fluid);
    memset(g_renderer.geometry.fluid.velocity, 0, ss * sizeof(vec3));
    memset(g_renderer.geometry.fluid.vswap, 0, ss * sizeof(vec3));
    memset(g_renderer.geometry.fluid.density, 0, ss * sizeof(float));
    memset(g_renderer.geometry.fluid.dswap, 0, ss * sizeof(float));
    memset(g_renderer.geometry.fluid.pressure, 0, ss * sizeof(float));
    memset(g_renderer.geometry.fluid.divergence, 0, ss * sizeof(float));
    for (size_t i = 0; i < g_renderer.geometry.fluid.sources.size; i++) {
        FluidSource s = g_renderer.geometry.fluid.sources.data[i];
        g_renderer.geometry.fluid.sources.data[i].timer = s.lifetime;
        if (s.lifetime == 0.0f) {
            size_t minx = MIN(s.x + 1, g_renderer.geometry.fluid.width - 1);
            size_t miny = MIN(s.y + 1, g_renderer.geometry.fluid.height - 1);
            size_t minz = MIN(s.z + 1, g_renderer.geometry.fluid.length - 1);
            size_t maxx = MIN(s.x + s.width + 1, g_renderer.geometry.fluid.width);
            size_t maxy = MIN(s.y + s.height + 1, g_renderer.geometry.fluid.height);
            size_t maxz = MIN(s.z + s.length + 1, g_renderer.geometry.fluid.length);
            for (size_t i = minx; i < maxx; i++) {
                for (size_t j = miny; j < maxy; j++) {
                    for (size_t k = minz; k < maxz; k++) {
                        g_renderer.geometry.fluid.density[SimIndex(g_renderer.geometry.fluid, i, j, k)] = s.density;
                    }
                }
            }
        }
    }
    UpdateSimulation();
}

size_t NumForces() {
    return g_renderer.geometry.fluid.forces.size;
}

FluidForce* ForceReference(size_t index) {
    return &(g_renderer.geometry.fluid.forces.data[index]);
}

char** ForceNameReference(size_t index) {
    return &(g_renderer.geometry.fluid.forcenames.data[index]);
}

size_t NumSources() {
    return g_renderer.geometry.fluid.sources.size;
}

FluidSource* SourceReference(size_t index) {
    return &(g_renderer.geometry.fluid.sources.data[index]);
}

char** SourceNameReference(size_t index) {
    return &(g_renderer.geometry.fluid.sourcenames.data[index]);
}

void SubmitForce(FluidForce force, const char* name) {
    ARRLIST_FluidForce_add(&(g_renderer.geometry.fluid.forces), force);
    char* b = EZ_ALLOC(MAX_FORCE_NAME_SIZE + 1, sizeof(char));
    strncpy(b, name, MAX_FORCE_NAME_SIZE);
    ARRLIST_DynamicString_add(&(g_renderer.geometry.fluid.forcenames), b);
    UpdateSimulation();
}

void SubmitSource(FluidSource source, const char* name) {
    ARRLIST_FluidSource_add(&(g_renderer.geometry.fluid.sources), source);
    char* b = EZ_ALLOC(MAX_SOURCE_NAME_SIZE + 1, sizeof(char));
    strncpy(b, name, MAX_SOURCE_NAME_SIZE);
    ARRLIST_DynamicString_add(&(g_renderer.geometry.fluid.sourcenames), b);
    UpdateSimulation();
}

size_t NumMeshes() {
    return g_renderer.geometry.meshes.size;
}

MeshDescriptor* MeshReference(size_t index) {
    return &(g_renderer.geometry.meshes.data[index]);
}

char** MeshNameReference(size_t index) {
    return &(g_renderer.geometry.meshnames.data[index]);
}

void SubmitMeshDescriptor(MeshDescriptor md, const char* name) {
    ARRLIST_MeshDescriptor_add(&(g_renderer.geometry.meshes), md);
    char* b = EZ_ALLOC(MAX_MESH_NAME_SIZE + 1, sizeof(char));
    strncpy(b, name, MAX_MESH_NAME_SIZE);
    ARRLIST_DynamicString_add(&(g_renderer.geometry.meshnames), b);
    UpdateMeshes();
}

void ClearMeshDescriptors() {
    for (size_t i = 0; i < g_renderer.geometry.meshnames.size; i++)
        EZ_FREE(g_renderer.geometry.meshnames.data[i]);
    ARRLIST_DynamicString_clear(&g_renderer.geometry.meshnames);
    ARRLIST_MeshDescriptor_clear(&g_renderer.geometry.meshes);
}

void UpdateObjectTransform(size_t i) {
    EZ_ASSERT(i < g_renderer.geometry.meshes.size, "Cannot set object transform due to index out of bounds");
    mat4 T, R, S, TR, transform;
    glm_mat4_identity(T);
    glm_mat4_identity(R);
    glm_mat4_identity(S);
    glm_translate(T, g_renderer.geometry.meshes.data[i].translate);
    glm_rotate_x(R, glm_rad(g_renderer.geometry.meshes.data[i].rotate[0]), R);
    glm_rotate_y(R, glm_rad(g_renderer.geometry.meshes.data[i].rotate[1]), R);
    glm_rotate_z(R, glm_rad(g_renderer.geometry.meshes.data[i].rotate[2]), R);
    glm_scale(S, g_renderer.geometry.meshes.data[i].scale);
    glm_mat4_mul(T, R, TR);
    glm_mat4_mul(TR, S, transform);
    memcpy(g_renderer.geometry.meshes.data[i].transform, transform, sizeof(mat4));
    UpdateMeshes();
}

void UpdateMeshes() {
    g_renderer.geometry.changes.update_meshes = TRUE;
}

void SubmitAnimation(size_t meshid, Skeleton skeleton, Animation animation) {
    char* name = EZ_ALLOC(strlen(animation.name) + 1, sizeof(char));
    strcpy(name, animation.name);
    for (size_t i = 0; i < g_renderer.geometry.animations.size; i++) {
        if (g_renderer.geometry.animations.data[i].meshid == meshid) {
            ARRLIST_Animation_add(&(g_renderer.geometry.animations.data[i].animations), animation);
            ARRLIST_DynamicString_add(&(g_renderer.geometry.animations.data[i].names), name);
            return;
        }
    }
    ARRLIST_Animation a = { 0 };
    ARRLIST_DynamicString n = { 0 };
    ARRLIST_Animation_add(&a, animation);
    ARRLIST_DynamicString_add(&n, name);
    ARRLIST_MeshAnimation_add(&(g_renderer.geometry.animations), (MeshAnimation){
        meshid, skeleton, a, n,
        0, 0.0f, TRUE, FALSE, TRUE, TRUE,
        0, 0.0f, 1.0f, 0.5f, FALSE
    });
    for (size_t i = 0; i < MAX_BONES; i++) {
        mat4 m = GLM_MAT4_IDENTITY_INIT;
        ARRLIST_mat4_add(&(g_renderer.geometry.poses), m);
    }
    UpdatePoses();
}

void SubmitVertexSkin(VertexSkin skin) {
    float total = 0.0f;
    for (size_t i = 0; i < MAX_BONE_INFLUENCES; i++) total += skin.weights[i];
    EZ_ASSERT(total != 0.0f, "Zero skin weight detected");
    for (size_t i = 0; i < MAX_BONE_INFLUENCES; i++) skin.weights[i] /= total;
    ARRLIST_VertexSkin_add(&(g_renderer.geometry.skins), skin);
    UpdateSkins();
}

size_t NumSkins() {
    return g_renderer.geometry.skins.size;
}

size_t NumAnimations() {
    return g_renderer.geometry.animations.size;
}

MeshAnimation* AnimationReference(size_t animid) {
    return &(g_renderer.geometry.animations.data[animid]);
}

void SwitchAnimation(MeshAnimation* anim, size_t newind) {
    if (anim->current == newind) return;
    anim->previous = anim->current;
    anim->ptime = anim->time;
    anim->current = newind;
    anim->time = 0.0f;
    anim->bweight = 0.0f;
    anim->blending = TRUE;
}

void ClearAnimations() {
    for (size_t i = 0; i < g_renderer.geometry.animations.size; i++)
        FreeMeshAnimation(AnimationReference(i));
    ARRLIST_MeshAnimation_clear(&(g_renderer.geometry.animations));
    ARRLIST_VertexSkin_clear(&(g_renderer.geometry.skins));
    ARRLIST_mat4_clear(&(g_renderer.geometry.poses));
    UpdateSkins();
    UpdatePoses();
}

void UpdateSkins() {
    g_renderer.geometry.changes.update_skins = TRUE;
}

void UpdatePoses() {
    g_renderer.geometry.changes.update_poses = TRUE;
}

void SaveViewportRender(const char* filepath) {
    Rectangle r = GetViewportRec();
	RenderTexture rt = LoadRenderTexture(r.width, r.height);
    BeginTextureMode(rt);
    DrawHelper(0, 0, r.width, r.height, g_renderer.dimensions.x, g_renderer.dimensions.y);
    EndTextureMode();
    Image image = LoadImageFromTexture(rt.texture);
    ImageFlipVertical(&image);
    ExportImage(image, filepath);
    UnloadImage(image);
    UnloadRenderTexture(rt);
}

void SaveRender(const char* filepath) {
	RenderTexture rt = LoadRenderTexture(g_renderer.dimensions.x, g_renderer.dimensions.y);
    BeginTextureMode(rt);
    DrawHelper(0, 0, -g_renderer.dimensions.x, g_renderer.dimensions.y, g_renderer.dimensions.x, g_renderer.dimensions.y);
    EndTextureMode();
    Image image = LoadImageFromTexture(rt.texture);
    ExportImage(image, filepath);
    UnloadImage(image);
    UnloadRenderTexture(rt);
}

BOOL CaptureRender(uint8_t* rgba) {
    const int w = (int)g_renderer.dimensions.x;
    const int h = (int)g_renderer.dimensions.y;
    RenderTexture rt = LoadRenderTexture(w, h);
    if (!IsRenderTextureValid(rt)) return FALSE;
    BeginTextureMode(rt);
    DrawHelper(0, 0, w, -h, w, h);
    EndTextureMode();
    Image image = LoadImageFromTexture(rt.texture);
    BOOL ok = image.data != NULL;
    if (ok) {
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        memcpy(rgba, image.data, (size_t)w * (size_t)h * 4);
    }
    UnloadImage(image);
    UnloadRenderTexture(rt);
    return ok;
}

char* GPUModel() {
    return g_renderer.vulkan.core.general.gpuname;
}

void PollGPUCache(BOOL init) {
    if (init || (GetTime() - g_renderer.stats.cache.update_timer) > g_renderer.stats.cache.update_interval) {
		if (init) {
			ARRLIST_StaticString_add(&(g_renderer.vulkan.metadata.extensions.required), "VK_KHR_get_physical_device_properties2");
			if (!VUTIL_CheckGPUExtensionSupport(g_renderer.vulkan.core.general.gpu)) {
				g_renderer.stats.cache.available = FALSE;
			} else {
				g_renderer.stats.cache.available = TRUE;
			}
			ARRLIST_StaticString_remove(&(g_renderer.vulkan.metadata.extensions.required), g_renderer.vulkan.metadata.extensions.required.size - 1);
		}
		if (g_renderer.stats.cache.available) {
			g_renderer.stats.cache.update_timer = GetTime();
			g_renderer.stats.cache.heap_budget = (VkPhysicalDeviceMemoryBudgetPropertiesEXT) { 0 };
	        g_renderer.stats.cache.heap_budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
			g_renderer.stats.cache.heap_budget.pNext = NULL;
			g_renderer.stats.cache.heap_props = (VkPhysicalDeviceMemoryProperties2) { 0 };
			g_renderer.stats.cache.heap_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
			g_renderer.stats.cache.heap_props.pNext = &(g_renderer.stats.cache.heap_budget);
			vkGetPhysicalDeviceMemoryProperties2(g_renderer.vulkan.core.general.gpu, &(g_renderer.stats.cache.heap_props));
		}
    }
}

size_t GPUHeapCount() {
	if (!g_renderer.stats.cache.available) return 0;
    return g_renderer.stats.cache.heap_props.memoryProperties.memoryHeapCount;
}

size_t GPUHeapUsage(size_t i) {
	if (!g_renderer.stats.cache.available) return 0;
    return g_renderer.stats.cache.heap_budget.heapUsage[i];
}

size_t GPUHeapBudget(size_t i) {
	if (!g_renderer.stats.cache.available) return 0;
    return g_renderer.stats.cache.heap_budget.heapBudget[i];
}

const char* GPUHeapType(size_t i) {
	if (!g_renderer.stats.cache.available) return "Unavailable";
    if (g_renderer.stats.cache.heap_props.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        return "LOCAL";
    if (g_renderer.stats.cache.heap_props.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT)
        return "MULTI";
    return "SHARE";
}

void SubmitExternalShader(const char* location, const char* binary, size_t invocations) {
    EZ_ASSERT(!g_renderer_init, "Cannot submit external shaders after renderer has been initialized");
    ARRLIST_ShaderLocation_add(&(g_renderer.externals), (ShaderLocation){ location, binary, invocations });
}

ShaderBuffer* CreateExternalBuffer(const char* bindname, size_t size) {
    EZ_ASSERT(!g_renderer_init, "Cannot create external buffers after renderer has been initialized");
    ShaderBuffer sb = { 0 };
    sb.bindname = bindname;
    sb.size = size;
    if (size != 0) sb.data = EZ_ALLOC(size, sizeof(char));
    ARRLIST_ShaderBuffer_add(&(g_renderer.ebuffers), sb);
    return &(g_renderer.ebuffers.data[g_renderer.ebuffers.size - 1]);
}

void UpdateShaderBuffer(ShaderBuffer* buffer) {
    buffer->update = TRUE;
}

void ClearScene(BOOL hard) {
    ClearTriangles();
    ClearVertices();
    ClearNormals();
    if (hard) ClearMaterials();
    ClearLights();
    ClearMeshDescriptors();
    ClearAnimations();
    ClearSimulation();
    ClearSplats();
    DeselectEditTarget();
    if (hard) ClearCameras();
}

void ClearSplats() {
    if (g_renderer.geometry.splats.maxsize == 0) return;
    ARRLIST_GaussianSplat_clear(&(g_renderer.geometry.splats));
    SETVEC3(g_renderer.geometry.splatbounds.min, FLT_MAX, FLT_MAX, FLT_MAX);
    SETVEC3(g_renderer.geometry.splatbounds.max, -FLT_MAX, -FLT_MAX, -FLT_MAX);
    UpdateSplats();
}

size_t NumSplats() {
    return g_renderer.geometry.splats.size;
}

void SubmitSplat(GaussianSplat splat) {
    ARRLIST_GaussianSplat_add(&(g_renderer.geometry.splats), splat);
    AxisAlignedBoundingBox box = SplatBounds(&splat);
    glm_vec3_minv(box.min, g_renderer.geometry.splatbounds.min, g_renderer.geometry.splatbounds.min);
    glm_vec3_maxv(box.max, g_renderer.geometry.splatbounds.max, g_renderer.geometry.splatbounds.max);
    UpdateSplats();
}

void UpdateSplats() {
    g_renderer.geometry.changes.update_splats = TRUE;
}

void UpdateEmissives() {
    g_renderer.geometry.changes.update_emissives = TRUE;
}

void RefreshEmissives() {
    g_renderer.geometry.changes.refresh_emissives = TRUE;
}

size_t FindMeshFromTriangle(TriangleID tid) {
    size_t lo = 0, hi = g_renderer.geometry.meshes.size;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_renderer.geometry.meshes.data[mid].tstart <= tid) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return (size_t)-1;
    MeshDescriptor* md = &(g_renderer.geometry.meshes.data[lo - 1]);
    if (md->tend < md->tstart || md->tend == (TriangleID)-1 || tid > md->tend) return (size_t)-1;
    return lo - 1;
}

BOOL FindTriangleTransform(TriangleID tid, mat4 dest) {
    glm_mat4_identity(dest);
    size_t mid = FindMeshFromTriangle(tid);
    if (mid != (size_t)-1) {
        MeshDescriptor* md = MeshReference(mid);
        if (md->disabled) {
            glm_mat4_zero(dest);
        } else {
            memcpy(dest, md->transform, sizeof(mat4));
        }
        return TRUE;
    }
    return FALSE;
}

BOOL FindVertexTransform(VertexID vid, mat4 dest) {
    glm_mat4_identity(dest);
    size_t lo = 0, hi = g_renderer.geometry.meshes.size;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_renderer.geometry.meshes.data[mid].start <= vid) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return FALSE;
    MeshDescriptor* md = &(g_renderer.geometry.meshes.data[lo - 1]);
    if (md->end < md->start || md->end == (VertexID)-1 || vid > md->end) return FALSE;
    if (md->disabled) {
        glm_mat4_zero(dest);
        return TRUE;
    }
    memcpy(dest, md->transform, sizeof(mat4));
    return TRUE;
}

BOOL DeleteMaterial(MaterialID id) {
    EZ_ASSERT(id < g_renderer.geometry.materials.size, "Cannot delete a non-existant material");
    if (id == 0) {
        logerror("Cannot delete the default material");
        return FALSE;
    }
    BOOL emissive = g_renderer.geometry.materials.data[id].emission[0] != 0 ||
        g_renderer.geometry.materials.data[id].emission[1] != 0 ||
        g_renderer.geometry.materials.data[id].emission[2] != 0;
    EZ_FREE(g_renderer.geometry.materialnames.data[id]);
    ARRLIST_DynamicString_remove(&(g_renderer.geometry.materialnames), id);
    ARRLIST_SurfaceMaterial_remove(&(g_renderer.geometry.materials), id);
    FixDeleteEditTarget(EDIT_MATERIAL, id);
    for (size_t i = 0; i < g_renderer.geometry.triangles.size; i++) {
        if (TriangleReference(i)->material > id) {
            TriangleReference(i)->material--;
        } else if (TriangleReference(i)->material == id) {
            TriangleReference(i)->material = 0;
            if (emissive) {
                for (size_t j = 0; j < g_renderer.geometry.emissives.size; j++) {
                    if (g_renderer.geometry.emissives.data[j] == i) {
                        ARRLIST_TriangleID_remove(&(g_renderer.geometry.emissives), j);
                        break;
                    }
                }
            }
        }
    }
    UpdateTriangles();
    UpdateEmissives();
    UpdateMaterials();
    return TRUE;
}

BOOL DeleteLight(LightID id) {
    EZ_ASSERT(id < g_renderer.geometry.lights.size, "Cannot delete a non-existant light");
    EZ_FREE(g_renderer.geometry.lightnames.data[id]);
    ARRLIST_DynamicString_remove(&(g_renderer.geometry.lightnames), id);
    ARRLIST_SceneLight_remove(&(g_renderer.geometry.lights), id);
    FixDeleteEditTarget(EDIT_LIGHT, id);
    UpdateLights();
    return TRUE;
}

BOOL DeleteTriangle(TriangleID id) {
    EZ_ASSERT(id < g_renderer.geometry.triangles.size, "Cannot delete a non-existant triangle");
    size_t md = FindMeshFromTriangle(id);
    if (md == (size_t)-1) md = 0;
    UnglueTriangle(id);
    ARRLIST_Triangle_remove(&(g_renderer.geometry.triangles), id);
    for (size_t i = md; i < g_renderer.geometry.meshes.size; i++) {
        MeshDescriptor* md = MeshReference(i);
        if (md->tstart > id) md->tstart--;
        if (md->tend >= id) md->tend--;
    }
    FixDeleteEditTarget(EDIT_SINGLE_TRIANGLE, id);
    UpdateMeshes();
    UpdateTriangles();
    RefreshEmissives();
    return TRUE;
}

BOOL DeleteVertex(VertexID id) {
    EZ_ASSERT(id < g_renderer.geometry.vertices.size, "Cannot delete a non-existant vertex");
    uint8_t* doomed = EZ_ALLOC(g_renderer.geometry.triangles.size > 0 ? g_renderer.geometry.triangles.size : 1, sizeof(uint8_t));
    for (size_t t = 0; t < g_renderer.geometry.triangles.size; t++) {
        Triangle* tri = &(g_renderer.geometry.triangles.data[t]);
        doomed[t] = tri->a == id || tri->b == id || tri->c == id;
    }
    size_t tremoved = RemoveFlaggedTriangles(doomed);
    EZ_FREE(doomed);
    for (size_t t = 0; t < g_renderer.geometry.triangles.size; t++) {
        Triangle* tri = &(g_renderer.geometry.triangles.data[t]);
        if (tri->a > id) tri->a--;
        if (tri->b > id) tri->b--;
        if (tri->c > id) tri->c--;
    }
    size_t owner = (size_t)-1;
    for (size_t m = 0; m < g_renderer.geometry.meshes.size; m++) {
        MeshDescriptor* md = &(g_renderer.geometry.meshes.data[m]);
        BOOL empty = md->end < md->start || md->end == (VertexID)-1;
        if (!empty && md->start <= id && id <= md->end) { owner = m; break; }
    }
    uint32_t skin = (uint32_t)-1;
    if (owner != (size_t)-1) {
        MeshDescriptor* md = &(g_renderer.geometry.meshes.data[owner]);
        size_t s = (size_t)md->skinstart + (id - md->start);
        if (md->pose != (uint32_t)-1 && s < g_renderer.geometry.skins.size) {
            skin = (uint32_t)s;
            ARRLIST_VertexSkin_remove(&(g_renderer.geometry.skins), s);
        }
    }
    for (size_t m = 0; m < g_renderer.geometry.meshes.size; m++) {
        MeshDescriptor* md = &(g_renderer.geometry.meshes.data[m]);
        if (m == owner) {
            md->end--;
        } else if (md->start > id) {
            BOOL empty = md->end < md->start || md->end == (VertexID)-1;
            md->start--;
            md->end = empty ? md->start - 1 : md->end - 1;
        }
        if (skin != (uint32_t)-1 && m != owner && md->skinstart > skin) md->skinstart--;
    }
    ARRLIST_vec4_remove(&(g_renderer.geometry.vertices), id);
    RemoveLockRange(id, 1);
    RebuildConnectivity();
    if (tremoved > 0 && g_renderer.geometry.emissives.size > 0) RefreshEmissives();
    EditType type;
    size_t index;
    if (tremoved > 0 && GetEditTarget(&type, &index) && type == EDIT_SINGLE_TRIANGLE) DeselectEditTarget();
    FixDeleteEditTarget(EDIT_SINGLE_VERTEX, id);
    UpdateVertices();
    UpdateTriangles();
    UpdateMeshes();
    if (skin != (uint32_t)-1) UpdateSkins();
    if (tremoved > 0) UpdateEmissives();
    return TRUE;
}

BOOL DeleteMesh(size_t id) {
    EZ_ASSERT(id < g_renderer.geometry.meshes.size, "Cannot delete a non-existant mesh");
    MeshDescriptor md = { 0 };
    memcpy(&md, &(g_renderer.geometry.meshes.data[id]), sizeof(MeshDescriptor));
    BOOL has_tris = !(md.tend < md.tstart || md.tend == (TriangleID)-1);
    BOOL has_verts = !(md.end < md.start || md.end == (VertexID)-1);
    size_t tcount = has_tris ? (size_t)md.tend - md.tstart + 1 : 0;
    size_t vcount = has_verts ? (size_t)md.end - md.start + 1 : 0;
    if (md.tstart + tcount > g_renderer.geometry.triangles.size || md.start + vcount > g_renderer.geometry.vertices.size) {
        logerror("Mesh %zu describes triangles/vertices outside of the scene", id);
        return FALSE;
    }
    uint32_t nmin = UINT32_MAX;
    uint32_t nmax = 0;
    for (size_t t = md.tstart; t < md.tstart + tcount; t++) {
        uint32_t ns[] = { g_renderer.geometry.triangles.data[t].an, g_renderer.geometry.triangles.data[t].bn, g_renderer.geometry.triangles.data[t].cn };
        for (size_t k = 0; k < 3; k++) {
            if (ns[k] == (uint32_t)-1) continue;
            if (ns[k] < nmin) nmin = ns[k];
            if (ns[k] > nmax) nmax = ns[k];
        }
    }
    size_t ncount = (nmin <= nmax && nmax < g_renderer.geometry.normals.size) ? (size_t)nmax - nmin + 1 : 0;
    for (size_t t = 0; t < g_renderer.geometry.triangles.size; t++) {
        if (t >= md.tstart && t < md.tstart + tcount) continue;
        Triangle tri = g_renderer.geometry.triangles.data[t];
        VertexID vs[] = { tri.a, tri.b, tri.c };
        uint32_t ns[] = { tri.an, tri.bn, tri.cn };
        for (size_t k = 0; k < 3; k++) {
            if (vs[k] >= md.start && vs[k] < md.start + vcount) {
                logerror("Cannot delete mesh %zu: triangle %zu outside of it uses its vertices", id, t);
                return FALSE;
            }
            if (ncount && ns[k] != (uint32_t)-1 && ns[k] >= nmin && ns[k] <= nmax) ncount = 0;
        }
    }
    ARRLIST_REMOVE_RANGE(g_renderer.geometry.triangles, md.tstart, tcount);
    for (size_t t = 0; t < g_renderer.geometry.triangles.size; t++) {
        Triangle* tri = &(g_renderer.geometry.triangles.data[t]);
        if (vcount) {
            if (tri->a >= md.start + vcount) tri->a -= (uint32_t)vcount;
            if (tri->b >= md.start + vcount) tri->b -= (uint32_t)vcount;
            if (tri->c >= md.start + vcount) tri->c -= (uint32_t)vcount;
        }
        if (ncount) {
            if (tri->an != (uint32_t)-1 && tri->an > nmax) tri->an -= (uint32_t)ncount;
            if (tri->bn != (uint32_t)-1 && tri->bn > nmax) tri->bn -= (uint32_t)ncount;
            if (tri->cn != (uint32_t)-1 && tri->cn > nmax) tri->cn -= (uint32_t)ncount;
        }
    }
    ARRLIST_REMOVE_RANGE(g_renderer.geometry.vertices, md.start, vcount);
    if (ncount) ARRLIST_REMOVE_RANGE(g_renderer.geometry.normals, nmin, ncount);
    size_t scount = 0;
    if (md.pose != (uint32_t)-1 && (size_t)md.skinstart + vcount <= g_renderer.geometry.skins.size) {
        scount = vcount;
        ARRLIST_REMOVE_RANGE(g_renderer.geometry.skins, md.skinstart, scount);
    }
    if (md.pose != (uint32_t)-1) {
        size_t aidx = md.pose / MAX_BONES;
        if (aidx < g_renderer.geometry.animations.size) {
            FreeMeshAnimation(&(g_renderer.geometry.animations.data[aidx]));
            ARRLIST_MeshAnimation_remove(&(g_renderer.geometry.animations), aidx);
        }
        ARRLIST_REMOVE_RANGE(g_renderer.geometry.poses, md.pose, (size_t)MAX_BONES);
    }
    for (size_t i = 0; i < g_renderer.geometry.animations.size; i++) {
        size_t meshid;
        memcpy(&meshid, &(g_renderer.geometry.animations.data[i].meshid), sizeof(size_t));
        if (meshid > id) {
            meshid--;
            memcpy(&(g_renderer.geometry.animations.data[i].meshid), &meshid, sizeof(size_t));
        }
    }
    EZ_FREE(g_renderer.geometry.meshnames.data[id]);
    ARRLIST_DynamicString_remove(&(g_renderer.geometry.meshnames), id);
    ARRLIST_MeshDescriptor_remove(&(g_renderer.geometry.meshes), id);
    for (size_t i = 0; i < g_renderer.geometry.meshes.size; i++) {
        MeshDescriptor* other = &(g_renderer.geometry.meshes.data[i]);
        if (tcount && other->tstart >= md.tstart + tcount) {
            other->tstart -= (uint32_t)tcount;
            if (other->tend != (TriangleID)-1) other->tend -= (uint32_t)tcount;
        }
        if (vcount && other->start >= md.start + vcount) {
            other->start -= (uint32_t)vcount;
            if (other->end != (VertexID)-1) other->end -= (uint32_t)vcount;
        }
        if (scount && other->skinstart >= md.skinstart + scount) other->skinstart -= (uint32_t)scount;
        if (md.pose != (uint32_t)-1 && other->pose != (uint32_t)-1 && other->pose > md.pose) other->pose -= MAX_BONES;
    }
    RemoveLockRange(md.start, vcount);
    RebuildConnectivity();
    if (g_renderer.geometry.emissives.size > 0) RefreshEmissives();
    EditType type;
    size_t index;
    if (GetEditTarget(&type, &index) && (type == EDIT_SINGLE_TRIANGLE || type == EDIT_SINGLE_VERTEX))
        DeselectEditTarget();
    FixDeleteEditTarget(EDIT_MESH, id);
    UpdateNormals();
    UpdateVertices();
    UpdateTriangles();
    UpdateMeshes();
    UpdateSkins();
    UpdatePoses();
    UpdateEmissives();
    return TRUE;
}

BOOL DeleteCamera(size_t id) {
    EZ_ASSERT(id < g_renderer.geometry.cameras.size, "Cannot delete a non-existant camera");
    if (id == 0) {
        logerror("Cannot delete camera - there must be at least one camera to view the scene");
        return FALSE;
    }
    EZ_FREE(g_renderer.geometry.cameranames.data[id]);
    ARRLIST_DynamicString_remove(&(g_renderer.geometry.cameranames), id);
    ARRLIST_SceneCamera_remove(&(g_renderer.geometry.cameras), id);
    FixDeleteEditTarget(EDIT_CAMERA, id);
    if (PrimaryCameraID() == id) SetCamera(0);
    return TRUE;
}
