#include "colors.h"
#include <easylogger.h>

ColorMap g_color_map = { 0 };

void InitializeColors() {
    g_color_map.colors[PANEL_BG_COLOR] = (Color){ 35, 35, 35, 255 };
    g_color_map.colors[PANEL_DIVIDER_COLOR] = (Color){ 100, 100, 100, 255 };
    g_color_map.colors[PANEL_DIVIDER_HOVER_COLOR] = (Color){ 100, 220, 220, 255 };
    g_color_map.colors[PANEL_DIVIDER_ACTIVE_COLOR] = (Color){ 100, 180, 180, 255 };
    g_color_map.colors[UI_DRAG_FLOAT_COLOR] = (Color){ 75, 75, 75, 255 };
    g_color_map.colors[UI_CHECKBOX_COLOR] = (Color){ 75, 75, 75, 255 };
	g_color_map.colors[UI_CHECKMARK_COLOR] = (Color){ 25, 180, 75, 255 };
    g_color_map.colors[UI_DRAG_INT_COLOR] = (Color){ 75, 75, 75, 255 };
    g_color_map.colors[PANEL_NB_COLOR] = (Color){ 50, 50, 50, 255 };
    g_color_map.colors[PANEL_NBG_COLOR] = (Color){ 65, 65, 65, 255 };
    g_color_map.colors[PANEL_BTN_BG_COLOR] = (Color){ 75, 75, 75, 255 };
    g_color_map.colors[PANEL_BTN_HVR_COLOR] = (Color){ 95, 95, 95, 255 };
    g_color_map.colors[PANEL_BTN_PRS_COLOR] = (Color){ 65, 65, 65, 255 };
    g_color_map.colors[UI_DIVIDER_COLOR] = (Color){ 100, 100, 100, 255 };
    g_color_map.colors[UI_TEXT_COLOR] = (Color){ 255, 255, 255, 255 };
    g_color_map.initialized = TRUE;
}

Color MappedColor(ColorKey key) {
    EZ_ASSERT(key < NUMCOLORS, "Invalid color code");
    return g_color_map.colors[key];
}
