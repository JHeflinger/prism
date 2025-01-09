#ifndef UI_H
#define UI_H

#include "data/config.h"
#include "raylib.h"
#include <stddef.h>
#include <stdarg.h>

#define MAX_NAME_LEN 256
#define MAX_LINE_WIDTH 2048

typedef void (*PanelFunction)(void);

typedef struct {
    char name[MAX_NAME_LEN];
    RenderTexture2D texture;
    PanelFunction draw;
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

void SetupPanel(Panel* panel, const char* name);

void UpdateUI(UI* ui);

void DrawUI(UI* ui, size_t x, size_t y, size_t w, size_t h);

void PreRenderUI(UI* ui);

void DestroyUI(UI* ui);

void DestroyPanel(Panel* panel);

void UIDrawText(const char* text, ...);

#endif