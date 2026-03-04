#include "popup.h"
#include "data/input.h"
#include "data/colors.h"
#include <easylogger.h>
#include "ui/ui.h"
#include "renderer/renderer.h"
#include <easymemory.h>

SceneLight g_scene_light = { 0 };
SurfaceMaterial g_material = { 0 };
vec3 g_cube_position = { 0 };
vec3 g_cube_scale = { 1.0, 1.0, 1.0 };

int add_object_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    float width = 250;
    float height = 280;
    float xpos = x + ((w - width) / 2.0f);
    float ypos = y + ((h - height) / 2.0f);
    float button_width = 200;
    UISetPosition(0, 0);
    UISetCursor(0, ypos + 10);
    DrawRectangle(xpos, ypos, width, height, MappedColor(PANEL_BG_COLOR));
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Add to Scene...") / 2), 0);
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
    float width = 385;
    float height = 500;
    float xpos = x + ((w - width) / 2.0f);
    float ypos = y + ((h - height) / 2.0f);
    float button_width = 200;
    UISetPosition(0, 0);
    UISetCursor(0, ypos + 10);
    DrawRectangle(xpos, ypos, width, height, MappedColor(PANEL_BG_COLOR));
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Add Material") / 2), 0);
    UIDrawText("Add Material");

    UIMoveCursor(0, 15);
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Emission") / 2) - 10, 0);
    UIDrawText("Emission");
    UIMoveCursor(xpos, 0);
    UIDrawText("r");
    UIMoveCursor(xpos + 15, -20);
    UIDragFloat(&(g_material.emission[0]), 0, 1.0f, 0.05f, 100);
    UIMoveCursor(xpos + 125, -20);
    UIDrawText("g");
    UIMoveCursor(xpos + 140, -20);
    UIDragFloat(&(g_material.emission[1]), 0, 1.0f, 0.05f, 100);
    UIMoveCursor(xpos + 250, -20);
    UIDrawText("b");
    UIMoveCursor(xpos + 265, -20);
    UIDragFloat(&(g_material.emission[2]), 0, 1.0f, 0.05f, 100);

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
    UIDragFloat(&(g_material.ior), 0, 1.0f, 0.01f, 200);
    UIMoveCursor(xpos, 5);
    UIDrawText("Shininess");
    UIMoveCursor(xpos + 165, -20);
    UIDragFloat(&(g_material.shiny), 0, FLT_MAX, 0.01f, 200);
    UIMoveCursor(xpos, 5);

    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 70);
    if (UIButton("Submit", button_width)) {
        SubmitMaterial(g_material);
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
        SubmitLight(g_scene_light);
        return 0;
    }
    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 40);
    if (UIButton("Cancel", button_width)) return 0;
    return -1;
}

int add_cube_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    float width = 385;
    float height = 300;
    float xpos = x + ((w - width) / 2.0f);
    float ypos = y + ((h - height) / 2.0f);
    float button_width = 200;
    UISetPosition(0, 0);
    UISetCursor(0, ypos + 10);
    DrawRectangle(xpos, ypos, width, height, MappedColor(PANEL_BG_COLOR));
    UIMoveCursor(xpos + (width / 2) - (UITextWidth("Add Cube") / 2), 0);
    UIDrawText("Add Cube");

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

    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 70);
    if (UIButton("Submit", button_width)) {
        // bottom face
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            0
        });
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            0
        });

        // top face
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            0
        });
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            0
        });

        // right face
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            0
        });
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            0
        });

        // left face
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            0
        });
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            0
        });

        // front face
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            0
        });
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] + g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            0
        });

        // back face
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            0
        });
        SubmitTriangle((Triangle){
            0, 0, 0,
            (uint32_t)-1, (uint32_t)-1, (uint32_t)-1,
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] - g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] - g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            //{g_cube_position[0] - g_cube_scale[0]/2.0f, g_cube_position[1] + g_cube_scale[1]/2.0f, g_cube_position[2] + g_cube_scale[2]/2.0f},
            0
        });
        return 0;
    }
    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 40);
    if (UIButton("Cancel", button_width)) return 0;
    return -1;

    UISetCursor(xpos + (width / 2) - (button_width / 2), ypos + height - 70);
    if (UIButton("Submit", button_width)) {
        SubmitLight(g_scene_light);
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
