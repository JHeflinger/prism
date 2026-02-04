#include "edit.h"
#include "renderer/renderer.h"
#include <easylogger.h>

typedef enum {
    EDIT_MATERIAL,
    EDIT_LIGHT,
    EDIT_SINGLE_TRIANGLE
} EditType;

size_t g_edit_item_index = 0;
BOOL g_item_selected = FALSE;
EditType g_edit_type = EDIT_MATERIAL;

void SetEditMaterial(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_MATERIAL;
}

void SetEditLight(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_LIGHT;
}

void SetEditTriangle(size_t index) {
    g_item_selected = TRUE;
    g_edit_item_index = index;
    g_edit_type = EDIT_SINGLE_TRIANGLE;
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
            UIMoveCursor((width / 2) - (UITextWidth("Emission") / 2) - 10, 0);
            UIDrawText("Emission");
            UIMoveCursor(0, 5);
            UIDrawText("r");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(matref->emission[0]), 0, 1.0f, 0.05f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("g");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(matref->emission[1]), 0, 1.0f, 0.05f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("b");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(matref->emission[2]), 0, 1.0f, 0.05f, component_width);
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
            UIDrawText("Refraction Index");
            UIMoveCursor(140, -20);
            edited |= UIDragFloat(&(matref->ior), 0, 1.0f, 0.01f, sboxwidth);
            UIMoveCursor(0, 5);
            UIDrawText("Shininess");
            UIMoveCursor(140, -20);
            edited |= UIDragFloat(&(matref->shiny), 0, FLT_MAX, 0.01f, sboxwidth);
            UIMoveCursor(0, 5);
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
            PointLight* lref = LightReference(g_edit_item_index);
            float component_width = (width - 20 - (3 * 15) - (2 * 10)) / 3.0f;
            UIMoveCursor((width - 20 - UITextWidth("Edit Light")) / 2.0f, 0);
            UIDrawText("Edit Light");
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
            UIMoveCursor((width / 2) - (UITextWidth("Ambient") / 2) - 10, 0);
            UIDrawText("Ambient");
            UIMoveCursor(0, 5);
            UIDrawText("r");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(lref->ambient[0]), 0.0f, 1.0f, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("g");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(lref->ambient[1]), 0.0f, 1.0f, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("b");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(lref->ambient[2]), 0.0f, 1.0f, 0.1f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Diffuse") / 2) - 10, 0);
            UIDrawText("Diffuse");
            UIMoveCursor(0, 5);
            UIDrawText("r");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(lref->diffuse[0]), 0.0f, 1.0f, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("g");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(lref->diffuse[1]), 0.0f, 1.0f, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("b");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(lref->diffuse[2]), 0.0f, 1.0f, 0.1f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Specular") / 2) - 10, 0);
            UIDrawText("Specular");
            UIMoveCursor(0, 5);
            UIDrawText("r");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(lref->specular[0]), 0.0f, 1.0f, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("g");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(lref->specular[1]), 0.0f, 1.0f, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("b");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(lref->specular[2]), 0.0f, 1.0f, 0.1f, component_width);
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
            edited |= UIDragFloat(&(tref->a[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(tref->a[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(tref->a[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(tref->b[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(tref->b[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(tref->b[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            edited |= UIDragFloat(&(tref->c[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(tref->c[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(tref->c[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(0, 15);
            UIMoveCursor((width / 2) - (UITextWidth("Move Face") / 2) - 10, 0);
            UIDrawText("Move Face");
            UIMoveCursor(0, 5);
            UIDrawText("x");
            UIMoveCursor(15, -20);
            vec3 old_a = {tref->a[0], tref->a[1], tref->a[2]};
            edited |= UIDragFloat(&(tref->a[0]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor(component_width + 25, -20);
            UIDrawText("y");
            UIMoveCursor(component_width + 40, -20);
            edited |= UIDragFloat(&(tref->a[1]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            UIMoveCursor((2*component_width) + 50, -20);
            UIDrawText("z");
            UIMoveCursor((2*component_width) + 65, -20);
            edited |= UIDragFloat(&(tref->a[2]), -FLT_MAX, FLT_MAX, 0.1f, component_width);
            glm_vec3_sub(tref->a, old_a, old_a);
            glm_vec3_add(tref->b, old_a, tref->b);
            glm_vec3_add(tref->c, old_a, tref->c);
            if (edited) {
                RecalculateTriangleBB(g_edit_item_index);
                UpdateTriangles();
            }
            if (UIGetCursor().y + 60 < height) {
                UISetCursor(UIGetCursor().x, height - 60);
            }
            UIMoveCursor((width - 20 - 200) / 2.0f, 0);
            if (UIButton("Delete", 200)) EZ_WARN("This functionality is not implemented yet");
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
