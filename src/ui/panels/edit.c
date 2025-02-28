#include "edit.h"
#include "data/config.h"
#include "core/log.h"

typedef enum {
    EDIT_MATERIAL,
    EDIT_LIGHT
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

void DrawEditPanel(float width, float height) {
    if (g_item_selected) {
        if (g_edit_type == EDIT_MATERIAL) {

        } else if (g_edit_type == EDIT_LIGHT) {

        } else {
            LOG_FATAL("Unhandled edit type detected");
        }
    } else {
        UISetCursor((width - UITextWidth("No Selected Element"))/2.0f, height / 2.0f - 20);
        UIDrawText("No Selected Element");
    }
}

void ConfigureEditPanel(Panel* panel) {
    SetupPanel(panel, "Edit Selected");
    panel->draw = DrawEditPanel;
}
