#include "edit.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"
#include "ui/shared.h"
#include <ui/extra.h>
#include <util/logger.h>

#define LEFT_COLUMN_WIDTH 150

typedef enum {
    EDIT_MATERIAL,
    EDIT_LIGHT,
    EDIT_SINGLE_TRIANGLE,
    EDIT_SINGLE_VERTEX,
    EDIT_SINGLE_FORCE,
    EDIT_SINGLE_SOURCE,
    EDIT_MESH,
    EDIT_CAMERA,
} EditType;

static size_t g_edit_item_index = 0;
static BOOL g_item_selected = FALSE;
static EditType g_edit_type = EDIT_MATERIAL;
static const char* g_light_types[] = { "Directional", "Spot", "Point" };

void DrawEditMaterial(SurfaceMaterial* matref, char* name, float width, float height) {
    BOOL edited = FALSE;
    UIMoveCursor((width - 20 - UITextWidth("Edit Material")) / 2.0f, 0);
    UIDrawText("Edit Material");
    UIMoveCursor(0, 15);
    UITextInput("Name", name, MAX_MATERIAL_NAME_SIZE, width - 20, FALSE);
    UIMoveCursor(0, 15);
    UIColumnHeader("Emission", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->emission[0]), &(matref->emission[1]), &(matref->emission[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.05f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Absorbtion", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->absorbtion[0]), &(matref->absorbtion[1]), &(matref->absorbtion[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.001f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Dispersion", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->dispersion[0]), &(matref->dispersion[1]), &(matref->dispersion[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.001f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Ambient", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->ambient[0]), &(matref->ambient[1]), &(matref->ambient[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = 1.0f }, (UIMultiValue){ ._float = 0.05f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Diffuse", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->diffuse[0]), &(matref->diffuse[1]), &(matref->diffuse[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = 1.0f }, (UIMultiValue){ ._float = 0.05f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Specular", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->specular[0]), &(matref->specular[1]), &(matref->specular[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = 1.0f }, (UIMultiValue){ ._float = 0.05f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Scattering", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("rgb", UI_FLOATS, &(matref->scattering[0]), &(matref->scattering[1]), &(matref->scattering[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = 1.0f }, (UIMultiValue){ ._float = 0.05f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIMoveCursor(0, 35);
    float sboxwidth = width - 20 - LEFT_COLUMN_WIDTH;
    UIColumnHeader("Index of Refraction", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(matref->ior), 0, FLT_MAX, 0.01f, sboxwidth);
    UIColumnHeader("Shininess", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(matref->shiny), 0, FLT_MAX, 0.01f, sboxwidth);
    UIColumnHeader("Anisotropy", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(matref->anisotropy), 0, FLT_MAX, 0.01f, sboxwidth);
    UIColumnHeader("Transport Scale", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(matref->scale), 0, FLT_MAX, 0.01f, sboxwidth);
    UIMoveCursor(0, 35);
    UIColumnHeader("Lighting Model", LEFT_COLUMN_WIDTH);
    UIDropdownMenu(sboxwidth, 4, LightModelLabels(), DropdownSelectLightModel, matref);
    if (edited) UpdateMaterials();
}

void DrawEditLight(SceneLight* lref, char* name, float width, float height) {
    BOOL edited = FALSE;
    int light_type = 0;
    if (lref->direction[0] == 0 && lref->direction[1] == 0 && lref->direction[2] == 0) {
        light_type = 2;
    } else if (lref->angle != 0) {
        light_type = 1;
    }
    UIMoveCursor((width - 20 - UITextWidth("Edit Light (%s)", g_light_types[light_type])) / 2.0f, 0);
    UIDrawText("Edit Light (%s)", g_light_types[light_type]);
    UIMoveCursor(0, 15);
    UITextInput("Name", name, MAX_LIGHT_NAME_SIZE, width - 20, FALSE);
    UIMoveCursor(0, 15);
    UIColumnHeader("Position", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(lref->position[0]), &(lref->position[1]), &(lref->position[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Intensity", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(lref->color[0]), &(lref->color[1]), &(lref->color[2]), (UIMultiValue){ ._float = 0 }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 1.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Direction", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(lref->direction[0]), &(lref->direction[1]), &(lref->direction[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIMoveCursor(0, 35);
    float sboxwidth = width - 20 - LEFT_COLUMN_WIDTH;
    UIColumnHeader("Penumbra", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(lref->penumbra), 0, 1.0f, 0.01f, sboxwidth);
    UIColumnHeader("Opening Angle", LEFT_COLUMN_WIDTH);
    edited |= UIDragFloat(&(lref->angle), 0, FLT_MAX, 0.1f, sboxwidth);
    if (edited) UpdateLights();
}

void DrawEditMesh(MeshDescriptor* md, char* name, float width, float height) {
    BOOL edited = FALSE;
    UIMoveCursor((width - 20 - UITextWidth("Edit Object")) / 2.0f, 0);
    UIDrawText("Edit Object");
    UIMoveCursor(0, 15);
    UITextInput("Name", name, MAX_MESH_NAME_SIZE, width - 20, FALSE);
    UIMoveCursor(0, 15);
    if (md->pose != (uint32_t)-1) {
        MeshAnimation* anim = AnimationReference(md->pose);
        float sboxwidth = width - 20 - LEFT_COLUMN_WIDTH;
        UIMoveCursor(0, 15);
        UIColumnHeader("Animation", LEFT_COLUMN_WIDTH);
        UIDropdownMenu(sboxwidth, anim->animations.size, anim->names.data, DropdownSelectAnimation, anim);
        UIColumnHeader("Transition", LEFT_COLUMN_WIDTH);
        UIDragFloat(&(anim->bduration), 0, FLT_MAX, 0.01f, sboxwidth);
        UIColumnHeader("Time", LEFT_COLUMN_WIDTH);
        UIDragFloat(&(anim->time), 0, FLT_MAX, 0.01f, sboxwidth);
        UIColumnHeader("Loop", LEFT_COLUMN_WIDTH);
        UICheckbox(&(anim->looping));
        UIColumnHeader("Play", LEFT_COLUMN_WIDTH);
        UICheckbox(&(anim->playing));
        UIColumnHeader("Enable", LEFT_COLUMN_WIDTH);
        UICheckbox(&(anim->enable));
    }
    UIMoveCursor(0, 15);
    UIColumnHeader("Translation", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(md->translate[0]), &(md->translate[1]), &(md->translate[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Rotation", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(md->rotate[0]), &(md->rotate[1]), &(md->rotate[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
    UIColumnHeader("Scale", LEFT_COLUMN_WIDTH);
    edited |= UITriplet("xyz", UI_FLOATS, &(md->scale[0]), &(md->scale[1]), &(md->scale[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 1.0f }, width - LEFT_COLUMN_WIDTH);
    UIMoveCursor(0, 15);
    UIColumnHeader("Disable", LEFT_COLUMN_WIDTH);
    BOOL disabled = md->disabled != 0;
    UICheckbox(&disabled);
    edited |= disabled != (md->disabled != 0);
    md->disabled = disabled;
    if (edited) UpdateObjectTransform(g_edit_item_index);
}

void DrawEditCamera(SceneCamera* cref, char* name, float width, float height) {
    float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
    UIMoveCursor((width - 20 - UITextWidth("Edit Camera")) / 2.0f, 0);
    UIDrawText("Edit Camera");
    UIMoveCursor(0, 15);
    UITextInput("Name", name, MAX_CAMERA_NAME_SIZE, width - 20, FALSE);
    UIMoveCursor(0, 3);
    if (g_edit_item_index == PrimaryCameraID()) DisableUI();
    if (UIButton("Set Primary", width - 20)) {
        SetCamera(g_edit_item_index);
    }
    EnableUI();
    UIMoveCursor(0, 20);
    UIMoveCursor((width / 2) - (UITextWidth("Position") / 2) - 10, 0);
    UIDrawText("Position");
    UIMoveCursor(0, 5);
    UIDrawText("x");
    UIMoveCursor(15, -20);
    UIDragFloat(&(cref->core.position[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
    UIMoveCursor(component_width + 25, -20);
    UIDrawText("y");
    UIMoveCursor(component_width + 40, -20);
    UIDragFloat(&(cref->core.position[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
    UIMoveCursor((2*component_width) + 50, -20);
    UIDrawText("z");
    UIMoveCursor((2*component_width) + 65, -20);
    UIDragFloat(&(cref->core.position[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
    UIMoveCursor(0, 5);
    UIMoveCursor((width / 2) - (UITextWidth("Look") / 2) - 10, 0);
    UIDrawText("Look");
    UIMoveCursor(0, 5);
    UIDrawText("x");
    UIMoveCursor(15, -20);
    UIDragFloat(&(cref->core.look[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
    UIMoveCursor(component_width + 25, -20);
    UIDrawText("y");
    UIMoveCursor(component_width + 40, -20);
    UIDragFloat(&(cref->core.look[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
    UIMoveCursor((2*component_width) + 50, -20);
    UIDrawText("z");
    UIMoveCursor((2*component_width) + 65, -20);
    UIDragFloat(&(cref->core.look[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
    UIMoveCursor(0, 5);
    UIMoveCursor((width / 2) - (UITextWidth("Up") / 2) - 10, 0);
    UIDrawText("Up");
    UIMoveCursor(0, 5);
    UIDrawText("x");
    UIMoveCursor(15, -20);
    UIDragFloat(&(cref->core.up[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
    UIMoveCursor(component_width + 25, -20);
    UIDrawText("y");
    UIMoveCursor(component_width + 40, -20);
    UIDragFloat(&(cref->core.up[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
    UIMoveCursor((2*component_width) + 50, -20);
    UIDrawText("z");
    UIMoveCursor((2*component_width) + 65, -20);
    UIDragFloat(&(cref->core.up[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
    UIMoveCursor(0, 25);
    UIDrawText("Preview Only");
    UIMoveCursor(160, -20);
    BOOL preview = !(cref->config.flags & PATHTRACE_SHADER_FLAG);
    if (UICheckbox(&preview)) {
        cref->config.flags = preview ? PREVIEW_PIPELINE_FLAGS : PATHTRACE_PIPELINE_FLAGS;
    }
    UIDrawText("Grid");
    UIMoveCursor(160, -20);
    UICheckbox(&(cref->config.grid));
    UIDrawText("Wireframe");
    UIMoveCursor(160, -20);
    UICheckbox(&(cref->config.wireframe));
    UIDrawText("Smooth Normals");
    UIMoveCursor(160, -20);
    UICheckbox(&(cref->config.normals));
    UIDrawText("BVH Culling");
    UIMoveCursor(160, -20);
    UICheckbox(&(cref->config.screenspace));
    UIDrawText("Direct Lighting");
    UIMoveCursor(160, -20);
    UICheckbox(&(cref->config.direct));
    UIDrawText("Direct Lighting Only");
    UIMoveCursor(160, -20);
    UICheckbox(&(cref->config.directonly));
    UIDrawText("Scene Lights");
    UIMoveCursor(160, -20);
    UICheckbox(&(cref->config.scenelighting));
    UIDrawText("Scene Lights Only");
    UIMoveCursor(160, -20);
    UICheckbox(&(cref->config.scenelightingonly));
    UIDrawText("Scene Light Shadows");
    UIMoveCursor(160, -20);
    UICheckbox(&(cref->config.scenelightshadows));
    UIDrawText("Spectral Coloring");
    UIMoveCursor(160, -20);
    UICheckbox(&(cref->config.spectral));

    UIMoveCursor(0, 15);
    float sboxwidth = width - 20 - 160;
    UIDrawText("Debug Mode");
    UIMoveCursor(160, -20);
    size_t pp = PrimaryCameraID();
    SetCamera(g_edit_item_index);
    UIDropdownMenu(sboxwidth, 4, DebugModeLabels(), DropdownSelectDebugMode, NULL);
    SetCamera(pp);
    UIDrawText("Max Bounces");
    UIMoveCursor(160, -20);
    UIDragSize(&(cref->config.maxbounces), 0, 999999999, 1, sboxwidth);
    UIDrawText("Frame Multiplier");
    UIMoveCursor(160, -20);
    UIDragSize(&(cref->config.multiplier), 0, 999999999, 1, sboxwidth);
    UIDrawText("Whitepoint");
    UIMoveCursor(160, -20);
    UIDragFloat(&(cref->config.whitepoint), 0.01f, 999999999.0f, 0.1f, sboxwidth);
    UIDrawText("Gamma");
    UIMoveCursor(160, -20);
    UIDragFloat(&(cref->config.gamma), 0.01f, 999999999.0f, 0.1f, sboxwidth);

    SimpleCamera c = cref->core;
    SimpleCamera oldc = cref->core;
    BOOL used = FALSE;
    UIMoveCursor(0, 20.0f);
    UIDrawText("Aperature");
    UIMoveCursor(160, -20);
    UIDragFloat(&(c.aperature), 0.0f, 999999999.0f, 0.01f, sboxwidth);
    used |= UIWasJustUsed();
    UIDrawText("Focus");
    UIMoveCursor(160, -20);
    UIDragFloat(&(c.focus), 0.0f, 999999999.0f, 0.01f, sboxwidth);
    used |= UIWasJustUsed();
    UIDrawText("FOV");
    UIMoveCursor(160, -20);
    UIDragFloat(&(c.fov), 0.0f, 180.0f, 0.1f, sboxwidth);
	if (memcmp(&c, &oldc, sizeof(SimpleCamera))) cref->core = c;
    cref->config.showdof = used;
}

static void DrawEditPanel(float width, float height) {
    if (g_item_selected) {
        if (g_edit_type == EDIT_MATERIAL) {
            SurfaceMaterial* matref = MaterialReference(g_edit_item_index);
            DrawEditMaterial(matref, MaterialName(g_edit_item_index), width, height);
        } else if (g_edit_type == EDIT_LIGHT) {
            SceneLight* lref = LightReference(g_edit_item_index);
            DrawEditLight(lref, LightName(g_edit_item_index), width, height);
        } else if (g_edit_type == EDIT_SINGLE_TRIANGLE) {
            BOOL edited = FALSE;
            Triangle* tref = TriangleReference(g_edit_item_index);
            UIMoveCursor((width - 20 - UITextWidth("Edit Face")) / 2.0f, 0);
            UIDrawText("Edit Face");
            UIMoveCursor(0, 15);
            UIDividerLabeled(width, "Vertices");
            UIMoveCursor(0, 5);
            edited |= UITriplet("xyz", UI_FLOATS, &VertexReference(tref->a)[0], &VertexReference(tref->a)[1], &VertexReference(tref->a)[2], (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width);
            edited |= UITriplet("xyz", UI_FLOATS, &VertexReference(tref->b)[0], &VertexReference(tref->b)[1], &VertexReference(tref->b)[2], (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width);
            edited |= UITriplet("xyz", UI_FLOATS, &VertexReference(tref->c)[0], &VertexReference(tref->c)[1], &VertexReference(tref->c)[2], (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width);
            UIMoveCursor(0, 15);
            UIDividerLabeled(width, "Move Face");
            UIMoveCursor(0, 5);
            vec3 old_a;
            glm_vec3_copy(VertexReference(tref->a), old_a);
            edited |= UITriplet("xyz", UI_FLOATS, &VertexReference(tref->a)[0], &VertexReference(tref->a)[1], &VertexReference(tref->a)[2], (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width);
            UIMoveCursor(0, 35);
            UIColumnHeader("Material", LEFT_COLUMN_WIDTH);
            float sboxwidth = width - 20 - LEFT_COLUMN_WIDTH;
            UIDropdownMenu(sboxwidth, NumMaterials(), MaterialNameReference(0), DropdownSelectMaterial, tref);
            vec3 adiff;
            glm_vec3_sub(VertexReference(tref->a), old_a, adiff);
            glm_vec3_add(VertexReference(tref->b), adiff, VertexReference(tref->b));
            glm_vec3_add(VertexReference(tref->c), adiff, VertexReference(tref->c));
            if (edited) UpdateVertices();
        } else if (g_edit_type == EDIT_SINGLE_VERTEX) {
            BOOL edited = FALSE;
            float* vref = VertexReference(g_edit_item_index);
            UIMoveCursor((width - 20 - UITextWidth("Edit Vertex")) / 2.0f, 0);
            UIDrawText("Edit Vertex");
            UIMoveCursor(0, 15);
            UIColumnHeader("Position", LEFT_COLUMN_WIDTH);
            edited |= UITriplet("xyz", UI_FLOATS, &(vref[0]), &(vref[1]), &(vref[2]), (UIMultiValue){ ._float = -FLT_MAX }, (UIMultiValue){ ._float = FLT_MAX }, (UIMultiValue){ ._float = 0.1f }, (UIMultiValue){ ._float = 0.0f }, width - LEFT_COLUMN_WIDTH);
            UIColumnHeader("(Deform) Lock", LEFT_COLUMN_WIDTH);
            BOOL locked = VertexLocked(g_edit_item_index);
            UIMoveCursor(-4, 2);
            UICheckbox(&locked);
            edited |= locked != VertexLocked(g_edit_item_index);
            if (edited) {
                if (locked) LockVertex(g_edit_item_index);
                else UnlockVertex(g_edit_item_index);
                UpdateVertices();
            }
        } else if (g_edit_type == EDIT_SINGLE_SOURCE) {
            BOOL edited = FALSE;
            FluidSimulation* fsim = &(RendererGeometry()->fluid);
            FluidSource* sourceref = SourceReference(g_edit_item_index);
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            UIMoveCursor((width - 20 - UITextWidth("Edit Source")) / 2.0f, 0);
            UIDrawText("Edit Source");
            UIMoveCursor(0, 15);
            UITextInput("Name", *(SourceNameReference(g_edit_item_index)), MAX_SOURCE_NAME_SIZE, width - 20, FALSE);
            UIMoveCursor(0, 15);
            float sboxwidth = width - 20 - 140;
            UIDrawText("Density");
            UIMoveCursor(140, -20);
            edited |= UIDragFloat(&(sourceref->density), 0, FLT_MAX, 0.01f, sboxwidth);
            UIMoveCursor(0, 5);
            UIDrawText("Lifetime");
            UIMoveCursor(140, -20);
            edited |= UIDragFloat(&(sourceref->lifetime), 0, FLT_MAX, 0.01f, sboxwidth);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Size") / 2) - 10, 0);
            UIDrawText("Size");
            UIMoveCursor(0, 5);
            UIDrawText("w");
            UIMoveCursor(15, -20);
            edited |= UIDragSize(&(sourceref->width), 0, fsim->width - sourceref->x, 1, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("h");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragSize(&(sourceref->height), 0, fsim->height - sourceref->y, 1, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("l");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragSize(&(sourceref->length), 0, fsim->length - sourceref->z, 1, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Origin") / 2) - 10, 0);
            UIDrawText("Origin");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragSize(&(sourceref->x), 0, fsim->width - sourceref->width, 1, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragSize(&(sourceref->y), 0, fsim->height - sourceref->height, 1, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragSize(&(sourceref->z), 0, fsim->length - sourceref->length, 1, component_width);
            UIMoveCursor(0, 5);
            if (edited) UpdateSimulation();
        } else if (g_edit_type == EDIT_SINGLE_FORCE) {
            BOOL edited = FALSE;
            FluidSimulation* fsim = &(RendererGeometry()->fluid);
            FluidForce* forceref = ForceReference(g_edit_item_index);
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            UIMoveCursor((width - 20 - UITextWidth("Edit Force")) / 2.0f, 0);
            UIDrawText("Edit Force");
            UIMoveCursor(0, 15);
            UITextInput("Name", *(ForceNameReference(g_edit_item_index)), MAX_FORCE_NAME_SIZE, width - 20, FALSE);
            UIMoveCursor(0, 15);
            UIDrawText("Global Force");
            UIMoveCursor(140, -20);
            UICheckbox(&(forceref->global));
            if (forceref->global) DisableUI();
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Size") / 2) - 10, 0);
            UIDrawText("Size");
            UIMoveCursor(0, 5);
            UIDrawText("w");
            UIMoveCursor(15, -20);
            edited |= UIDragSize(&(forceref->width), 0, fsim->width - forceref->x, 1, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("h");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragSize(&(forceref->height), 0, fsim->height - forceref->y, 1, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("l");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragSize(&(forceref->length), 0, fsim->length - forceref->z, 1, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Origin") / 2) - 10, 0);
            UIDrawText("Origin");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragSize(&(forceref->x), 0, fsim->width - forceref->width, 1, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragSize(&(forceref->y), 0, fsim->height - forceref->height, 1, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragSize(&(forceref->z), 0, fsim->length - forceref->length, 1, component_width);
            UIMoveCursor(0, 15);
            EnableUI();
            UIMoveCursor((width / 2) - (UITextWidth("Force Vector") / 2) - 10, 0);
            UIDrawText("Force Vector");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(forceref->force[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(forceref->force[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(forceref->force[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 15);
            if (edited) UpdateSimulation();
        } else if (g_edit_type == EDIT_MESH) {
            MeshDescriptor* md = MeshReference(g_edit_item_index);
            DrawEditMesh(md, *(MeshNameReference(g_edit_item_index)), width, height);
        } else if (g_edit_type == EDIT_CAMERA) {
            SceneCamera* cref = GetCamera(g_edit_item_index);
            DrawEditCamera(cref, *(CameraNameReference(g_edit_item_index)), width, height);
        } else {
            EZ_FATAL("Unhandled edit type detected");
        }
    } else {
        UISetCursor((width - UITextWidth("No Selected Element"))/2.0f, height / 2.0f - 20);
        UIDrawText("No Selected Element");
    }
}

void SetEditCamera(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_CAMERA;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
}

void SetEditMaterial(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_MATERIAL;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
}

void SetEditLight(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_LIGHT;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
}

void SetEditTriangle(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_SINGLE_TRIANGLE;
    SetSelectedVertex((VertexID)-1);
    SetSelectedTriangle(index);
}

void SetEditVertex(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_SINGLE_VERTEX;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex(index);
}

void SetEditForce(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_SINGLE_FORCE;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
}

void SetEditSource(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_SINGLE_SOURCE;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
}

void SetEditMesh(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_MESH;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
}

void DeselectEditTarget() {
    g_item_selected = FALSE;
    SetSelectedTriangle((TriangleID)-1);
    SetSelectedVertex((VertexID)-1);
}

Panel GenerateEditPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Edit Selected");
    p.scrollable = TRUE;
	p.draw = DrawEditPanel;
	return p;
}
