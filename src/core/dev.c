#include "dev.h"

#ifndef PROD_BUILD

#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "ui/panels/edit.h"
#include "renderer/rmath.h"
#include <data/input.h>
#include <core/binds.h>

static void LoadStones() {
    LoadOBJ("assets/models/OBJ/dressed/gems.obj");
    SubmitNamedLight((SceneLight){{0},{1,1,1},{0,-1,0},0,0}, "Gems Overhead Light");
    FitCamera();
}

static void LoadBox() {
    LoadOBJ("assets/models/OBJ/dressed/CornellBox-Sphere.obj");
    FitCamera();
}

static void LoadRabbit() {
    LoadOBJ("assets/models/OBJ/naked/bunny.obj");
    if (FALSE) {
        SubmitNamedLight((SceneLight){{0, 0, -1.5f},{10,10,10},{0,0,0},0,0}, "Rabbit Point Light");
    } else {
        SurfaceMaterial light = { 0 };
        light.emission[0] = 10.0f;
        light.emission[1] = 10.0f;
        light.emission[2] = 10.0f;
        SubmitNamedMaterial(light, "Light");
        vec3 position = { 0, 0, -3 };
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
            FALSE, vertex_base, NumVertices() - 1, triangle_base, NumTriangles() - 1, 0, (uint32_t)-1, { 0 }, INLINEV3(extent), { 0 }, { 0 },
            { 1.0f, 1.0f, 1.0f }, GLM_MAT4_IDENTITY_INIT }, "Cube Light");
    }
    FitCamera();
}

static void Screenshot() {
    SaveRender("out.png");
}

static void ClearSoftScene() {
    ClearScene(FALSE);
}

void DevInitialize() {
    AddBind("load stones", LoadStones,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_L_OVERRIDE, BIND_KEY_DOWN },
        (BindCommand){ IK_A_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("load cornell box", LoadBox,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_L_OVERRIDE, BIND_KEY_DOWN },
        (BindCommand){ IK_B_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("load rabbit", LoadRabbit,
        (BindCommand){ IK_R_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("clear scene", ClearSoftScene,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_C_OVERRIDE, BIND_KEY_PRESSED });
    AddBind("screenshot", Screenshot,
        (BindCommand){ IK_DEV, BIND_KEY_DOWN },
        (BindCommand){ IK_S_OVERRIDE, BIND_KEY_PRESSED });
}

void DevUpdate() {}

#else

void DevInitialize() {}

void DevUpdate() {}

#endif
