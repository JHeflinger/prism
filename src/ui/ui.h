#ifndef UI_H
#define UI_H

#include "data/config.h"
#include <stddef.h>

#define MAX_NAME_LEN 256

typedef struct {
    char name[MAX_NAME_LEN];
} Panel;

typedef struct {
    void* left;
    void* right;
    size_t divide;
    size_t x;
    size_t y;
    size_t w;
    size_t h;
    Panel panel;
    BOOL vertical;
} UI;

UI* GenerateUI();

void UpdateUI(UI* ui);

void DrawUI(UI* ui, size_t x, size_t y, size_t w, size_t h);

void DestroyUI(UI* ui);

#endif