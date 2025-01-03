#ifndef UI_H
#define UI_H

#include "data/config.h"

#define MAX_NAME_LEN 256

typedef struct {
    char name[MAX_NAME_LEN];
} Panel;

typedef struct {
    void* left;
    void* right;
    Panel panel;
    BOOL vertical;
} UI;

UI* GenerateUI();

void UpdateUI(UI* ui);

void DrawUI(UI* ui);

void DestroyUI(UI* ui);

#endif