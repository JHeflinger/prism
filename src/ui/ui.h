#ifndef UI_H
#define UI_H

#include <stddef.h>
#include "data/config.h"

#define MAX_UI_DEPTH 24
#define MAX_NAME_LEN 256

typedef struct {
    char name[MAX_NAME_LEN];
} Panel;

typedef enum {
    TOPLEFT = 0,
    TOPMIDDLE = 1,
    TOPRIGHT = 2,
    MIDDLELEFT = 3,
    MIDDLEMIDDLE = 4,
    MIDDLERIGHT = 5,
    BOTTOMLEFT = 6,
    BOTTOMMIDDLE = 7,
    BOTTOMRIGHT = 8,
} UIPlacement;

typedef struct {
    Panel panel;
    UIPlacement placement[MAX_UI_DEPTH];
    size_t placements;
} PanelPlacement;

typedef struct {
    void* internal[9];
} UI;

UI* GenerateUI();

BOOL InjectIntoUI(UI* ui, PanelPlacement placement);

void UpdateUI(UI* ui);

void DrawUI(UI* ui);

void DestroyUI(UI* ui);

#endif