#include "renderer.h"
#include <easylogger.h>
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/vulkan/vupdate.h"
#include "renderer/vulkan/vclean.h"
#include "renderer/rmath.h"
#include "renderer/overlay.h"
#include "renderer/processor.h"
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
        EZ_FREE(g_renderer.geometry.arap.v2f);
        EZ_FREE(g_renderer.geometry.arap.f2v);
        EZ_FREE(g_renderer.geometry.arap.b);
        cholmod_free_sparse(&g_renderer.geometry.arap.A, &g_cholmod);
        cholmod_free_factor(&g_renderer.geometry.arap.L, &g_cholmod);
        EZ_FREE(g_renderer.geometry.arap.Ai_back);
    }
    g_renderer.geometry.arap.values = NULL;
    g_renderer.geometry.arap.cindices = NULL;
    g_renderer.geometry.arap.rpointers = NULL;
    g_renderer.geometry.arap.rcounts = NULL;
    g_renderer.geometry.arap.cursor = NULL;
    g_renderer.geometry.arap.diag = NULL;
    g_renderer.geometry.arap.originals = NULL;
    g_renderer.geometry.arap.rotations = NULL;
    g_renderer.geometry.arap.v2f = NULL;
    g_renderer.geometry.arap.f2v = NULL;
    g_renderer.geometry.arap.b = NULL;
    g_renderer.geometry.arap.rows = 0;
    g_renderer.geometry.arap.nnz = 0;
    g_renderer.geometry.arap.max_nnz = 0;
    g_renderer.geometry.arap.max_rows = 0;
    g_renderer.geometry.arap.A = NULL;
    g_renderer.geometry.arap.L = NULL;
    g_renderer.geometry.arap.Ai_back = NULL;
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
    g_renderer.geometry.arap.nnz = nnz;
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
        g_renderer.geometry.arap.max_rows = nrows;
    }
    memcpy(g_renderer.geometry.arap.originals, g_renderer.geometry.vertices.data, g_renderer.geometry.vertices.size * sizeof(vec4));
    g_renderer.geometry.arap.rows = 0;
    memset(g_renderer.geometry.arap.rcounts, 0, (nrows - 1)*sizeof(size_t));
    for (size_t i = 0; i < g_renderer.geometry.vertices.size; i++) {
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
        g_renderer.geometry.arap.diag[i] = 0.0;
    }
    for (size_t i = 0; i < g_renderer.geometry.edges.size; i++) {
        Edge e = g_renderer.geometry.edges.data[i];
        double w = EdgeWeight(e);
        if (isunlocked(e.a) && isunlocked(e.b)) {
            size_t fa = g_renderer.geometry.arap.v2f[e.a];
            size_t fb = g_renderer.geometry.arap.v2f[e.b];
            size_t i_index = g_renderer.geometry.arap.cursor[fa]++;
            size_t j_index = g_renderer.geometry.arap.cursor[fb]++;
            g_renderer.geometry.arap.cindices[i_index] = fb;
            g_renderer.geometry.arap.values[i_index] = -w;
            g_renderer.geometry.arap.cindices[j_index] = fa;
            g_renderer.geometry.arap.values[j_index] = -w;
            g_renderer.geometry.arap.diag[fa] += w;
            g_renderer.geometry.arap.diag[fb] += w;
        } else if (isunlocked(e.a)) {
            size_t fa = g_renderer.geometry.arap.v2f[e.a];
            g_renderer.geometry.arap.diag[fa] += w;
        } else if (isunlocked(e.b)) {
            size_t fb = g_renderer.geometry.arap.v2f[e.b];
            g_renderer.geometry.arap.diag[fb] += w;
        }
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
    g_renderer.config.normals = TRUE;
    g_renderer.config.flags = PREVIEW_PIPELINE_FLAGS;

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
    if (g_renderer.geometry.bounds.min[0] >= g_renderer.geometry.bounds.max[0]) return;
    vec3 l2p;
    glm_vec3_sub(g_renderer.camera.position, g_renderer.camera.look, l2p);
    glm_vec3_normalize(l2p);
    vec3 extend, min2o, newo;
    glm_vec3_sub(g_renderer.geometry.bounds.max, g_renderer.geometry.bounds.min, extend);
    glm_vec3_scale(extend, 0.5f, min2o);
    glm_vec3_add(min2o, g_renderer.geometry.bounds.min, newo);
    float width = glm_vec3_norm(extend);
    SETVEC(g_renderer.camera.look, newo);
    glm_vec3_scale(l2p, width, l2p);
    glm_vec3_add(l2p, newo, g_renderer.camera.position);
}

void ReorientCamera() {
    for (int i = 0; i < 3; i++) {
        float sign = g_renderer.camera.up[i] > 0 ? 1.0f : (g_renderer.camera.up[i] < 0 ? -1.0f : 0.0f);
        g_renderer.camera.look[i] += sign*1e-6f;
    }
    vec3 desired = { 0, 1, 0 };
    SETVEC(g_renderer.camera.up, desired);
}

void GetVertex(size_t index, vec3 out) {
    EZ_ASSERT(index < g_renderer.geometry.vertices.size, "Vertex does not exist for requested index");
    SETVEC(out, g_renderer.geometry.vertices.data[index]);
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
    SETVEC(v, vertex);
    ARRLIST_vec4_add(&(g_renderer.geometry.vertices), v);
    if (vertex[0] < g_renderer.geometry.bounds.min[0]) g_renderer.geometry.bounds.min[0] = vertex[0];
    if (vertex[1] < g_renderer.geometry.bounds.min[1]) g_renderer.geometry.bounds.min[1] = vertex[1];
    if (vertex[2] < g_renderer.geometry.bounds.min[2]) g_renderer.geometry.bounds.min[2] = vertex[2];
    if (vertex[0] > g_renderer.geometry.bounds.max[0]) g_renderer.geometry.bounds.max[0] = vertex[0];
    if (vertex[1] > g_renderer.geometry.bounds.max[1]) g_renderer.geometry.bounds.max[1] = vertex[1];
    if (vertex[2] > g_renderer.geometry.bounds.max[2]) g_renderer.geometry.bounds.max[2] = vertex[2];
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
    SETVEC(n, normal);
    ARRLIST_vec4_add(&(g_renderer.geometry.normals), n);
}

void ClearNormals() {
    if (g_renderer.geometry.normals.maxsize == 0) return;
    ARRLIST_vec4_clear(&(g_renderer.geometry.normals));
    g_renderer.geometry.changes.update_normals = TRUE;

}

TriangleID SubmitTriangle(Triangle triangle) {
    g_renderer.geometry.changes.update_triangles = TRUE;
    EZ_ASSERT(triangle.a < g_renderer.geometry.vertices.size &&
              triangle.b < g_renderer.geometry.vertices.size &&
              triangle.c < g_renderer.geometry.vertices.size, "Triangle vertex does not exist");
    vec3 emission; SETVEC(emission, g_renderer.geometry.materials.data[triangle.material].emission);
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
    return id;
}

void ClearTriangles() {
    if (g_renderer.geometry.triangles.maxsize == 0) return;
    ARRLIST_Triangle_clear(&(g_renderer.geometry.triangles));
    ARRLIST_TriangleID_clear(&(g_renderer.geometry.emissives));
    HASHMAP_EdgeGlue_clear(&(g_renderer.geometry.glue));
    g_renderer.geometry.changes.update_triangles = TRUE;
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

    // update render frame time;
    g_rft += GetFrameTime();

    // detect changes in described data
    if (async_update) {
        // profile for stats
        BeginProfile(&(g_renderer.stats.profile));

        BOOL descriptor_changes = 
            g_renderer.geometry.changes.update_triangles |
            g_renderer.geometry.changes.update_materials |
            g_renderer.geometry.changes.update_lights |
            g_renderer.geometry.changes.update_vertices |
            g_renderer.geometry.changes.update_normals;

        // set bvh reconstruction
        if (g_renderer.geometry.changes.update_vertices || g_renderer.geometry.changes.update_triangles)
            g_renderer.geometry.changes.update_bvh = CPUSWAP_LENGTH;

        // update normals if needed
        if (g_renderer.geometry.changes.update_normals) {
            g_renderer.geometry.changes.update_normals = FALSE;
            if (g_renderer.geometry.changes.max_normals != g_renderer.geometry.normals.maxsize) {
                vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);
                g_renderer.geometry.changes.max_normals = g_renderer.geometry.normals.maxsize;
                VCLEAN_Normals(&(g_renderer.vulkan.core.geometry.normals));
                VINIT_Normals(&(g_renderer.vulkan.core.geometry.normals));
            } else {
                VUPDT_Normals(&(g_renderer.vulkan.core.geometry.normals));
            }
        }

        // update vertices if needed
        if (g_renderer.geometry.changes.update_vertices) {
            g_renderer.geometry.changes.update_vertices = FALSE;
            if (g_renderer.geometry.changes.max_vertices != g_renderer.geometry.vertices.maxsize) {
                vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);
                g_renderer.geometry.changes.max_vertices = g_renderer.geometry.vertices.maxsize;
                VCLEAN_Vertices(&(g_renderer.vulkan.core.geometry.vertices));
                VINIT_Vertices(&(g_renderer.vulkan.core.geometry.vertices));
            } else {
                VUPDT_Vertices(&(g_renderer.vulkan.core.geometry.vertices));
            }
        }

        // update triangles if needed
        if (g_renderer.geometry.changes.update_triangles) {
            g_renderer.geometry.changes.update_triangles = FALSE;
            SavePose();
            if (g_renderer.geometry.changes.max_triangles != g_renderer.geometry.triangles.maxsize) {
                vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);
                g_renderer.geometry.changes.max_triangles = g_renderer.geometry.triangles.maxsize;
                VCLEAN_Triangles(&(g_renderer.vulkan.core.geometry.triangles));
                VINIT_Triangles(&(g_renderer.vulkan.core.geometry.triangles));
                VCLEAN_BVH(&(g_renderer.vulkan.core.geometry.bvh));
                VINIT_BVH(&(g_renderer.vulkan.core.geometry.bvh));
            } else {
                VUPDT_Triangles(&(g_renderer.vulkan.core.geometry.triangles));
            }
            if (g_renderer.geometry.changes.max_emissives != g_renderer.geometry.emissives.maxsize) {
                vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);
                g_renderer.geometry.changes.max_emissives = g_renderer.geometry.emissives.maxsize;
                VCLEAN_Emissives(&(g_renderer.vulkan.core.geometry.emissives));
                VINIT_Emissives(&(g_renderer.vulkan.core.geometry.emissives));
            } else {
                VUPDT_Emissives(&(g_renderer.vulkan.core.geometry.emissives));
            }
        }

        // update materials if needed
        if (g_renderer.geometry.changes.update_materials) {
            g_renderer.geometry.changes.update_materials = FALSE;
            if (g_renderer.geometry.changes.max_materials != g_renderer.geometry.materials.maxsize) {
                vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);
                g_renderer.geometry.changes.max_materials = g_renderer.geometry.materials.maxsize;
                VCLEAN_Materials(&(g_renderer.vulkan.core.geometry.materials));
                VINIT_Materials(&(g_renderer.vulkan.core.geometry.materials));
            } else {
                VUPDT_Materials(&(g_renderer.vulkan.core.geometry.materials));
            }
        }

        // update lights if needed
        if (g_renderer.geometry.changes.update_lights) {
            g_renderer.geometry.changes.update_lights = FALSE;
            if (g_renderer.geometry.changes.max_lights != g_renderer.geometry.lights.maxsize) {
                vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);
                g_renderer.geometry.changes.max_lights = g_renderer.geometry.lights.maxsize;
                VCLEAN_Lights(&(g_renderer.vulkan.core.geometry.lights));
                VINIT_Lights(&(g_renderer.vulkan.core.geometry.lights));
            } else {
                VUPDT_Lights(&(g_renderer.vulkan.core.geometry.lights));
            }
        }

        // update descriptor sets if needed
        if (descriptor_changes) VUPDT_DescriptorSets(g_renderer.vulkan.core.context.renderdata.descriptors);

        // update uniform buffers
        VUPDT_UniformBuffers(&(g_renderer.vulkan.core.context.renderdata.ubos));

        // reset renderer frame time
        g_rft = 0.0f;

        // reset command buffer and record it
        vkResetCommandBuffer(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index], 0);
        VUPDT_RecordCommand(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index]);

        // submit command buffer
        VkSubmitInfo submitInfo = { 0 };
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 0;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index]);
        submitInfo.signalSemaphoreCount = 0;
        VkResult result = vkQueueSubmit(g_renderer.vulkan.core.scheduler.queue, 1, &submitInfo, g_renderer.vulkan.core.scheduler.syncro.fences[g_renderer.swapchain.index]);
        EZ_ASSERT(result == VK_SUCCESS, "failed to submit draw command buffer!");
    }

    // wait for and reset rendering fence
	size_t new_ind = (g_renderer.swapchain.index + 1) % CPUSWAP_LENGTH;
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
        result[0][0] = a[0] * b[0];
        result[0][1] = a[0] * b[1];
        result[0][2] = a[0] * b[2];
        result[1][0] = a[1] * b[0];
        result[1][1] = a[1] * b[1];
        result[1][2] = a[1] * b[2];
        result[2][0] = a[2] * b[0];
        result[2][1] = a[2] * b[1];
        result[2][2] = a[2] * b[2];
    }
    for (size_t i = 0; i < 20; i++) {
        // compute rotations
        for (size_t j = 0; j < g_renderer.geometry.vertices.size; j++) {
            glm_mat3_zero(g_renderer.geometry.arap.rotations[j]);
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
            if (isunlocked(e.a)) Mat3Add(C, g_renderer.geometry.arap.rotations[e.a], g_renderer.geometry.arap.rotations[e.a]);
            if (isunlocked(e.b)) Mat3Add(C, g_renderer.geometry.arap.rotations[e.b], g_renderer.geometry.arap.rotations[e.b]);
        }
        for (size_t j = 0; j < g_renderer.geometry.vertices.size; j++) {
            if (isunlocked(j)) {
                mat3 R;
                PolarDecompose(g_renderer.geometry.arap.rotations[j], R);
                glm_mat3_copy(R, g_renderer.geometry.arap.rotations[j]);
            } else {
                glm_mat3_identity(g_renderer.geometry.arap.rotations[j]);
            }
        }

        // build RHS
        for (size_t j = 0; j < g_renderer.geometry.edges.size; j++) {
            Edge e = g_renderer.geometry.edges.data[j];
            EdgeMeta em = HASHMAP_EdgeGlue_get(&(g_renderer.geometry.glue), e);
            vec3 ti, ri_pij, rj_pij;
            if (isunlocked(e.a)) glm_mat3_mulv(g_renderer.geometry.arap.rotations[e.a], em.pij, ri_pij);
            else glm_vec3_zero(ri_pij);
            if (isunlocked(e.b)) glm_mat3_mulv(g_renderer.geometry.arap.rotations[e.b], em.pij, rj_pij);
            else glm_vec3_zero(rj_pij);
            glm_vec3_add(ri_pij, rj_pij, ti);
            glm_vec3_scale(ti, 0.5f * em.weight, ti);
            if (isunlocked(e.a)) glm_vec3_add(g_renderer.geometry.arap.b[e.a], ti, g_renderer.geometry.arap.b[e.a]);
            glm_vec3_scale(ti, -1.0f, ti);
            if (isunlocked(e.b)) glm_vec3_add(g_renderer.geometry.arap.b[e.b], ti, g_renderer.geometry.arap.b[e.b]);
            if (!isunlocked(e.a) && isunlocked(e.b)) {
                vec3 locked_contrib;
                glm_vec3_scale((float*)g_renderer.geometry.vertices.data[e.a], em.weight, locked_contrib);
                glm_vec3_add(g_renderer.geometry.arap.b[e.b], locked_contrib, g_renderer.geometry.arap.b[e.b]);
            } else if (isunlocked(e.a) && !isunlocked(e.b)) {
                vec3 locked_contrib;
                glm_vec3_scale((float*)g_renderer.geometry.vertices.data[e.b], em.weight, locked_contrib);
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
