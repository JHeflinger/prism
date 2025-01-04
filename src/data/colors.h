#ifndef COLORS_H
#define COLORS_H

#include "data/config.h"
#include "raylib.h"

#define NUMCOLORS 4

typedef enum {
    PANEL_BG_COLOR = 0,
    PANEL_DIVIDER_COLOR = 1,
    PANEL_DIVIDER_HOVER_COLOR = 2,
    PANEL_DIVIDER_ACTIVE_COLOR = 3,
} ColorKey;

typedef struct {
    BOOL initialized;
    Color colors[NUMCOLORS];
} ColorMap;

void InitializeColors();

Color MappedColor(ColorKey key);

#endif