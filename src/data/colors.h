#ifndef COLORS_H
#define COLORS_H

#include "data/config.h"
#include <raylib.h>

#define NUMCOLORS 15

typedef enum {
    PANEL_BG_COLOR = 0,
    PANEL_DIVIDER_COLOR = 1,
    PANEL_DIVIDER_HOVER_COLOR = 2,
    PANEL_DIVIDER_ACTIVE_COLOR = 3,
    UI_DRAG_FLOAT_COLOR = 4,
	UI_CHECKBOX_COLOR = 5,
	UI_CHECKMARK_COLOR = 6,
	UI_DRAG_INT_COLOR = 7,
    PANEL_NB_COLOR = 8,
    PANEL_NBG_COLOR = 9,
    PANEL_BTN_BG_COLOR = 10,
    PANEL_BTN_HVR_COLOR = 11,
    PANEL_BTN_PRS_COLOR = 12,
    UI_DIVIDER_COLOR = 13,
    UI_TEXT_COLOR = 14
} ColorKey;

typedef struct {
    BOOL initialized;
    Color colors[NUMCOLORS];
} ColorMap;

void InitializeColors();

Color MappedColor(ColorKey key);

#endif
