#include "shared.h"
#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "renderer/rmath.h"
#include "renderer/texture.h"
#include "ui/panels/edit.h"
#include <data/colors.h>
#include <util/logger.h>
#include <ui/popup.h>

static char* g_lightmodel_labels[] = { "lambertian", "mirror", "dielectric", "subsurface" };
static char* g_arapmodel_labels[] = { "rigid", "cubic" };
static char* g_sim_visual_labels[] = { "smoke", "fire", "water", "plasma" };
static char* g_debugmode_labels[] = { "none", "normals", "bvh", "bounces", "sss path", "sss cost", "variance", "uvs" };
static FluidForce g_fluid_force = { 0 };
static FluidSource g_fluid_source = { 0 };
static vec3 g_cube_position = { 0 };
static vec3 g_cube_scale = { 1.0, 1.0, 1.0 };
static char g_force_name[MAX_FORCE_NAME_SIZE] = "Untitled Force";
static char g_source_name[MAX_SOURCE_NAME_SIZE] = "Untitled Source";
static char g_object_name[MAX_MESH_NAME_SIZE] = "Untitled Object";
static Triangle g_dummy_triangle = { 0 };

static int add_object_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    float width = 250;
    float height = 360;
    float xpos = x + ((w - width) / 2.0f);
    float ypos = y + ((h - height) / 2.0f);
    float button_width = 200;
    UISetPosition(0, 0);
    UISetCursor(0, ypos + 10);
    DrawRectangle(xpos, ypos, width, height, MappedColor(PANEL_BG_COLOR));
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Add to Scene") / 2), 0);
    UIDrawText("Add to Scene");
    UIMoveCursor(xpos + (width / 2) - (button_width / 2) - 10, 20);
    if (UIButton("Material", button_width)) return 0;
    UIMoveCursor(xpos + (width / 2) - (button_width / 2) - 10, 10);
    if (UIButton("Scene Light", button_width)) return 1;
    UIMoveCursor(xpos + (width / 2) - (button_width / 2) - 10, 10);
    if (UIButton("Cube", button_width)) return 2;
    UIMoveCursor(xpos + (width / 2) - (button_width / 2) - 10, 10);
    if (UIButton(".OBJ", button_width)) return 3;
    UIMoveCursor(xpos + (width / 2) - (button_width / 2) - 10, 10);
    if (UIButton(".FBX", button_width)) return 4;
    UIMoveCursor(xpos + (width / 2) - (button_width / 2) - 10, 10);
    if (UIButton(".PLY", button_width)) return 5;
    UIMoveCursor(xpos + (width / 2) - (button_width / 2) - 10, 10);
    if (UIButton(".PBJ", button_width)) return 6;
    UIMoveCursor(xpos + (width / 2) - (button_width / 2) - 10, 10);
    if (UIButton("Camera", button_width)) return 7;
    UIMoveCursor(xpos + (width / 2) - (button_width / 2) - 10, 10);
    if (UIButton("Texture", button_width)) return 8;
    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 40);
    if (UIButton("Cancel", button_width)) return 9;
    return -1;
}

static int add_obj_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    const char* filepath = GetOpenFile("obj");
    if (filepath && !LoadOBJ(filepath)) {
        logerror("Unable to load obj file \"%s\"", filepath);
    }
    return 0;
}

static int add_fbx_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    const char* filepath = GetOpenFile("fbx");
    if (filepath && !LoadFBX(filepath)) {
        logerror("Unable to load fbx file \"%s\"", filepath);
    }
    return 0;
}

static int add_ply_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    const char* filepath = GetOpenFile("ply");
    if (filepath && !LoadPLY(filepath)) {
        logerror("Unable to load ply file \"%s\"", filepath);
    }
    return 0;
}

static int add_pbj_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    const char* filepath = GetOpenFile("pbj");
    PBJType type;
    size_t index;
    if (filepath && LoadPBJ(filepath, &type, &index)) {
        switch (type) {
            case PBJ_CAMERA: SetEditCamera(index); break;
            case PBJ_MATERIAL: SetEditMaterial(index); break;
            case PBJ_LIGHT: SetEditLight(index); break;
            case PBJ_MESH: SetEditMesh(index); break;
            case PBJ_FORCE: SetEditForce(index); break;
            case PBJ_SOURCE: SetEditSource(index); break;
            case PBJ_TEXTURE: SetEditTexture(index); break;
            default: break;
        }
    }
    return 0;
}

static int add_texture_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    const char* filepath = GetOpenFile("png,jpg,jpeg,tga,bmp,hdr,psd,gif,pic,ppm,pgm");
    if (filepath) {
        size_t index = LoadTextureFile(filepath, FALSE);
        if (index != (size_t)-1) SetEditTexture(index);
        else logerror("Unable to load texture \"%s\"", filepath);
    }
    return 0;
}

static int add_camera_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    SubmitCamera(DefaultCamera());
    SetEditCamera(NumCameras() - 1);
    return 0;
}

static int add_material_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    SurfaceMaterial mat = { 0 };
    mat.model = 2;
    SubmitNamedMaterial(mat, "New Material");
    SetEditMaterial(NumMaterials() - 1);
    return 0;
}

static int add_light_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    SceneLight defaultlight = { 0 };
    defaultlight.color[0] = 5.0f;
    defaultlight.color[1] = 5.0f;
    defaultlight.color[2] = 5.0f;
    SubmitNamedLight(defaultlight, "New Light");
    SetEditLight(NumLights() - 1);
    return 0;
}

static int add_cube_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    size_t vertex_base = NumVertices();
    size_t triangle_base = NumTriangles();
    vec3 v1 = {
        g_cube_position[0] - g_cube_scale[0]/2.0f,
        g_cube_position[1] - g_cube_scale[1]/2.0f,
        g_cube_position[2] - g_cube_scale[2]/2.0f};
    vec3 v2 = {
        g_cube_position[0] - g_cube_scale[0]/2.0f,
        g_cube_position[1] + g_cube_scale[1]/2.0f,
        g_cube_position[2] - g_cube_scale[2]/2.0f};
    vec3 v3 = {
        g_cube_position[0] + g_cube_scale[0]/2.0f,
        g_cube_position[1] + g_cube_scale[1]/2.0f,
        g_cube_position[2] - g_cube_scale[2]/2.0f};
    vec3 v4 = {
        g_cube_position[0] + g_cube_scale[0]/2.0f,
        g_cube_position[1] - g_cube_scale[1]/2.0f,
        g_cube_position[2] - g_cube_scale[2]/2.0f};
    vec3 v5 = {
        g_cube_position[0] - g_cube_scale[0]/2.0f,
        g_cube_position[1] - g_cube_scale[1]/2.0f,
        g_cube_position[2] + g_cube_scale[2]/2.0f};
    vec3 v6 = {
        g_cube_position[0] - g_cube_scale[0]/2.0f,
        g_cube_position[1] + g_cube_scale[1]/2.0f,
        g_cube_position[2] + g_cube_scale[2]/2.0f};
    vec3 v7 = {
        g_cube_position[0] + g_cube_scale[0]/2.0f,
        g_cube_position[1] + g_cube_scale[1]/2.0f,
        g_cube_position[2] + g_cube_scale[2]/2.0f};
    vec3 v8 = {
        g_cube_position[0] + g_cube_scale[0]/2.0f,
        g_cube_position[1] - g_cube_scale[1]/2.0f,
        g_cube_position[2] + g_cube_scale[2]/2.0f};
    SubmitVertex(v1);
    SubmitVertex(v2);
    SubmitVertex(v3);
    SubmitVertex(v4);
    SubmitVertex(v5);
    SubmitVertex(v6);
    SubmitVertex(v7);
    SubmitVertex(v8);
    size_t cube_tstart = NumTriangles();
    SubmitTriangle((Triangle){ vertex_base + 3, vertex_base + 1, vertex_base + 0, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    SubmitTriangle((Triangle){ vertex_base + 3, vertex_base + 2, vertex_base + 1, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    SubmitTriangle((Triangle){ vertex_base + 2, vertex_base + 5, vertex_base + 1, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    SubmitTriangle((Triangle){ vertex_base + 2, vertex_base + 6, vertex_base + 5, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    SubmitTriangle((Triangle){ vertex_base + 7, vertex_base + 2, vertex_base + 3, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    SubmitTriangle((Triangle){ vertex_base + 7, vertex_base + 6, vertex_base + 2, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    SubmitTriangle((Triangle){ vertex_base + 3, vertex_base + 0, vertex_base + 4, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    SubmitTriangle((Triangle){ vertex_base + 4, vertex_base + 7, vertex_base + 3, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    SubmitTriangle((Triangle){ vertex_base + 5, vertex_base + 6, vertex_base + 7, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    SubmitTriangle((Triangle){ vertex_base + 7, vertex_base + 4, vertex_base + 5, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    SubmitTriangle((Triangle){ vertex_base + 0, vertex_base + 1, vertex_base + 5, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    SubmitTriangle((Triangle){ vertex_base + 5, vertex_base + 4, vertex_base + 0, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material, NO_UVS });
    g_dummy_triangle = (Triangle){ 0 };
    for (size_t t = cube_tstart; t < NumTriangles(); t++) {
        Triangle* tri = TriangleReference(t);
        float* p[3] = { VertexReference(tri->a), VertexReference(tri->b), VertexReference(tri->c) };
        vec3 e1, e2, n;
        glm_vec3_sub(p[1], p[0], e1);
        glm_vec3_sub(p[2], p[0], e2);
        glm_vec3_cross(e1, e2, n);
        int axis = fabsf(n[0]) > fabsf(n[1]) ? (fabsf(n[0]) > fabsf(n[2]) ? 0 : 2) : (fabsf(n[1]) > fabsf(n[2]) ? 1 : 2);
        int ua = axis == 0 ? 2 : 0;
        int va = axis == 1 ? 2 : 1;
        float side = (p[0][axis] + p[1][axis] + p[2][axis]) / 3.0f - g_cube_position[axis];
        float flip = side > 0.0f ? 1.0f : -1.0f;
        if (axis == 1) flip = -flip;
        uint32_t* corners[3] = { &(tri->at), &(tri->bt), &(tri->ct) };
        for (size_t k = 0; k < 3; k++) {
            float u = (p[k][ua] - (g_cube_position[ua] - g_cube_scale[ua] / 2.0f)) / g_cube_scale[ua];
            float v = (p[k][va] - (g_cube_position[va] - g_cube_scale[va] / 2.0f)) / g_cube_scale[va];
            *(corners[k]) = (uint32_t)NumUVs();
            SubmitUV((vec2){ axis == 0 ? (flip > 0.0f ? 1.0f - u : u) : (flip > 0.0f ? u : 1.0f - u), v });
        }
    }
    UpdateTriangles();
    vec3 extent;
    glm_vec3_scale(g_cube_scale, 0.5f, extent);
    SubmitMeshDescriptor((MeshDescriptor){
        FALSE, vertex_base, NumVertices() - 1, triangle_base, NumTriangles() - 1, 0, (uint32_t)-1,
        (uint32_t)NumSplats(), (uint32_t)NumSplats() - 1, { 0 }, INLINEV3(extent), { 0 }, { 0 },
        { 1.0f, 1.0f, 1.0f }, GLM_MAT4_IDENTITY_INIT, TEXFILTER_BILINEAR, FALSE, 1 }, g_object_name);
    memset(g_object_name, 0, MAX_MESH_NAME_SIZE);
    strcpy(g_object_name, "New Cube");
    SetEditMesh(NumMeshes() - 1);
    return 0;
}

static int add_sim_object_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    float width = 250;
    float height = 220;
    float xpos = x + ((w - width) / 2.0f);
    float ypos = y + ((h - height) / 2.0f);
    float button_width = 200;
    UISetPosition(0, 0);
    UISetCursor(0, ypos + 10);
    DrawRectangle(xpos, ypos, width, height, MappedColor(PANEL_BG_COLOR));
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Add to Simulation") / 2), 0);
    UIDrawText("Add to Simulation");
    UIMoveCursor(xpos + (width / 2) - (button_width / 2) - 10, 20);
    if (UIButton("Force", button_width)) return 0;
    UIMoveCursor(xpos + (width / 2) - (button_width / 2) - 10, 10);
    if (UIButton("Source", button_width)) return 1;
    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 40);
    if (UIButton("Cancel", button_width)) return 3;
    return -1;
}

static int add_fluid_force_stage_0(size_t x, size_t y, size_t w, size_t h) {
    float width = 385;
    float height = 450;
    float xpos = x + ((w - width) / 2.0f);
    float ypos = y + ((h - height) / 2.0f);
    float button_width = 200;
    FluidSimulation* fsim = &(RendererGeometry()->fluid);
    UISetPosition(0, 0);
    UISetCursor(0, ypos + 10);
    DrawRectangle(xpos, ypos, width, height, MappedColor(PANEL_BG_COLOR));
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Add Force") / 2), 0);
    UIDrawText("Add Force");

    UIMoveCursor(xpos, 15);
    UITextInput("Force Name", g_force_name, MAX_FORCE_NAME_SIZE, width - 20, FALSE);

    UIMoveCursor(xpos, 15);
    UIDivider(width - 20);
    UIMoveCursor(xpos, 5);
    UIDrawText("Global Force");
    UIMoveCursor(xpos + ((width - 20) / 2.0f), -20);
    UICheckbox(&(g_fluid_force.global));
    UIMoveCursor(xpos, 5);
    UIDivider(width - 20);

    if (g_fluid_force.global) DisableUI();
    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Size") / 2) - 10, 0);
    UIDrawText("Size");
    UIMoveCursor(xpos, 0);
    UIDrawText("w");
    UIMoveCursor(xpos + 15, -20);
    UIDragSize(&(g_fluid_force.width), 0, fsim->width - g_fluid_force.x, 1, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("h");
    UIMoveCursor(xpos + 140, -20);
    UIDragSize(&(g_fluid_force.height), 0, fsim->height - g_fluid_force.y, 1, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("l");
    UIMoveCursor(xpos + 265, -20);
    UIDragSize(&(g_fluid_force.length), 0, fsim->length - g_fluid_force.z, 1, 100);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Origin") / 2) - 10, 0);
    UIDrawText("Origin");
    UIMoveCursor(xpos, 0);
    UIDrawText("x");
    UIMoveCursor(xpos + 15, -20);
    UIDragSize(&(g_fluid_force.x), 0, fsim->width - g_fluid_force.width, 1, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("y");
    UIMoveCursor(xpos + 140, -20);
    UIDragSize(&(g_fluid_force.y), 0, fsim->height - g_fluid_force.height, 1, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("z");
    UIMoveCursor(xpos + 265, -20);
    UIDragSize(&(g_fluid_force.z), 0, fsim->length - g_fluid_force.length, 1, 100);
    EnableUI();

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Force Vector") / 2) - 10, 0);
    UIDrawText("Force Vector");
    UIMoveCursor(xpos, 0);
    UIDrawText("x");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_fluid_force.force[0]), -FLT_MAX, FLT_MAX, 0.001f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("y");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_fluid_force.force[1]), -FLT_MAX, FLT_MAX, 0.001f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("z");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_fluid_force.force[2]), -FLT_MAX, FLT_MAX, 0.001f, 100);

    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 70);
    if (UIButton("Submit", button_width)) {
        SubmitForce(g_fluid_force, g_force_name);
        g_fluid_force = (FluidForce){ 0 };
        memset(g_force_name, 0, MAX_FORCE_NAME_SIZE);
        strcpy(g_force_name, "Untitled Force");
        return 0;
    }
    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 40);
    if (UIButton("Cancel", button_width)) return 0;
    return -1;
}

static int add_fluid_source_stage_0(size_t x, size_t y, size_t w, size_t h) {
    float width = 385;
    float height = 450;
    float xpos = x + ((w - width) / 2.0f);
    float ypos = y + ((h - height) / 2.0f);
    float button_width = 200;
    FluidSimulation* fsim = &(RendererGeometry()->fluid);
    UISetPosition(0, 0);
    UISetCursor(0, ypos + 10);
    DrawRectangle(xpos, ypos, width, height, MappedColor(PANEL_BG_COLOR));
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Add Source") / 2), 0);
    UIDrawText("Add Source");

    UIMoveCursor(xpos, 15);
    UITextInput("Source Name", g_source_name, MAX_SOURCE_NAME_SIZE, width - 20, FALSE);

    UIMoveCursor(xpos, 15);
    UIDivider(width - 20);
    UIMoveCursor(xpos, 5);
    UIDrawText("Density");
    UIMoveCursor(xpos + ((width - 20) / 2.0f), -20);
    UIDragFloat(&(g_fluid_source.density), 0.0f, FLT_MAX, 0.01f, (width - 20.0f)/2.0f);
    UIMoveCursor(xpos, 5);
    UIDrawText("Lifetime");
    UIMoveCursor(xpos + ((width - 20) / 2.0f), -20);
    UIDragFloat(&(g_fluid_source.lifetime), 0.0f, FLT_MAX, 0.01f, (width - 20.0f)/2.0f);
    UIMoveCursor(xpos, 5);
    UIDivider(width - 20);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Size") / 2) - 10, 0);
    UIDrawText("Size");
    UIMoveCursor(xpos, 0);
    UIDrawText("w");
    UIMoveCursor(xpos + 15, -20);
    UIDragSize(&(g_fluid_source.width), 0, fsim->width - g_fluid_source.x, 1, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("h");
    UIMoveCursor(xpos + 140, -20);
    UIDragSize(&(g_fluid_source.height), 0, fsim->height - g_fluid_source.y, 1, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("l");
    UIMoveCursor(xpos + 265, -20);
    UIDragSize(&(g_fluid_source.length), 0, fsim->length - g_fluid_source.z, 1, 100);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Origin") / 2) - 10, 0);
    UIDrawText("Origin");
    UIMoveCursor(xpos, 0);
    UIDrawText("x");
    UIMoveCursor(xpos + 15, -20);
    UIDragSize(&(g_fluid_source.x), 0, fsim->width - g_fluid_source.width, 1, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("y");
    UIMoveCursor(xpos + 140, -20);
    UIDragSize(&(g_fluid_source.y), 0, fsim->height - g_fluid_source.height, 1, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("z");
    UIMoveCursor(xpos + 265, -20);
    UIDragSize(&(g_fluid_source.z), 0, fsim->length - g_fluid_source.length, 1, 100);

    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 70);
    if (UIButton("Submit", button_width)) {
        SubmitSource(g_fluid_source, g_source_name);
        g_fluid_source = (FluidSource){ 0 };
        memset(g_source_name, 0, MAX_SOURCE_NAME_SIZE);
        strcpy(g_source_name, "Untitled Source");
        return 0;
    }
    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 40);
    if (UIButton("Cancel", button_width)) return 0;
    return -1;
}

size_t DropdownSelectSimVisual(void* data, size_t index, BOOL cancel) {
    if (index == (size_t)-1) {
        return RendererGeometry()->fluid.style;
    } else {
        RendererGeometry()->fluid.style = index;
    }
    return index;
}

size_t DropdownSelectMaterial(void* data, size_t index, BOOL cancel) {
    Triangle* triref = (Triangle*)data;
    if (index == (size_t)-1) return triref->material;
    BOOL refresh_emission = MaterialReference(triref->material)->emission[0] != 0 ||
        MaterialReference(triref->material)->emission[1] != 0 ||
        MaterialReference(triref->material)->emission[2] != 0 ||
        MaterialReference(index)->emission[0] != 0 ||
        MaterialReference(index)->emission[1] != 0 ||
        MaterialReference(index)->emission[2] != 0;
    triref->material = index;
    if (refresh_emission) RefreshEmissives();
    if (NumTriangles() != 0) UpdateTriangles();
    return index;
}

size_t DropdownSelectLightModel(void* data, size_t index, BOOL cancel) {
    SurfaceMaterial* matref = (SurfaceMaterial*)data;
    if (index == (size_t)-1) {
        switch (matref->model) {
            case LAMBERTIAN_MODEL: return 0;
            case MIRROR_MODEL: return 1;
            case DIELECTRIC_MODEL: return 2;
            case SUBSURFACE_MODEL: return 3;
            default: return 0;
        }
    } else {
        switch (index) {
            case 0:
                matref->model = LAMBERTIAN_MODEL;
                break;
            case 1:
                matref->model = MIRROR_MODEL;
                break;
            case 2:
                matref->model = DIELECTRIC_MODEL;
                break;
            case 3:
                matref->model = SUBSURFACE_MODEL;
                break;
            default: break;
        }
        UpdateMaterials();
    }
    return index;
}

size_t DropdownSelectARAPModel(void* data, size_t index, BOOL cancel) {
    if (index == (size_t)-1) {
        return RenderConfig()->arap.style;
    } else {
        RenderConfig()->arap.style = index;
    }
    return index;
}

size_t DropdownSelectAnimation(void* data, size_t index, BOOL cancel) {
    MeshAnimation* animation = (MeshAnimation*)data;
    if (index != (size_t)-1) SwitchAnimation(animation, index);
    return animation->current;
}

size_t DropdownSelectDebugMode(void* data, size_t index, BOOL cancel) {
    if (index != (size_t)-1) RenderConfig()->debug = (DebugConfig)index;
    return RenderConfig()->debug;
}

char** SimVisualLabels() {
    return g_sim_visual_labels;
}

char** LightModelLabels() {
    return g_lightmodel_labels;
}

char** ARAPModelLabels() {
    return g_arapmodel_labels;
}

char** DebugModeLabels() {
    return g_debugmode_labels;
}
Popup* GenerateAddObjectPopup() {
    Popup* popup = GenerateEmptyPopup();
    popup->options = 9;
    popup->behavior = add_object_popup_stage_0;
    popup->results = EZ_ALLOC(popup->options, sizeof(Popup*));
    PopupFunction stage_1[] = {add_material_popup_stage_0, add_light_popup_stage_0, add_cube_popup_stage_0, add_obj_popup_stage_0, add_fbx_popup_stage_0, add_ply_popup_stage_0, add_pbj_popup_stage_0, add_camera_popup_stage_0, add_texture_popup_stage_0 };
    for (size_t i = 0; i < popup->options; i++) {
        Popup* next = GenerateEmptyPopup();
        next->options = 0;
        next->behavior = stage_1[i];
        ((Popup**)popup->results)[i] = next;
    }
    return popup;
}

Popup* GenerateAddSimObjectPopup() {
    Popup* popup = GenerateEmptyPopup();
    popup->options = 2;
    popup->behavior = add_sim_object_popup_stage_0;
    popup->results = EZ_ALLOC(popup->options, sizeof(Popup*));
    PopupFunction stage_1[] = {add_fluid_force_stage_0, add_fluid_source_stage_0};
    for (size_t i = 0; i < popup->options; i++) {
        Popup* next = GenerateEmptyPopup();
        next->options = 0;
        next->behavior = stage_1[i];
        ((Popup**)popup->results)[i] = next;
    }
    return popup;
}
