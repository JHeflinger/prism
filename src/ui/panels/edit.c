#include "edit.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"
#include "renderer/rmath.h"
#include "ui/shared.h"
#include <easylogger.h>
#include <raymath.h>

typedef enum {
    EDIT_MATERIAL,
    EDIT_LIGHT,
    EDIT_SINGLE_TRIANGLE,
    EDIT_SINGLE_VERTEX,
    EDIT_SINGLE_FORCE,
    EDIT_SINGLE_SOURCE,
    EDIT_MESH,
} EditType;

size_t g_edit_item_index = 0;
BOOL g_item_selected = FALSE;
EditType g_edit_type = EDIT_MATERIAL;
const char* g_light_types[] = { "Directional", "Spot", "Point" };

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

void DrawEditPanel(float width, float height) {
    if (g_item_selected) {
        if (g_edit_type == EDIT_MATERIAL) {
            BOOL edited = FALSE;
            SurfaceMaterial* matref = MaterialReference(g_edit_item_index);
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            UIMoveCursor((width - 20 - UITextWidth("Edit Material")) / 2.0f, 0);
            UIDrawText("Edit Material");
            UIMoveCursor(0, 15);
            UITextInput("Name", MaterialName(g_edit_item_index), MAX_MATERIAL_NAME_SIZE, width - 20);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Emission") / 2) - 10, 0);
            UIDrawText("Emission");
            UIMoveCursor(0, 5);
            UIDrawText("r");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(matref->emission[0]), 0, FLT_MAX, 0.05f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("g");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(matref->emission[1]), 0, FLT_MAX, 0.05f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("b");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(matref->emission[2]), 0, FLT_MAX, 0.05f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Absorbtion") / 2) - 10, 0);
            UIDrawText("Absorbtion");
            UIMoveCursor(0, 5);
            UIDrawText("r");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(matref->absorbtion[0]), 0, FLT_MAX, 0.001f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("g");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(matref->absorbtion[1]), 0, FLT_MAX, 0.001f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("b");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(matref->absorbtion[2]), 0, FLT_MAX, 0.001f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Dispersion") / 2) - 10, 0);
            UIDrawText("Dispersion");
            UIMoveCursor(0, 5);
            UIDrawText("r");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(matref->dispersion[0]), 0, FLT_MAX, 0.001f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("g");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(matref->dispersion[1]), 0, FLT_MAX, 0.001f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("b");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(matref->dispersion[2]), 0, FLT_MAX, 0.001f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Ambient") / 2) - 10, 0);
            UIDrawText("Ambient");
            UIMoveCursor(0, 5);
            UIDrawText("r");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(matref->ambient[0]), 0, 1.0f, 0.05f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("g");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(matref->ambient[1]), 0, 1.0f, 0.05f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("b");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(matref->ambient[2]), 0, 1.0f, 0.05f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Diffuse") / 2) - 10, 0);
            UIDrawText("Diffuse");
            UIMoveCursor(0, 5);
            UIDrawText("r");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(matref->diffuse[0]), 0, 1.0f, 0.05f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("g");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(matref->diffuse[1]), 0, 1.0f, 0.05f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("b");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(matref->diffuse[2]), 0, 1.0f, 0.05f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Specular") / 2) - 10, 0);
            UIDrawText("Specular");
            UIMoveCursor(0, 5);
            UIDrawText("r");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(matref->specular[0]), 0, 1.0f, 0.05f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("g");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(matref->specular[1]), 0, 1.0f, 0.05f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("b");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(matref->specular[2]), 0, 1.0f, 0.05f, component_width); 
            UIMoveCursor(0, 35);
            float sboxwidth = width - 20 - 140;
            UIDrawText("Index of Refraction");
            UIMoveCursor(140, -20);
            edited |= UIDragFloat(&(matref->ior), 0, FLT_MAX, 0.01f, sboxwidth);
            UIMoveCursor(0, 5);
            UIDrawText("Shininess");
            UIMoveCursor(140, -20);
            edited |= UIDragFloat(&(matref->shiny), 0, FLT_MAX, 0.01f, sboxwidth);
            UIMoveCursor(0, 35);
            UIDrawText("Lighting Model");
            UIMoveCursor(140, -20);
            UIDropdownMenu(sboxwidth, 3, LightModelLabels(), DropdownSelectLightModel, matref);
            if (edited) UpdateMaterials();
            if (g_edit_item_index != 0) {
                if (UIGetCursor().y + 60 < height) {
                    UISetCursor(UIGetCursor().x, height - 60);
                }
                UIMoveCursor((width - 20 - 200) / 2.0f, 0);
                if (UIButton("Delete", 200)) EZ_WARN("This functionality is not implemented yet");
            }
        } else if (g_edit_type == EDIT_LIGHT) {
            BOOL edited = FALSE;
            SceneLight* lref = LightReference(g_edit_item_index);
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            int light_type = 0;
            if (lref->direction[0] == 0 && lref->direction[1] == 0 && lref->direction[2] == 0) {
                light_type = 2;
            } else if (lref->angle != 0) {
                light_type = 1;
            }
            UIMoveCursor((width - 20 - UITextWidth("Edit Light (%s)", g_light_types[light_type])) / 2.0f, 0);
            UIDrawText("Edit Light (%s)", g_light_types[light_type]);
            UIMoveCursor(0, 15);
            UITextInput("Name", LightName(g_edit_item_index), MAX_LIGHT_NAME_SIZE, width - 20);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Position") / 2) - 10, 0);
            UIDrawText("Position");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(lref->position[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(lref->position[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(lref->position[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Intensity") / 2) - 10, 0);
            UIDrawText("Intensity");
            UIMoveCursor(0, 5);
            UIDrawText("r");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(lref->color[0]), 0.0f, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("g");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(lref->color[1]), 0.0f, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("b");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(lref->color[2]), 0.0f, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Direction") / 2) - 10, 0);
            UIDrawText("Direction");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(lref->direction[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(lref->direction[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(lref->direction[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 35);
            float sboxwidth = width - 20 - 140;
            UIDrawText("Penumbra");
            UIMoveCursor(140, -20);
            edited |= UIDragFloat(&(lref->penumbra), 0, 1.0f, 0.01f, sboxwidth);
            UIMoveCursor(0, 5);
            UIDrawText("Opening Angle");
            UIMoveCursor(140, -20);
            edited |= UIDragFloat(&(lref->angle), 0, FLT_MAX, 0.1f, sboxwidth);
            if (edited) UpdateLights();
            if (UIGetCursor().y + 60 < height) {
                UISetCursor(UIGetCursor().x, height - 60);
            }
            UIMoveCursor((width - 20 - 200) / 2.0f, 0);
            if (UIButton("Delete", 200)) EZ_WARN("This functionality is not implemented yet");
        } else if (g_edit_type == EDIT_SINGLE_TRIANGLE) {
            BOOL edited = FALSE;
            Triangle* tref = TriangleReference(g_edit_item_index);
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            UIMoveCursor((width - 20 - UITextWidth("Edit Face")) / 2.0f, 0);
            UIDrawText("Edit Face");
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Vertices") / 2) - 10, 0);
            UIDrawText("Vertices");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(VertexReference(tref->a)[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(VertexReference(tref->a)[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(VertexReference(tref->a)[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(VertexReference(tref->b)[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(VertexReference(tref->b)[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(VertexReference(tref->b)[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(VertexReference(tref->c)[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(VertexReference(tref->c)[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(VertexReference(tref->c)[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Move Face") / 2) - 10, 0);
            UIDrawText("Move Face");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            vec3 old_a;
            glm_vec3_copy(VertexReference(tref->a), old_a);
            edited |= UIDragFloat(&(VertexReference(tref->a)[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(VertexReference(tref->a)[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(VertexReference(tref->a)[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 35);
            UIDrawText("Material");
            float sboxwidth = width - 20 - 140;
            UIMoveCursor(140, -20);
            UIDropdownMenu(sboxwidth, NumMaterials(), MaterialNameReference(0), DropdownSelectMaterial, tref);
            vec3 adiff;
            glm_vec3_sub(VertexReference(tref->a), old_a, adiff);
            glm_vec3_add(VertexReference(tref->b), adiff, VertexReference(tref->b));
            glm_vec3_add(VertexReference(tref->c), adiff, VertexReference(tref->c));
            if (edited) UpdateVertices();
            if (UIGetCursor().y + 60 < height) {
                UISetCursor(UIGetCursor().x, height - 60);
            }
            UIMoveCursor((width - 20 - 200) / 2.0f, 0);
            if (UIButton("Delete", 200)) EZ_WARN("This functionality is not implemented yet");
        } else if (g_edit_type == EDIT_SINGLE_VERTEX) {
            BOOL edited = FALSE;
            float* vref = VertexReference(g_edit_item_index);
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            UIMoveCursor((width - 20 - UITextWidth("Edit Vertex")) / 2.0f, 0);
            UIDrawText("Edit Vertex");
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Position") / 2) - 10, 0);
            UIDrawText("Position");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(vref[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(vref[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(vref[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 15);
            UIDrawText("(Deform) Lock");
            UIMoveCursor(140, -20);
            BOOL locked = VertexLocked(g_edit_item_index);
            UICheckbox(&locked);
            edited |= locked != VertexLocked(g_edit_item_index);
            if (edited) {
                if (locked) LockVertex(g_edit_item_index);
                else UnlockVertex(g_edit_item_index);
                UpdateVertices();
            }
            if (UIGetCursor().y + 60 < height) {
                UISetCursor(UIGetCursor().x, height - 60);
            }
            UIMoveCursor((width - 20 - 200) / 2.0f, 0);
            if (UIButton("Delete", 200)) EZ_WARN("This functionality is not implemented yet");
        } else if (g_edit_type == EDIT_SINGLE_SOURCE) {
            BOOL edited = FALSE;
            FluidSimulation* fsim = &(RendererGeometry()->fluid);
            FluidSource* sourceref = SourceReference(g_edit_item_index);
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            UIMoveCursor((width - 20 - UITextWidth("Edit Source")) / 2.0f, 0);
            UIDrawText("Edit Source");
            UIMoveCursor(0, 15);
            UITextInput("Name", *(SourceNameReference(g_edit_item_index)), MAX_SOURCE_NAME_SIZE, width - 20);
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
        } else if (g_edit_type == EDIT_SINGLE_FORCE) {
            BOOL edited = FALSE;
            FluidSimulation* fsim = &(RendererGeometry()->fluid);
            FluidForce* forceref = ForceReference(g_edit_item_index);
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            UIMoveCursor((width - 20 - UITextWidth("Edit Force")) / 2.0f, 0);
            UIDrawText("Edit Force");
            UIMoveCursor(0, 15);
            UITextInput("Name", *(ForceNameReference(g_edit_item_index)), MAX_FORCE_NAME_SIZE, width - 20);
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
        } else if (g_edit_type == EDIT_MESH) {
            MeshDescriptor* md = MeshReference(g_edit_item_index);
            BOOL edited = FALSE;
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            UIMoveCursor((width - 20 - UITextWidth("Edit Object")) / 2.0f, 0);
            UIDrawText("Edit Object");
            UIMoveCursor(0, 15);
            UITextInput("Name", *(MeshNameReference(g_edit_item_index)), MAX_MESH_NAME_SIZE, width - 20);
            UIMoveCursor(0, 15);
            UIDrawText("Disable");
            UIMoveCursor(140, -20);
            BOOL disabled = md->disabled != 0;
            UICheckbox(&disabled);
            edited |= disabled != (md->disabled != 0);
            md->disabled = disabled;
            if (md->pose != (uint32_t)-1) {
                MeshAnimation* anim = AnimationReference(md->pose);
                float sboxwidth = width - 20 - 140;
                UIMoveCursor(0, 15);
                UIDrawText("Animation");
                UIMoveCursor(140, -20);
                UIDropdownMenu(sboxwidth, anim->animations.size, anim->names.data, DropdownSelectAnimation, anim);
                UIMoveCursor(0, 5);
                UIDrawText("Transition");
                UIMoveCursor(140, -20);
                UIDragFloat(&(anim->bduration), 0, FLT_MAX, 0.01f, sboxwidth);
                UIMoveCursor(0, 5);
                UIDrawText("Time");
                UIMoveCursor(140, -20);
                UIDragFloat(&(anim->time), 0, FLT_MAX, 0.01f, sboxwidth);
                UIMoveCursor(0, 5);
                UIDrawText("Loop");
                UIMoveCursor(140, -20);
                UICheckbox(&(anim->looping));
                UIMoveCursor(0, 5);
                UIDrawText("Play");
                UIMoveCursor(140, -20);
                UICheckbox(&(anim->playing));
                UIMoveCursor(0, 5);
                UIDrawText("Enable");
                UIMoveCursor(140, -20);
                UICheckbox(&(anim->enable));
            }
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Translation") / 2) - 10, 0);
            UIDrawText("Translation");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(md->translate[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(md->translate[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(md->translate[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Rotation") / 2) - 10, 0);
            UIDrawText("Rotation");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(md->rotate[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(md->rotate[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(md->rotate[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Scale") / 2) - 10, 0);
            UIDrawText("Scale");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(md->scale[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(md->scale[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(md->scale[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            if (edited) UpdateObjectTransform(g_edit_item_index);
        } else {
            EZ_FATAL("Unhandled edit type detected");
        }
    } else {
        UISetCursor((width - UITextWidth("No Selected Element"))/2.0f, height / 2.0f - 20);
        UIDrawText("No Selected Element");
    }
}

Panel GenerateEditPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Edit Selected");
	p.draw = DrawEditPanel;
	return p;
}
