#include "dev.h"

#ifndef PROD_BUILD

#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "ui/panels/edit.h"
#include "renderer/rmath.h"
#include <data/input.h>
#include <core/binds.h>

#define TEST_SAMPLES 100

typedef enum {
    WAX_STAGE = 0,
    FLESH_STAGE,
    JADE_STAGE
} MaterialStage;

typedef enum {
    SCENEL_STAGE = 0,
    EMISSION_STAGE,
    BOX_STAGE
} EnvironmentStage;

typedef enum {
    GROUND_STAGE = 0,
    MINIMAL_STAGE,
    STANDARD_STAGE,
    QUALITY_STAGE
} QualityStage;

static MaterialStage g_mstage = WAX_STAGE;
static EnvironmentStage g_estage = SCENEL_STAGE;
static QualityStage g_qstage = GROUND_STAGE;
static BOOL g_testing = FALSE;
static BOOL g_training = FALSE;

static void ChangeToWax() {
    SurfaceMaterial* m = MaterialReference(0);
    SETVEC3(m->emission, 0, 0, 0);
    SETVEC3(m->ambient, 0, 0, 0);
    SETVEC3(m->diffuse, 0, 0, 0);
    SETVEC3(m->specular, 0, 0, 0);
    SETVEC3(m->absorbtion, 0.5, 1.5, 4.0);
    SETVEC3(m->dispersion, 0, 0, 0);
    SETVEC3(m->scattering, 50, 45, 45);
    m->anisotropy = 0;
    m->scale = 1.0f;
    m->subsurface = 1.0f;
    m->ior = 1.0f;
    m->shiny = 20.0f;
    m->model = SUBSURFACE_MODEL;
    UpdateMaterials();
}

static void ChangeToFlesh() {
    SurfaceMaterial* m = MaterialReference(0);
    SETVEC3(m->emission, 0, 0, 0);
    SETVEC3(m->ambient, 0, 0, 0);
    SETVEC3(m->diffuse, 0.9, 0.85, 0.82);
    SETVEC3(m->specular, 0, 0, 0);
    SETVEC3(m->absorbtion, 0.25, 3.0, 6.0);
    SETVEC3(m->dispersion, 0, 0, 0);
    SETVEC3(m->scattering, 25, 45, 60);
    m->anisotropy = 0.2;
    m->scale = 1.0f;
    m->subsurface = 0.7f;
    m->ior = 1.0f;
    m->shiny = 20.0f;
    m->model = SUBSURFACE_MODEL;
    UpdateMaterials();
}

static void ChangeToJade() {
    SurfaceMaterial* m = MaterialReference(0);
    SETVEC3(m->emission, 0, 0, 0);
    SETVEC3(m->ambient, 0, 0, 0);
    SETVEC3(m->diffuse, 0.05, 0.15, 0.08);
    SETVEC3(m->specular, 0.9, 0.9, 0.9);
    SETVEC3(m->absorbtion, 3.0, 0.4, 2.0);
    SETVEC3(m->dispersion, 0, 0, 0);
    SETVEC3(m->scattering, 40, 40, 40);
    m->anisotropy = 0.3;
    m->scale = 1.0f;
    m->subsurface = 0.9f;
    m->ior = 1.0f;
    m->shiny = 300.0f;
    m->model = SUBSURFACE_MODEL;
    UpdateMaterials();
}

static void CreateBoxLight(float x, float y, float z) {
    SurfaceMaterial light = { 0 };
    light.emission[0] = 10.0f;
    light.emission[1] = 10.0f;
    light.emission[2] = 10.0f;
    SubmitNamedMaterial(light, "Light");
    vec3 position = { x, y, z };
    vec3 scale = { 0.5, 0.5, 0.5 };
    size_t vertex_base = NumVertices();
    size_t triangle_base = NumTriangles();
    vec3 v1 = {
        position[0] - scale[0]/2.0f,
        position[1] - scale[1]/2.0f,
        position[2] - scale[2]/2.0f};
    vec3 v2 = {
        position[0] - scale[0]/2.0f,
        position[1] + scale[1]/2.0f,
        position[2] - scale[2]/2.0f};
    vec3 v3 = {
        position[0] + scale[0]/2.0f,
        position[1] + scale[1]/2.0f,
        position[2] - scale[2]/2.0f};
    vec3 v4 = {
        position[0] + scale[0]/2.0f,
        position[1] - scale[1]/2.0f,
        position[2] - scale[2]/2.0f};
    vec3 v5 = {
        position[0] - scale[0]/2.0f,
        position[1] - scale[1]/2.0f,
        position[2] + scale[2]/2.0f};
    vec3 v6 = {
        position[0] - scale[0]/2.0f,
        position[1] + scale[1]/2.0f,
        position[2] + scale[2]/2.0f};
    vec3 v7 = {
        position[0] + scale[0]/2.0f,
        position[1] + scale[1]/2.0f,
        position[2] + scale[2]/2.0f};
    vec3 v8 = {
        position[0] + scale[0]/2.0f,
        position[1] - scale[1]/2.0f,
        position[2] + scale[2]/2.0f};
    SubmitVertex(v1);
    SubmitVertex(v2);
    SubmitVertex(v3);
    SubmitVertex(v4);
    SubmitVertex(v5);
    SubmitVertex(v6);
    SubmitVertex(v7);
    SubmitVertex(v8);
    SubmitTriangle((Triangle){ vertex_base + 3, vertex_base + 1, vertex_base + 0, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    SubmitTriangle((Triangle){ vertex_base + 3, vertex_base + 2, vertex_base + 1, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    SubmitTriangle((Triangle){ vertex_base + 2, vertex_base + 5, vertex_base + 1, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    SubmitTriangle((Triangle){ vertex_base + 2, vertex_base + 6, vertex_base + 5, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    SubmitTriangle((Triangle){ vertex_base + 7, vertex_base + 2, vertex_base + 3, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    SubmitTriangle((Triangle){ vertex_base + 7, vertex_base + 6, vertex_base + 2, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    SubmitTriangle((Triangle){ vertex_base + 3, vertex_base + 0, vertex_base + 4, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    SubmitTriangle((Triangle){ vertex_base + 4, vertex_base + 7, vertex_base + 3, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    SubmitTriangle((Triangle){ vertex_base + 5, vertex_base + 6, vertex_base + 7, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    SubmitTriangle((Triangle){ vertex_base + 7, vertex_base + 4, vertex_base + 5, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    SubmitTriangle((Triangle){ vertex_base + 0, vertex_base + 1, vertex_base + 5, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    SubmitTriangle((Triangle){ vertex_base + 5, vertex_base + 4, vertex_base + 0, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 1 });
    vec3 extent;
    glm_vec3_scale(scale, 0.5f, extent);
    SubmitMeshDescriptor((MeshDescriptor){
        FALSE, vertex_base, NumVertices() - 1, triangle_base, NumTriangles() - 1, 0, (uint32_t)-1,
        (uint32_t)NumSplats(), (uint32_t)NumSplats() - 1, { 0 }, INLINEV3(extent), { 0 }, { 0 },
        { 1.0f, 1.0f, 1.0f }, GLM_MAT4_IDENTITY_INIT }, "Cube Light");
}

static void LoadBox() {
    LoadOBJ("assets/models/OBJ/dressed/CornellBox-Sphere.obj");
    FitCamera();
}

static void ResetCamera() {
    SimpleCamera camera = RendererCamera()->core;
    SETVEC3(camera.position, 0.0f, 2.133f, 2.11f);
    SETVEC3(camera.look, 0.0f, 0.0f, 0.0f);
    SETVEC3(camera.up, 0.0f, 1.0f, 0.0f);
    camera.fov = 90.0f;
    camera.orthosize = 4.0f;
    camera.aperature = 0.0f;
    camera.focus = 0.0f;
    RendererCamera()->core = camera;
}

static void SetVoidCamera() {
    ResetCamera();
    RendererCamera()->core.position[1] = 1.0f;
    RendererCamera()->core.position[2] = 3.0f;
    RendererCamera()->config.reset = TRUE;
    FitCamera();
}

static void StartTestSuite() {
    g_mstage = 0;
    g_estage = 0;
    g_qstage = 0;
    g_testing = TRUE;
    ClearScene(FALSE);
    SubmitNamedLight((SceneLight){{0,1,1},{5,5,5},{0,0,0},0,0}, "Rabbit Point Light");
    LoadOBJ("assets/models/OBJ/naked/bunny.obj");
    ChangeToWax();
    SetVoidCamera();
    RendererCamera()->config.denoise = FALSE;
    RendererCamera()->config.flags = PATHTRACE_PIPELINE_FLAGS;
    RendererCamera()->config.reset = TRUE;
    RendererCamera()->config.gsssenabled = FALSE;
    // test matrix:
    // wax   ->  flesh ->  jade
    // |
    // scenelighting in void
    // |
    // emissive in cornell box
    //
    // .. ground truth .. minimal preset .. normal preset .. extreme preset
    // log stats, mean squared diff, etc!
}

static void TestSuite() {
    if (!g_training && g_testing && GetRenderStats().accumulation > TEST_SAMPLES) {
        if (g_mstage == WAX_STAGE) {
            if (g_estage == SCENEL_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    SaveRender("000_wax_scenel_ground.png");
                    g_training = TRUE;
                    GSSSConfig cfg = SubsurfaceDefaultConfig();
                    cfg.surface_gaussians = 512;
                    cfg.lobes = 6;
                    cfg.internal_gaussians = 512;
                    cfg.walks = (uint32_t)s_walks;
                    SubsurfaceTrainStart(0, &cfg);
                    // train minimal 
                    // enable clouds
                    //
                } else if (g_qstage == MINIMAL_STAGE) {
                    SaveRender("000_wax_scenel_minimal.png");
                } else if (g_qstage == STANDARD_STAGE) {
                    SaveRender("000_wax_scenel_standard.png");
                } else {
                    SaveRender("000_wax_scenel_quality.png");
                }
            } else if (g_estage == EMISSION_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    SaveRender("000_wax_emission_ground.png");
                } else if (g_qstage == MINIMAL_STAGE) {
                    SaveRender("000_wax_emission_minimal.png");
                } else if (g_qstage == STANDARD_STAGE) {
                    SaveRender("000_wax_emission_standard.png");
                } else {
                    SaveRender("000_wax_emission_quality.png");
                }
            } else {
                if (g_qstage == GROUND_STAGE) {
                    SaveRender("000_wax_box_ground.png");
                } else if (g_qstage == MINIMAL_STAGE) {
                    SaveRender("000_wax_box_minimal.png");
                } else if (g_qstage == STANDARD_STAGE) {
                    SaveRender("000_wax_box_standard.png");
                } else {
                    SaveRender("000_wax_box_quality.png");
                }
            }
        } else if (g_mstage == FLESH_STAGE) {
            if (g_estage == SCENEL_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    SaveRender("000_flesh_scenel_ground.png");
                } else if (g_qstage == MINIMAL_STAGE) {
                    SaveRender("000_flesh_scenel_minimal.png");
                } else if (g_qstage == STANDARD_STAGE) {
                    SaveRender("000_flesh_scenel_standard.png");
                } else {
                    SaveRender("000_flesh_scenel_quality.png");
                }
            } else if (g_estage == EMISSION_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    SaveRender("000_flesh_emission_ground.png");
                } else if (g_qstage == MINIMAL_STAGE) {
                    SaveRender("000_flesh_emission_minimal.png");
                } else if (g_qstage == STANDARD_STAGE) {
                    SaveRender("000_flesh_emission_standard.png");
                } else {
                    SaveRender("000_flesh_emission_quality.png");
                }
            } else {
                if (g_qstage == GROUND_STAGE) {
                    SaveRender("000_flesh_box_ground.png");
                } else if (g_qstage == MINIMAL_STAGE) {
                    SaveRender("000_flesh_box_minimal.png");
                } else if (g_qstage == STANDARD_STAGE) {
                    SaveRender("000_flesh_box_standard.png");
                } else {
                    SaveRender("000_flesh_box_quality.png");
                }
            }
        } else {
            if (g_estage == SCENEL_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    SaveRender("000_jade_scenel_ground.png");
                } else if (g_qstage == MINIMAL_STAGE) {
                    SaveRender("000_jade_scenel_minimal.png");
                } else if (g_qstage == STANDARD_STAGE) {
                    SaveRender("000_jade_scenel_standard.png");
                } else {
                    SaveRender("000_jade_scenel_quality.png");
                }
            } else if (g_estage == EMISSION_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    SaveRender("000_jade_emission_ground.png");
                } else if (g_qstage == MINIMAL_STAGE) {
                    SaveRender("000_jade_emission_minimal.png");
                } else if (g_qstage == STANDARD_STAGE) {
                    SaveRender("000_jade_emission_standard.png");
                } else {
                    SaveRender("000_jade_emission_quality.png");
                }
            } else {
                if (g_qstage == GROUND_STAGE) {
                    SaveRender("000_jade_box_ground.png");
                } else if (g_qstage == MINIMAL_STAGE) {
                    SaveRender("000_jade_box_minimal.png");
                } else if (g_qstage == STANDARD_STAGE) {
                    SaveRender("000_jade_box_standard.png");
                } else {
                    SaveRender("000_jade_box_quality.png");
                    g_testing = FALSE;
                }
            }
        }
        RendererCamera()->config.reset = TRUE;
    }
}

static void Screenshot() {
    SaveViewportRender("screenshot.png");
}

static void ClearSoftScene() {
    ClearScene(FALSE);
}

void DevInitialize() {
    AddBind("load cornell box", LoadBox,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_L_OVERRIDE, BIND_KEY_DOWN },
        (BindCommand){ IK_B_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("run test suite", StartTestSuite,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_L_OVERRIDE, BIND_KEY_DOWN },
        (BindCommand){ IK_A_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("clear scene", ClearSoftScene,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_C_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("screenshot", Screenshot,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_S_OVERRIDE, BIND_KEY_PRESSED });
}

void DevUpdate() { TestSuite(); }

#else

void DevInitialize() {}

void DevUpdate() {}

#endif
