#include "renderer.h"
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/vulkan/vupdate.h"
#include "renderer/vulkan/vclean.h"
#include "renderer/processor.h"
#include "renderer/simulator.h"
#include "renderer/overlay.h"
#include "renderer/rmath.h"
#include <easylogger.h>
#include <GLFW/glfw3.h>
#include <easymemory.h>
#include <string.h>
#include <time.h>

Renderer g_renderer = { 0 };
Vector2 g_override_resolution = { 0 };
float g_rft = 0.0f;
cholmod_common g_cholmod = { 0 };

void CleanARAP() {
    if (g_renderer.geometry.arap.values != NULL) {
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
        cholmod_free_sparse(&g_renderer.geometry.arap.A, &g_cholmod);
        cholmod_free_factor(&g_renderer.geometry.arap.L, &g_cholmod);
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
}

float EdgeWeight(Edge e) {
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

void ReconstructARAP() {
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
}

PipelineFlags GetPipelineFlags() {
    return g_renderer.config.flags;
}

void SetPipelineFlags(PipelineFlags flags) {
    g_renderer.config.flags = flags;
}

void SetViewportSlice(size_t w, size_t h) {
	float psuedo_w = w * (g_renderer.dimensions.x / (float)GetScreenWidth());
	float psuedo_h = h * (g_renderer.dimensions.y / (float)GetScreenHeight());
    g_renderer.viewport = (Vector2) { ceil(psuedo_w), ceil(psuedo_h) };
}

void OverrideResolution(size_t x, size_t y) {
	g_override_resolution = (Vector2){ x, y };
}

void InitializeRenderer() {
	// init rand
	srand(time(NULL));

    // init cholmod
    cholmod_start(&g_cholmod);

    // initialize config
    g_renderer.config.whitepoint = 20.0f;
    g_renderer.config.gamma = 2.2f;
    g_renderer.config.direct = TRUE;
    g_renderer.config.grid = TRUE;
    g_renderer.config.async = TRUE;
    g_renderer.config.showdof = TRUE;
    g_renderer.config.directonly = FALSE;
    g_renderer.config.scenelighting = TRUE;
    g_renderer.config.scenelightingonly = FALSE;
    g_renderer.config.scenelightshadows = TRUE;
    g_renderer.config.normals = TRUE;
    g_renderer.config.flags = PREVIEW_PIPELINE_FLAGS;
    g_renderer.config.arap.iterations = 10;
    g_renderer.config.arap.style = 0;
    g_renderer.config.arap.cube_lambda = 0.5f;
    g_renderer.config.arap.cube_rho = 1e-4f;
    g_renderer.config.arap.addm = 5;

    // initialize sim
    g_renderer.geometry.fluid.timestep = 0.016f;
    g_renderer.geometry.fluid.diffusion = 0.000001;
    g_renderer.geometry.fluid.dissipation = 0.005f;
    g_renderer.geometry.fluid.viscosity = 0.000001f;
    g_renderer.geometry.fluid.iterations = 20;

    // initialize min/max BB
    SETVEC3(g_renderer.geometry.bounds.min, FLT_MAX, FLT_MAX, FLT_MAX);
    SETVEC3(g_renderer.geometry.bounds.max, -FLT_MAX, -FLT_MAX, -FLT_MAX);

    // initialize camera
    g_renderer.camera = (SimpleCamera){
        { 0.0f, 2.133f, 2.11f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        90.0f, 0.0f, 0.0f
    };

    // set up dimensions
    g_renderer.dimensions = (Vector2){ 
		g_override_resolution.x == 0 ? GetScreenWidth() : g_override_resolution.x,
		g_override_resolution.y == 0 ? GetScreenHeight() : g_override_resolution.y };

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
    ConfigureProfile(&(g_renderer.stats.profile), "Renderer", 10);

    // configure GPU stat cache
    g_renderer.stats.cache.update_interval = 1.0;
    PollGPUCache(TRUE);

    // default material
    SubmitNamedMaterial((SurfaceMaterial){
        {0.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        0, 10.0f, 2
    }, "Default");

    // set overlay context
    SetOverlayContext(&g_renderer);
}

void DestroyRenderer() {
    // clean geometry
    ClearNormals();
    ClearVertices();
    ClearTriangles();
    ClearMaterials();
    ClearLights();
    CleanManifoldMesh(&(g_renderer.geometry.manifold));
    CleanARAP();
    ClearSimulation();
    ClearMeshDescriptors();

    // destroy cholmod
    cholmod_finish(&g_cholmod);

    // destroy vulkan resources
    VCLEAN_Vulkan(&(g_renderer.vulkan));

    // unload cpu swap textures
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++)
	    UnloadRenderTexture(g_renderer.swapchain.target[i]);
}

SimpleCamera GetCamera() {
    return g_renderer.camera;
}

void MoveCamera(SimpleCamera camera) {
    g_renderer.camera = camera;
}

void FitCamera() {
    vec3 min, max;
    if (g_renderer.config.flags & FLUID_SHADER_FLAG) {
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
    glm_vec3_sub(g_renderer.camera.position, g_renderer.camera.look, l2p);
    glm_vec3_normalize(l2p);
    vec3 extend, min2o, newo;
    glm_vec3_sub(max, min, extend);
    glm_vec3_scale(extend, 0.5f, min2o);
    glm_vec3_add(min2o, min, newo);
    float width = glm_vec3_norm(extend);
    glm_vec3_copy(newo, g_renderer.camera.look);
    glm_vec3_scale(l2p, width, l2p);
    glm_vec3_add(l2p, newo, g_renderer.camera.position);
}

void ReorientCamera() {
    for (int i = 0; i < 3; i++) {
        float sign = g_renderer.camera.up[i] > 0 ? 1.0f : (g_renderer.camera.up[i] < 0 ? -1.0f : 0.0f);
        g_renderer.camera.look[i] += sign*1e-6f;
    }
    vec3 desired = { 0, 1, 0 };
    glm_vec3_copy(desired, g_renderer.camera.up);
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
    g_renderer.geometry.changes.update_vertices = TRUE;
    vec4 v = { 0 };
    glm_vec3_copy(vertex, v);
    ARRLIST_vec4_add(&(g_renderer.geometry.vertices), v);
    glm_vec3_minv(g_renderer.geometry.bounds.min, vertex, g_renderer.geometry.bounds.min);
    glm_vec3_maxv(g_renderer.geometry.bounds.max, vertex, g_renderer.geometry.bounds.max);
}

void ClearVertices() {
    if (g_renderer.geometry.vertices.maxsize == 0) return;
    ARRLIST_vec4_clear(&(g_renderer.geometry.vertices));
    HASHMAP_Locks_clear(&(g_renderer.geometry.locks));
    ARRLIST_Edge_clear(&(g_renderer.geometry.edges));
    g_renderer.geometry.changes.update_vertices = TRUE;
    SETVEC3(g_renderer.geometry.bounds.min, FLT_MAX, FLT_MAX, FLT_MAX);
    SETVEC3(g_renderer.geometry.bounds.max, -FLT_MAX, -FLT_MAX, -FLT_MAX);
}

void SubmitNormal(vec3 normal) {
    g_renderer.geometry.changes.update_normals = TRUE;
    vec4 n = { 0 };
    glm_vec3_copy(normal, n);
    ARRLIST_vec4_add(&(g_renderer.geometry.normals), n);
}

void ClearNormals() {
    if (g_renderer.geometry.normals.maxsize == 0) return;
    ARRLIST_vec4_clear(&(g_renderer.geometry.normals));
    g_renderer.geometry.changes.update_normals = TRUE;

}

TriangleID SubmitTriangle(Triangle triangle) {
    EZ_ASSERT(triangle.a < g_renderer.geometry.vertices.size &&
              triangle.b < g_renderer.geometry.vertices.size &&
              triangle.c < g_renderer.geometry.vertices.size, "Triangle vertex does not exist");
    vec3 emission; 
    glm_vec3_copy(g_renderer.geometry.materials.data[triangle.material].emission, emission);
    if (emission[0] != 0 || emission[1] != 0 || emission[2] != 0) {
        ARRLIST_TriangleID_add(&(g_renderer.geometry.emissives), g_renderer.geometry.triangles.size);
        g_renderer.geometry.lightarea += TriangleArea(
            g_renderer.geometry.vertices.data[triangle.a],
            g_renderer.geometry.vertices.data[triangle.b],
            g_renderer.geometry.vertices.data[triangle.c]);
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
    ARRLIST_SceneLight_add(&(g_renderer.geometry.lights), light);
    char* b = EZ_ALLOC(MAX_LIGHT_NAME_SIZE + 1, sizeof(char));
    strncpy(b, name, MAX_LIGHT_NAME_SIZE);
    ARRLIST_DynamicString_add(&(g_renderer.geometry.lightnames), b);
    g_renderer.geometry.changes.update_lights = TRUE;
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
    g_renderer.geometry.changes.update_lights = TRUE;
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
    g_renderer.geometry.changes.update_materials = TRUE;
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
    g_renderer.geometry.changes.update_materials = TRUE;
}

void Render() {
    static BOOL async_update = TRUE;
    static size_t dupdate_queue = 0;
	size_t new_ind = (g_renderer.swapchain.index + 1) % CPUSWAP_LENGTH;
    BOOL resized_buffers = FALSE;
    BOOL is_transferring = FALSE;

    // helpers for readability
    #define TRANSFER_UPDATE(cname, sname, gname, gsname, oname, vname, tri) { \
        if (g_renderer.geometry.changes.update_##cname) { \
            g_renderer.geometry.changes.update_##cname = FALSE; \
            if (g_renderer.geometry.changes.max_##sname != gname) { \
                g_renderer.geometry.changes.max_##sname = gname; \
                if (!resized_buffers) { \
                    resized_buffers = TRUE; \
                    is_transferring = TRUE; \
                    VUTIL_BeginTransferCommands(); \
                    vkWaitForFences(g_renderer.vulkan.core.general.interface, 1, \
                            &g_renderer.vulkan.core.scheduler.syncro.fences[new_ind], \
                            VK_TRUE, UINT64_MAX); \
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
            if (tri) { \
                if (g_renderer.geometry.changes.max_emissives != g_renderer.geometry.emissives.maxsize) { \
                    g_renderer.geometry.changes.max_emissives = g_renderer.geometry.emissives.maxsize; \
                    if (!resized_buffers) { \
                        resized_buffers = TRUE; \
                        is_transferring = TRUE; \
                        VUTIL_BeginTransferCommands(); \
                        vkWaitForFences(g_renderer.vulkan.core.general.interface, 1, \
                                &g_renderer.vulkan.core.scheduler.syncro.fences[new_ind], \
                                VK_TRUE, UINT64_MAX); \
                    } \
                    VCLEAN_Emissives(&(g_renderer.vulkan.core.geometry.emissives)); \
                    VINIT_Emissives(&(g_renderer.vulkan.core.geometry.emissives)); \
                } else { \
                    if (g_renderer.geometry.changes.num_emissives != g_renderer.geometry.emissives.size) { \
                        dupdate_queue = CPUSWAP_LENGTH; \
                        g_renderer.geometry.changes.num_emissives = g_renderer.geometry.emissives.size; \
                    } \
                    if (!is_transferring) { is_transferring = TRUE; VUTIL_BeginTransferCommands(); } \
                    VUPDT_Emissives(&(g_renderer.vulkan.core.geometry.emissives)); \
                } \
            } \
        } \
    }

    // update render frame time;
    g_rft += GetFrameTime();

    // ARAP pose saving
    if (g_renderer.geometry.changes.update_triangles) SavePose();

    // detect changes in described data
    if (async_update) {
        // profile for stats
        BeginProfile(&(g_renderer.stats.profile));

        // recompute min/max
        if (g_renderer.geometry.changes.update_meshes || g_renderer.geometry.changes.update_vertices) {
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
        }

        // set bvh reconstruction
        if (g_renderer.geometry.changes.update_vertices ||
            g_renderer.geometry.changes.update_triangles ||
            g_renderer.geometry.changes.update_meshes)
            g_renderer.geometry.changes.update_bvh = CPUSWAP_LENGTH;

        // transfer updates
        TRANSFER_UPDATE(normals, normals, g_renderer.geometry.normals.maxsize, g_renderer.geometry.normals.size, normals, Normals, FALSE);
        TRANSFER_UPDATE(vertices, vertices, g_renderer.geometry.vertices.maxsize, g_renderer.geometry.vertices.size, vertices, Vertices, FALSE);
        TRANSFER_UPDATE(triangles, triangles, g_renderer.geometry.triangles.maxsize, g_renderer.geometry.triangles.size, triangles, Triangles, TRUE);
        TRANSFER_UPDATE(materials, materials, g_renderer.geometry.materials.maxsize, g_renderer.geometry.materials.size, materials, Materials, FALSE);
        TRANSFER_UPDATE(lights, lights, g_renderer.geometry.lights.maxsize, g_renderer.geometry.lights.size, lights, Lights, FALSE);
        TRANSFER_UPDATE(simulation, sim_size, SimSize(g_renderer.geometry.fluid), SimSize(g_renderer.geometry.fluid), fluid, Simulation, FALSE);
        TRANSFER_UPDATE(meshes, meshes, g_renderer.geometry.meshes.maxsize, g_renderer.geometry.meshes.size, transforms, Transforms, FALSE);

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
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        VkTimelineSemaphoreSubmitInfo tsi = { 0 };
        tsi.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        tsi.waitSemaphoreValueCount = 1;
        tsi.pWaitSemaphoreValues = &waitVal;
        VkSubmitInfo submitInfo = { 0 };
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = &tsi;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &g_renderer.vulkan.core.transfer.semaphore;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index]);
        submitInfo.signalSemaphoreCount = 0;
        VkResult result = vkQueueSubmit(g_renderer.vulkan.core.scheduler.queue, 1, &submitInfo, g_renderer.vulkan.core.scheduler.syncro.fences[g_renderer.swapchain.index]);
        EZ_ASSERT(result == VK_SUCCESS, "failed to submit draw command buffer!");
    }

    // wait for and reset rendering fence
    if (!g_renderer.config.async)
        vkWaitForFences(g_renderer.vulkan.core.general.interface, 1, &(g_renderer.vulkan.core.scheduler.syncro.fences[new_ind]), VK_TRUE, UINT64_MAX);
    if (vkGetFenceStatus(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.scheduler.syncro.fences[new_ind]) == VK_SUCCESS) {
        // copy overlay results to host
        memcpy((void*)ExposedOverlaySSBO(), g_renderer.vulkan.core.context.renderdata.overlay_mapped, sizeof(OverlaySSBO));

        // reset fences and update swapchain index
        vkResetFences(g_renderer.vulkan.core.general.interface, 1, &(g_renderer.vulkan.core.scheduler.syncro.fences[new_ind]));
        g_renderer.swapchain.index = new_ind;
        async_update = TRUE;

        // update render target
        glBindTexture(GL_TEXTURE_2D, g_renderer.swapchain.target[new_ind].texture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_renderer.dimensions.x, g_renderer.dimensions.y, GL_RGBA, GL_UNSIGNED_BYTE, g_renderer.swapchain.reference);
        glBindTexture(GL_TEXTURE_2D, 0);

        // end profiling
        EndProfile(&(g_renderer.stats.profile));
    } else {
        async_update = FALSE;
    }

    #undef TRANSFER_UPDATE
}

void DrawHelper(float x, float y, float w, float h, float maxw, float maxh) {
    ClearBackground(BLACK);
    BeginBlendMode(BLEND_ADDITIVE);
	float psuedo_w = w * (g_renderer.dimensions.x / maxw);
	float psuedo_h = h * (g_renderer.dimensions.y / maxh);
    if (g_renderer.config.flags & PATHTRACE_SHADER_FLAG) {
        for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
            DrawTexturePro(
                g_renderer.swapchain.target[i].texture,
                (Rectangle){
                    (g_renderer.swapchain.target[i].texture.width / 2.0f) - (psuedo_w/2.0f),
                    (g_renderer.swapchain.target[i].texture.height / 2.0f) - (psuedo_h/2.0f),
                    psuedo_w,
                    psuedo_h },
                (Rectangle){ x, y, w, h},
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
    return ProfileResult(&(g_renderer.stats.profile));
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

size_t NumEmissives() {
    return g_renderer.geometry.emissives.size;
}

void UpdateNormals() {
    g_renderer.geometry.changes.update_normals = TRUE;
}

void UpdateVertices() {
    g_renderer.geometry.changes.update_vertices = TRUE;
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
    return &(g_renderer.config);
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
        EZ_ERROR("Mesh was not detected to be a valid manifold");
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
        EZ_ERROR("Mesh was not detected to be a valid manifold");
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
        EZ_ERROR("Mesh was not detected to be a valid manifold");
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
        EZ_ERROR("Mesh was not detected to be a valid manifold");
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
    inline BOOL isunlocked(size_t i) { return g_renderer.geometry.arap.v2f[i] != (size_t)-1; }
    inline void outer(vec3 a, vec3 b, mat3 result) {
        for (int col = 0; col < 3; col++) for (int row = 0; row < 3; row++) result[col][row] = a[row] * b[col];
    }
    for (size_t i = 0; i < g_renderer.geometry.vertices.size; i++) {
        if (isunlocked(i)) glm_vec3_copy(g_renderer.geometry.arap.originals[i], g_renderer.geometry.vertices.data[i]);
        if (g_renderer.config.arap.style == 1) {
            glm_vec3_copy(g_renderer.geometry.arap.normals[i], g_renderer.geometry.arap.z[i]);
            glm_vec3_zero(g_renderer.geometry.arap.u[i]);
        }
    }
    for (size_t i = 0; i < g_renderer.config.arap.iterations; i++) {
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
            if (g_renderer.config.arap.style == 1) {
                vec3* z = &g_renderer.geometry.arap.z[j];
                vec3* u = &g_renderer.geometry.arap.u[j];
                vec3* n = &g_renderer.geometry.arap.normals[j];
                float a = g_renderer.geometry.arap.areas[j];
                float lambda = g_renderer.config.arap.cube_lambda;
                float rho = g_renderer.config.arap.cube_rho;
                for (size_t admm = 0; admm < g_renderer.config.arap.addm; admm++) {
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

void SaveRender(const char* filepath) {
	RenderTexture rt = LoadRenderTexture(g_renderer.dimensions.x, g_renderer.dimensions.y);
    BeginTextureMode(rt);
    DrawHelper(0, 0, g_renderer.dimensions.x, -g_renderer.dimensions.y, g_renderer.dimensions.x, g_renderer.dimensions.y);
    EndTextureMode();
    Image image = LoadImageFromTexture(rt.texture);
    ExportImage(image, filepath);
    UnloadImage(image);
    UnloadRenderTexture(rt);
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
