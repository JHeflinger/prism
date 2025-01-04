#ifndef COLORS_H
#define COLORS_H

#include "data/config.h"
#include "raylib.h"

#define NUMCOLORS 2

typedef enum {
    PANEL_BG_COLOR = 0,
    PANEL_DIVIDER_COLOR = 1,
} ColorKey;

typedef struct {
    BOOL initialized;
    Color colors[NUMCOLORS];
} ColorMap;

void InitializeColors();

Color MappedColor(ColorKey key);

#endif