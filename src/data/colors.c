#include "colors.h"
#include "core/log.h"

ColorMap g_color_map = { 0 };

void InitializeColors() {
    g_color_map.colors[PANEL_BG_COLOR] = (Color){ 20, 20, 20, 255 };
    g_color_map.colors[PANEL_DIVIDER_COLOR] = (Color){ 100, 100, 100, 255 };
    g_color_map.colors[PANEL_DIVIDER_HOVER_COLOR] = (Color){ 100, 220, 220, 255 };
    g_color_map.colors[PANEL_DIVIDER_ACTIVE_COLOR] = (Color){ 100, 180, 180, 255 };
    g_color_map.colors[UI_DRAG_FLOAT_COLOR] = (Color){ 75, 75, 75, 255 };
    g_color_map.initialized = TRUE;
}

Color MappedColor(ColorKey key) {
    LOG_ASSERT(key < NUMCOLORS, "Invalid color code");
    return g_color_map.colors[key];
}