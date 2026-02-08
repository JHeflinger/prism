#include "overview.h"
#include "renderer/renderer.h"
#include "data/strings.h"
#include "ui/panels/edit.h"

size_t g_num_lights_tracker = 0;
ARRLIST_DynamicString g_lights_list = { 0 };

void DrawOverviewPanel(float width, float height) {
    UIDrawText("Add To Scene...");
    UIMoveCursor(width - 45, -20);
    if (UIButton("+", 0)) {
        UIPopup(GenerateAddObjectPopup());
    }
    UIDivider(width - 20);
    UIDropList("Materials", width - 20, NumMaterials(), MaterialNameReference(0), SetEditMaterial);
    UIMoveCursor(0, 5);
    UIDropList("Lights", width - 20, g_num_lights_tracker, g_lights_list.data, SetEditLight);
}

void UpdateOverviewPanel(float width, float height) {
    if (NumLights() != g_num_lights_tracker) {
        for (size_t i = 0; i < g_num_lights_tracker; i++)
            EZ_FREE(g_lights_list.data[i]);
        ARRLIST_DynamicString_clear(&g_lights_list);
        g_num_lights_tracker = NumLights();
        for (size_t i = 0; i < g_num_lights_tracker; i++) {
            char* string = EZ_ALLOC(64, sizeof(char));
            snprintf(string, 64, "light #%d", (int)i);
            ARRLIST_DynamicString_add(&g_lights_list, string);
        }
    }
}

void CleanOverviewPanel() {
    for (size_t i = 0; i < g_num_lights_tracker; i++)
        EZ_FREE(g_lights_list.data[i]);
    ARRLIST_DynamicString_clear(&g_lights_list);
}

Panel GenerateOverviewPanel() {
	Panel p = { 0 };
    SetupPanel(&p, "Overview");
    p.draw = DrawOverviewPanel;
    p.clean = CleanOverviewPanel;
    p.update = UpdateOverviewPanel;
	return p;
}
