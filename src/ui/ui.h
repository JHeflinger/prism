#ifndef UI_H
#define UI_H

#include "data/config.h"
#include <raylib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define MAX_NAME_LEN 256
#define MAX_LINE_WIDTH 2048

typedef void (*PanelFunction)(float width, float height);

typedef struct {
    char name[MAX_NAME_LEN];
    RenderTexture2D texture;
    PanelFunction draw;
    PanelFunction update;
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

void UIDragFloat(float* value, float min, float max, float speed, size_t w);

void UIDragFloatLabeled(const char* label, float* value, float min, float max, float speed, size_t w);

void UIMoveCursor(float x, float y);

void UICheckbox(BOOL* value);

void UICheckboxLabeled(const char* label, BOOL* value);

void UIDragUInt(uint32_t* value, uint32_t min, uint32_t max, uint32_t speed, size_t w);

void UIDragUIntLabeled(const char* label, uint32_t* value, uint32_t min, uint32_t max, uint32_t speed, size_t w);

#endif
