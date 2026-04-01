#include "popup.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include "data/colors.h"
#include "data/input.h"
#include "ui/shared.h"
#include "ui/ui.h"
#include <easymemory.h>
#include <easylogger.h>

SceneLight g_scene_light = { 0 };
SurfaceMaterial g_material = { 0 };
FluidForce g_fluid_force = { 0 };
FluidSource g_fluid_source = { 0 };
vec3 g_cube_position = { 0 };
vec3 g_cube_scale = { 1.0, 1.0, 1.0 };
char g_material_name[MAX_MATERIAL_NAME_SIZE] = "Untitled Material";
char g_light_name[MAX_LIGHT_NAME_SIZE] = "Untitled Light";
char g_force_name[MAX_FORCE_NAME_SIZE] = "Untitled Force";
char g_source_name[MAX_SOURCE_NAME_SIZE] = "Untitled Source";
char g_object_name[MAX_MESH_NAME_SIZE] = "Untitled Object";
Triangle g_dummy_triangle = { 0 };

int add_object_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    float width = 250;
    float height = 280;
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
    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 40);
    if (UIButton("Cancel", button_width)) return 3;
    return -1;
}

int add_material_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    if (g_material.model == 0) g_material.model = 2;
    float width = 385;
    float height = 650;
    float xpos = x + ((w - width) / 2.0f);
    float ypos = y + ((h - height) / 2.0f);
    float button_width = 200;
    UISetPosition(0, 0);
    UISetCursor(0, ypos + 10);
    DrawRectangle(xpos, ypos, width, height, MappedColor(PANEL_BG_COLOR));
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Add Material") / 2), 0);
    UIDrawText("Add Material");

    UIMoveCursor(xpos, 15);
    UITextInput("Material Name", g_material_name, MAX_MATERIAL_NAME_SIZE, width - 20);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Emission") / 2) - 10, 0);
    UIDrawText("Emission");
    UIMoveCursor(xpos, 0);
    UIDrawText("r");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_material.emission[0]), 0, FLT_MAX, 0.05f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("g");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_material.emission[1]), 0, FLT_MAX, 0.05f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("b");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_material.emission[2]), 0, FLT_MAX, 0.05f, 100);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Absorbtion") / 2) - 10, 0);
    UIDrawText("Absorbtion");
    UIMoveCursor(xpos, 0);
    UIDrawText("r");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_material.absorbtion[0]), 0, FLT_MAX, 0.001f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("g");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_material.absorbtion[1]), 0, FLT_MAX, 0.001f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("b");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_material.absorbtion[2]), 0, FLT_MAX, 0.001f, 100);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Dispersion") / 2) - 10, 0);
    UIDrawText("Dispersion");
    UIMoveCursor(xpos, 0);
    UIDrawText("r");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_material.dispersion[0]), 0, FLT_MAX, 0.001f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("g");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_material.dispersion[1]), 0, FLT_MAX, 0.001f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("b");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_material.dispersion[2]), 0, FLT_MAX, 0.001f, 100);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Ambient") / 2) - 10, 0);
    UIDrawText("Ambient");
    UIMoveCursor(xpos, 0);
    UIDrawText("r");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_material.ambient[0]), 0, 1.0f, 0.05f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("g");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_material.ambient[1]), 0, 1.0f, 0.05f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("b");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_material.ambient[2]), 0, 1.0f, 0.05f, 100);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Diffuse") / 2) - 10, 0);
    UIDrawText("Diffuse");
    UIMoveCursor(xpos, 0);
    UIDrawText("r");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_material.diffuse[0]), 0, 1.0f, 0.05f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("g");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_material.diffuse[1]), 0, 1.0f, 0.05f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("b");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_material.diffuse[2]), 0, 1.0f, 0.05f, 100);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Specular") / 2) - 10, 0);
    UIDrawText("Specular");
    UIMoveCursor(xpos, 0);
    UIDrawText("r");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_material.specular[0]), 0, 1.0f, 0.05f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("g");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_material.specular[1]), 0, 1.0f, 0.05f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("b");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_material.specular[2]), 0, 1.0f, 0.05f, 100);

    UIMoveCursor(xpos, 35);
    UIDrawText("Refraction Index");
    UIMoveCursor(xpos + 165, -20);
    UIDragFloat(&(g_material.ior), 0, FLT_MAX, 0.01f, 200);
    UIMoveCursor(xpos, 5);
    UIDrawText("Shininess");
    UIMoveCursor(xpos + 165, -20);
    UIDragFloat(&(g_material.shiny), 0, FLT_MAX, 0.01f, 200);
    UIMoveCursor(xpos, 5);
    
    UIMoveCursor(0, 35);
    UIDrawText("Lighting Model");
    UIMoveCursor(xpos + 165, -20);
    UIDropdownMenu(200, 3, LightModelLabels(), DropdownSelectLightModel, &g_material);

    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 70);
    if (UIButton("Submit", button_width)) {
        SubmitNamedMaterial(g_material, g_material_name);
        g_material = (SurfaceMaterial){ 0 };
        memset(g_material_name, 0, MAX_MATERIAL_NAME_SIZE);
        strcpy(g_material_name, "Untitled Material");
        return 0;
    }
    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 40);
    if (UIButton("Cancel", button_width)) return 0;
    return -1;
}

int add_light_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    float width = 385;
    float height = 400;
    float xpos = x + ((w - width) / 2.0f);
    float ypos = y + ((h - height) / 2.0f);
    float button_width = 200;
    UISetPosition(0, 0);
    UISetCursor(0, ypos + 10);
    DrawRectangle(xpos, ypos, width, height, MappedColor(PANEL_BG_COLOR));
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Add Scene Light") / 2), 0);
    UIDrawText("Add Scene Light");

    UIMoveCursor(xpos, 15);
    UITextInput("Light Name", g_light_name, MAX_LIGHT_NAME_SIZE, width - 20);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Position") / 2) - 10, 0);
    UIDrawText("Position");
    UIMoveCursor(xpos, 0);
    UIDrawText("x");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_scene_light.position[0]), -FLT_MAX, FLT_MAX, 0.1f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("y");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_scene_light.position[1]), -FLT_MAX, FLT_MAX, 0.1f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("z");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_scene_light.position[2]), -FLT_MAX, FLT_MAX, 0.1f, 100);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Intensity") / 2) - 10, 0);
    UIDrawText("Intensity");
    UIMoveCursor(xpos, 0);
    UIDrawText("r");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_scene_light.color[0]), 0, FLT_MAX, 0.05f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("g");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_scene_light.color[1]), 0, FLT_MAX, 0.05f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("b");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_scene_light.color[2]), 0, FLT_MAX, 0.05f, 100);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Direction") / 2) - 10, 0);
    UIDrawText("Direction");
    UIMoveCursor(xpos, 0);
    UIDrawText("x");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_scene_light.direction[0]), -FLT_MAX, FLT_MAX, 0.05f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("y");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_scene_light.direction[1]), -FLT_MAX, FLT_MAX, 0.05f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("z");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_scene_light.direction[2]), -FLT_MAX, FLT_MAX, 0.05f, 100);

    UIMoveCursor(xpos, 35);
    UIDrawText("Penumbra");
    UIMoveCursor(xpos + 165, -20);
    UIDragFloat(&(g_scene_light.penumbra), 0, 1.0f, 0.001f, 200);
    UIMoveCursor(xpos, 5);
    UIDrawText("Angle");
    UIMoveCursor(xpos + 165, -20);
    UIDragFloat(&(g_scene_light.angle), 0, 360.0f, 0.1f, 200);
    UIMoveCursor(xpos, 5);
    
    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 70);
    if (UIButton("Submit", button_width)) {
        SubmitNamedLight(g_scene_light, g_light_name);
        g_scene_light = (SceneLight){ 0 };
        memset(g_light_name, 0, MAX_LIGHT_NAME_SIZE);
        strcpy(g_light_name, "Untitled Light");
        return 0;
    }
    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 40);
    if (UIButton("Cancel", button_width)) return 0;
    return -1;
}

int add_cube_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    float width = 385;
    float height = 340;
    float xpos = x + ((w - width) / 2.0f);
    float ypos = y + ((h - height) / 2.0f);
    float button_width = 200;
    UISetPosition(0, 0);
    UISetCursor(0, ypos + 10);
    DrawRectangle(xpos, ypos, width, height, MappedColor(PANEL_BG_COLOR));
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Add Cube") / 2), 0);
    UIDrawText("Add Cube");

    UIMoveCursor(xpos, 15);
    UITextInput("Object Name", g_object_name, MAX_MESH_NAME_SIZE, width - 20);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Position") / 2) - 10, 0);
    UIDrawText("Position");
    UIMoveCursor(xpos, 0);
    UIDrawText("x");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_cube_position[0]), -FLT_MAX, FLT_MAX, 0.1f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("y");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_cube_position[1]), -FLT_MAX, FLT_MAX, 0.1f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("z");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_cube_position[2]), -FLT_MAX, FLT_MAX, 0.1f, 100);

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Scale") / 2) - 10, 0);
    UIDrawText("Scale");
    UIMoveCursor(xpos, 0);
    UIDrawText("x");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_cube_scale[0]), -FLT_MAX, FLT_MAX, 0.1f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("y");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_cube_scale[1]), -FLT_MAX, FLT_MAX, 0.1f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("z");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_cube_scale[2]), -FLT_MAX, FLT_MAX, 0.1f, 100);

    UIMoveCursor(xpos + 2, 30);
    UIDrawText("Select Material");
    UIMoveCursor(xpos + 110, -20);
    UIDropdownMenu(width - 130, NumMaterials(), MaterialNameReference(0), DropdownSelectMaterial, &g_dummy_triangle);

    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 70);
    if (UIButton("Submit", button_width)) {
        size_t vertex_base = NumVertices();
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
        SubmitTriangle((Triangle){ vertex_base + 3, vertex_base + 1, vertex_base + 0, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        SubmitTriangle((Triangle){ vertex_base + 3, vertex_base + 2, vertex_base + 1, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        SubmitTriangle((Triangle){ vertex_base + 2, vertex_base + 5, vertex_base + 1, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        SubmitTriangle((Triangle){ vertex_base + 2, vertex_base + 6, vertex_base + 5, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        SubmitTriangle((Triangle){ vertex_base + 7, vertex_base + 2, vertex_base + 3, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        SubmitTriangle((Triangle){ vertex_base + 7, vertex_base + 6, vertex_base + 2, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        SubmitTriangle((Triangle){ vertex_base + 3, vertex_base + 0, vertex_base + 4, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        SubmitTriangle((Triangle){ vertex_base + 4, vertex_base + 7, vertex_base + 3, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        SubmitTriangle((Triangle){ vertex_base + 5, vertex_base + 6, vertex_base + 7, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        SubmitTriangle((Triangle){ vertex_base + 7, vertex_base + 4, vertex_base + 5, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        SubmitTriangle((Triangle){ vertex_base + 0, vertex_base + 1, vertex_base + 5, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        SubmitTriangle((Triangle){ vertex_base + 5, vertex_base + 4, vertex_base + 0, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, g_dummy_triangle.material });
        g_dummy_triangle = (Triangle){ 0 };
        vec3 extent;
        glm_vec3_scale(g_cube_scale, 0.5f, extent);
        SubmitMeshDescriptor((MeshDescriptor){
            FALSE, vertex_base, NumVertices() - 1, { 0 }, INLINEV3(extent), { 0 }, { 0 },
            { 1.0f, 1.0f, 1.0f }, GLM_MAT4_IDENTITY_INIT }, g_object_name);
        memset(g_object_name, 0, MAX_MESH_NAME_SIZE);
        strcpy(g_object_name, "Untitled Object");
        return 0;
    }
    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 40);
    if (UIButton("Cancel", button_width)) return 0;
    return -1;
}

int add_sim_object_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
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

int add_fluid_force_stage_0(size_t x, size_t y, size_t w, size_t h) {
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
    UITextInput("Force Name", g_force_name, MAX_FORCE_NAME_SIZE, width - 20);

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
    UIDragFloat(&(g_fluid_force.force[0]), 0, FLT_MAX, 0.001f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("y");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_fluid_force.force[1]), 0, FLT_MAX, 0.001f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("z");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_fluid_force.force[2]), 0, FLT_MAX, 0.001f, 100);

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

int add_fluid_source_stage_0(size_t x, size_t y, size_t w, size_t h) {
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
    UITextInput("Source Name", g_source_name, MAX_SOURCE_NAME_SIZE, width - 20);

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

Popup* GenerateEmptyPopup() {
    return EZ_ALLOC(1, sizeof(Popup));
}

void CleanPopup(Popup* popup) {
    if (popup->options != 0)
        for (size_t i = 0; i < popup->options; i++)
            CleanPopup(((Popup**)popup->results)[i]);
    if (popup->options > 0) EZ_FREE(popup->results);
    EZ_FREE(popup);
}

Popup* GenerateAddObjectPopup() {
    Popup* popup = GenerateEmptyPopup();
    popup->options = 3;
    popup->behavior = add_object_popup_stage_0;
    popup->results = EZ_ALLOC(popup->options, sizeof(Popup*));
    PopupFunction stage_1[] = {add_material_popup_stage_0, add_light_popup_stage_0, add_cube_popup_stage_0};
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
