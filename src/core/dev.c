#include "dev.h"

#ifndef PROD_BUILD

#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "ui/panels/edit.h"
#include "renderer/rmath.h"
#include "renderer/subsurface.h"
#include <util/logger.h>
#include <data/input.h>
#include <core/binds.h>

#define TEST_SAMPLES 2000
#define MINIMAL_WALKS 300000
#define STANDARD_WALKS 600000
#define QUALITY_WALKS 900000
#define MINIMAL_SURFACE 64
#define STANDARD_SURFACE 128
#define QUALITY_SURFACE 256
#define MINIMAL_SPLATS 512
#define STANDARD_SPLATS 1024
#define QUALITY_SPLATS 2048

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
static float g_timer = 0.0f;

static void ChangeAllToDefault() {
    for (size_t i = 0; i < NumTriangles(); i++) TriangleReference(i)->material = 0;
    UpdateTriangles();
}

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
    ResetAccumulation();
}

static void ChangeToFlesh() {
    SurfaceMaterial* m = MaterialReference(0);
    SETVEC3(m->emission, 0, 0, 0);
    SETVEC3(m->ambient, 0, 0, 0);
    SETVEC3(m->diffuse, 0.9, 0.55, 0.32);
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
    ResetAccumulation();
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
    ResetAccumulation();
}

static void CreateBoxLight(float x, float y, float z) {
    SurfaceMaterial light = { 0 };
    light.emission[0] = 20.0f;
    light.emission[1] = 20.0f;
    light.emission[2] = 20.0f;
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

static void SetBoxCamera() {
    ResetCamera();
    RendererCamera()->core.position[0] = 0.42;
    RendererCamera()->core.position[1] = 1.03;
    RendererCamera()->core.position[2] = 1.11;
    RendererCamera()->core.look[0] = 0.18;
    RendererCamera()->core.look[1] = 0.57;
    RendererCamera()->core.look[2] = -0.1;
}

static void StartTestSuite() {
    g_mstage = JADE_STAGE;
    g_estage = EMISSION_STAGE;
    g_qstage = QUALITY_STAGE;
    g_testing = TRUE;
    ClearScene(FALSE);
    SubmitNamedLight((SceneLight){{0,1,1},{5,5,5},{0,0,0},0,0}, "Rabbit Point Light");
    LoadOBJ("assets/models/OBJ/naked/bunny.obj");
    ChangeToWax();
    SetVoidCamera();
    RendererCamera()->config.denoise = FALSE;
    RendererCamera()->config.direct = TRUE;
    RendererCamera()->config.flags = PATHTRACE_PIPELINE_FLAGS;
    RendererCamera()->config.gsssenabled = FALSE;
    ResetAccumulation();
    g_timer = 0.0f;
}

static void StartTraining(size_t s, size_t l, size_t i, size_t w) {
    ResetAccumulation();
    RendererCamera()->config.flags = PREVIEW_PIPELINE_FLAGS;
    g_training = TRUE;
    GSSSConfig cfg = SubsurfaceDefaultConfig();
    cfg.surface_gaussians = s;
    cfg.lobes = l;
    cfg.internal_gaussians = i;
    cfg.walks = w;
    SubsurfaceTrainStart(0, &cfg);
}

static void TestSuite() {
    g_timer += GetFrameTime();
    if (g_training && SubsurfaceTrainPhaseNow() == SUBSURFACE_DONE) {
        g_training = FALSE;
        RendererCamera()->config.flags = PATHTRACE_PIPELINE_FLAGS;
        RendererCamera()->config.gsssenabled = TRUE;
        ResetAccumulation();
        g_timer = 0.0f;
    } else if (g_training && SubsurfaceTrainPhaseNow() == SUBSURFACE_FAILED) {
        logerror("Unable to finish training...");
        g_training = FALSE;
        g_testing = FALSE;
    }
    if (!g_training && g_testing && GetRenderStats().accumulation > TEST_SAMPLES) {
        if (g_mstage == WAX_STAGE) {
            if (g_estage == SCENEL_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    loginfo("Tested wax-scenelight-ground");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("000_wax_scenel_ground.png");
                    ChangeToWax();
                    StartTraining(MINIMAL_SURFACE, 6, MINIMAL_SPLATS, MINIMAL_WALKS);
                    g_qstage++;
                } else if (g_qstage == MINIMAL_STAGE) {
                    loginfo("Tested wax-scenelight-minimal");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("001_wax_scenel_minimal.png");
                    ChangeToWax();
                    StartTraining(STANDARD_SURFACE, 6, STANDARD_SPLATS, STANDARD_WALKS);
                    g_qstage++;
                } else if (g_qstage == STANDARD_STAGE) {
                    loginfo("Tested wax-scenelight-standard");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("002_wax_scenel_standard.png");
                    ChangeToWax();
                    StartTraining(QUALITY_SURFACE, 8, QUALITY_SPLATS, QUALITY_WALKS);
                    g_qstage++;
                } else {
                    loginfo("Tested wax-scenelight-quality");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("003_wax_scenel_quality.png");
                    ChangeToFlesh();
                    g_qstage = 0;
                    g_mstage++;
                    RendererCamera()->config.gsssenabled = FALSE;
                    ResetAccumulation();
                    g_timer = 0.0f;
                }
            } else if (g_estage == EMISSION_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    loginfo("Tested wax-emission-ground");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("010_wax_emission_ground.png");
                    ChangeToWax();
                    StartTraining(MINIMAL_SURFACE, 6, MINIMAL_SPLATS, MINIMAL_WALKS);
                    g_qstage++;
                } else if (g_qstage == MINIMAL_STAGE) {
                    loginfo("Tested wax-emission-minimal");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("011_wax_emission_minimal.png");
                    ChangeToWax();
                    StartTraining(STANDARD_SURFACE, 6, STANDARD_SPLATS, STANDARD_WALKS);
                    g_qstage++;
                } else if (g_qstage == STANDARD_STAGE) {
                    loginfo("Tested wax-emission-standard");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("012_wax_emission_standard.png");
                    ChangeToWax();
                    StartTraining(QUALITY_SURFACE, 8, QUALITY_SPLATS, QUALITY_WALKS);
                    g_qstage++;
                } else {
                    loginfo("Tested wax-emission-quality");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("013_wax_emission_quality.png");
                    ChangeToFlesh();
                    g_qstage = 0;
                    g_mstage++;
                    RendererCamera()->config.gsssenabled = FALSE;
                    ResetAccumulation();
                    g_timer = 0.0f;
                }
            } else {
                if (g_qstage == GROUND_STAGE) {
                    loginfo("Tested wax-box-ground");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("020_wax_box_ground.png");
                    ChangeToWax();
                    StartTraining(MINIMAL_SURFACE, 6, MINIMAL_SPLATS, MINIMAL_WALKS);
                    g_qstage++;
                } else if (g_qstage == MINIMAL_STAGE) {
                    loginfo("Tested wax-box-minimal");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("021_wax_box_minimal.png");
                    ChangeToWax();
                    StartTraining(STANDARD_SURFACE, 6, STANDARD_SPLATS, STANDARD_WALKS);
                    g_qstage++;
                } else if (g_qstage == STANDARD_STAGE) {
                    loginfo("Tested wax-box-standard");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("022_wax_box_standard.png");
                    ChangeToWax();
                    StartTraining(QUALITY_SURFACE, 8, QUALITY_SPLATS, QUALITY_WALKS);
                    g_qstage++;
                } else {
                    loginfo("Tested wax-box-quality");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("023_wax_box_quality.png");
                    ChangeToFlesh();
                    g_qstage = 0;
                    g_mstage++;
                    RendererCamera()->config.gsssenabled = FALSE;
                    ResetAccumulation();
                    g_timer = 0.0f;
                }
            }
        } else if (g_mstage == FLESH_STAGE) {
            if (g_estage == SCENEL_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    loginfo("Tested flesh-scenelight-ground");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("100_flesh_scenel_ground.png");
                    ChangeToFlesh();
                    StartTraining(MINIMAL_SURFACE, 6, MINIMAL_SPLATS, MINIMAL_WALKS);
                    g_qstage++;
                } else if (g_qstage == MINIMAL_STAGE) {
                    loginfo("Tested flesh-scenelight-minimal");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("101_flesh_scenel_minimal.png");
                    ChangeToFlesh();
                    StartTraining(STANDARD_SURFACE, 6, STANDARD_SPLATS, STANDARD_WALKS);
                    g_qstage++;
                } else if (g_qstage == STANDARD_STAGE) {
                    loginfo("Tested flesh-scenelight-standard");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("102_flesh_scenel_standard.png");
                    ChangeToFlesh();
                    StartTraining(QUALITY_SURFACE, 8, QUALITY_SPLATS, QUALITY_WALKS);
                    g_qstage++;
                } else {
                    loginfo("Tested flesh-scenelight-quality");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("103_flesh_scenel_quality.png");
                    ChangeToJade();
                    g_qstage = 0;
                    g_mstage++;
                    RendererCamera()->config.gsssenabled = FALSE;
                    ResetAccumulation();
                    g_timer = 0.0f;
                }
            } else if (g_estage == EMISSION_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    loginfo("Tested flesh-emission-ground");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("110_flesh_emission_ground.png");
                    ChangeToFlesh();
                    StartTraining(MINIMAL_SURFACE, 6, MINIMAL_SPLATS, MINIMAL_WALKS);
                    g_qstage++;
                } else if (g_qstage == MINIMAL_STAGE) {
                    loginfo("Tested flesh-emission-minimal");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("111_flesh_emission_minimal.png");
                    ChangeToFlesh();
                    StartTraining(STANDARD_SURFACE, 6, STANDARD_SPLATS, STANDARD_WALKS);
                    g_qstage++;
                } else if (g_qstage == STANDARD_STAGE) {
                    loginfo("Tested flesh-emission-standard");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("112_flesh_emission_standard.png");
                    ChangeToFlesh();
                    StartTraining(QUALITY_SURFACE, 8, QUALITY_SPLATS, QUALITY_WALKS);
                    g_qstage++;
                } else {
                    loginfo("Tested flesh-emission-quality");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("113_flesh_emission_quality.png");
                    ChangeToJade();
                    g_qstage = 0;
                    g_mstage++;
                    RendererCamera()->config.gsssenabled = FALSE;
                    ResetAccumulation();
                    g_timer = 0.0f;
                }
            } else {
                if (g_qstage == GROUND_STAGE) {
                    loginfo("Tested flesh-box-ground");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("120_flesh_box_ground.png");
                    ChangeToFlesh();
                    StartTraining(MINIMAL_SURFACE, 6, MINIMAL_SPLATS, MINIMAL_WALKS);
                    g_qstage++;
                } else if (g_qstage == MINIMAL_STAGE) {
                    loginfo("Tested flesh-box-minimal");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("121_flesh_box_minimal.png");
                    ChangeToFlesh();
                    StartTraining(STANDARD_SURFACE, 6, STANDARD_SPLATS, STANDARD_WALKS);
                    g_qstage++;
                } else if (g_qstage == STANDARD_STAGE) {
                    loginfo("Tested flesh-box-standard");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("122_flesh_box_standard.png");
                    ChangeToFlesh();
                    StartTraining(QUALITY_SURFACE, 8, QUALITY_SPLATS, QUALITY_WALKS);
                    g_qstage++;
                } else {
                    loginfo("Tested flesh-box-quality");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("123_flesh_box_quality.png");
                    ChangeToJade();
                    g_qstage = 0;
                    g_mstage++;
                    RendererCamera()->config.gsssenabled = FALSE;
                    ResetAccumulation();
                    g_timer = 0.0f;
                }
            }
        } else {
            if (g_estage == SCENEL_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    loginfo("Tested jade-scenelight-ground");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("200_jade_scenel_ground.png");
                    ChangeToJade();
                    StartTraining(MINIMAL_SURFACE, 6, MINIMAL_SPLATS, MINIMAL_WALKS);
                    g_qstage++;
                } else if (g_qstage == MINIMAL_STAGE) {
                    loginfo("Tested jade-scenelight-minimal");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("201_jade_scenel_minimal.png");
                    ChangeToJade();
                    StartTraining(STANDARD_SURFACE, 6, STANDARD_SPLATS, STANDARD_WALKS);
                    g_qstage++;
                } else if (g_qstage == STANDARD_STAGE) {
                    loginfo("Tested jade-scenelight-standard");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("202_jade_scenel_standard.png");
                    ChangeToJade();
                    StartTraining(QUALITY_SURFACE, 8, QUALITY_SPLATS, QUALITY_WALKS);
                    g_qstage++;
                } else {
                    loginfo("Tested jade-scenelight-quality");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("203_jade_scenel_quality.png");
                    g_qstage = 0;
                    g_mstage = 0;
                    g_estage++;
                    ClearScene(FALSE);
                    LoadOBJ("assets/models/OBJ/naked/bunny.obj");
                    ChangeToWax();
                    SetVoidCamera();
                    CreateBoxLight(0, 2, 1);
                    ResetAccumulation();
                    RendererCamera()->config.gsssenabled = FALSE;
                    ResetAccumulation();
                    g_timer = 0.0f;
                }
            } else if (g_estage == EMISSION_STAGE) {
                if (g_qstage == GROUND_STAGE) {
                    loginfo("Tested jade-emission-ground");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("210_jade_emission_ground.png");
                    ChangeToJade();
                    StartTraining(MINIMAL_SURFACE, 6, MINIMAL_SPLATS, MINIMAL_WALKS);
                    g_qstage++;
                } else if (g_qstage == MINIMAL_STAGE) {
                    loginfo("Tested jade-emission-minimal");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("211_jade_emission_minimal.png");
                    ChangeToJade();
                    StartTraining(STANDARD_SURFACE, 6, STANDARD_SPLATS, STANDARD_WALKS);
                    g_qstage++;
                } else if (g_qstage == STANDARD_STAGE) {
                    loginfo("Tested jade-emission-standard");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("212_jade_emission_standard.png");
                    ChangeToJade();
                    StartTraining(QUALITY_SURFACE, 8, QUALITY_SPLATS, QUALITY_WALKS);
                    g_qstage++;
                } else {
                    loginfo("Tested jade-emission-quality");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("213_jade_emission_quality.png");
                    g_qstage = 0;
                    g_mstage = 0;
                    g_estage++;
                    ClearScene(FALSE);
                    ChangeToWax();
                    SubmitNamedLight((SceneLight){{0,1.9,0},{5,5,5},{0,-1,0},1,170.0f}, "Box Spot Light");
                    LoadOBJ("assets/models/OBJ/naked/bunny.obj");
                    LoadOBJ("assets/models/OBJ/dressed/CornellBox-Original.obj");
                    MeshDescriptor* md = MeshReference(0);
                    md->translate[0] = 0.3;
                    md->translate[1] = 0.8;
                    md->translate[2] = 0.4;
                    md->scale[0] = 0.2;
                    md->scale[1] = 0.2;
                    md->scale[2] = 0.2;
                    UpdateObjectTransform(0);
                    SetBoxCamera();
                    ResetAccumulation();
                    RendererCamera()->config.gsssenabled = FALSE;
                    ResetAccumulation();
                    g_timer = 0.0f;
                }
            } else {
                if (g_qstage == GROUND_STAGE) {
                    loginfo("Tested jade-box-ground");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("220_jade_box_ground.png");
                    ChangeToJade();
                    StartTraining(MINIMAL_SURFACE, 6, MINIMAL_SPLATS, MINIMAL_WALKS);
                    g_qstage++;
                } else if (g_qstage == MINIMAL_STAGE) {
                    loginfo("Tested jade-box-minimal");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("221_jade_box_minimal.png");
                    ChangeToJade();
                    StartTraining(STANDARD_SURFACE, 6, STANDARD_SPLATS, STANDARD_WALKS);
                    g_qstage++;
                } else if (g_qstage == STANDARD_STAGE) {
                    loginfo("Tested jade-box-standard");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("222_jade_box_standard.png");
                    ChangeToJade();
                    StartTraining(QUALITY_SURFACE, 8, QUALITY_SPLATS, QUALITY_WALKS);
                    g_qstage++;
                } else {
                    loginfo("Tested jade-box-quality");
                    loginfo("rendered in %.3f", g_timer);
                    SaveViewportRender("223_jade_box_quality.png");
                    g_testing = FALSE;
                    RendererCamera()->config.flags = PREVIEW_PIPELINE_FLAGS;
                    loginfo("Finished testing suite");
                    g_timer = 0.0f;
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
    AddBind("set triangles to default material", ChangeAllToDefault,
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
    AddBind("set wax material", ChangeToWax,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_ALIGN_Z, BIND_KEY_PRESSED });
    AddBind("set flesh material", ChangeToFlesh,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_ALIGN_X, BIND_KEY_PRESSED });
    AddBind("set jade material", ChangeToJade,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_ALIGN_Y, BIND_KEY_PRESSED });
}

void DevUpdate() { TestSuite(); }

#else

void DevInitialize() {}

void DevUpdate() {}

#endif
