#ifndef UI_H
#define UI_H

#include "data/config.h"
#include "ui/popup.h"
#include <raylib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#define MAX_NAME_LEN 256
#define MAX_LINE_WIDTH 2048

#define PERSISTANT_UI(func, ...) \
    ({static PersistantUIData s_ui_data_##__COUNTER__ = { 0 }; \
    func(&s_ui_data_##__COUNTER__, __VA_ARGS__);})

typedef void (*PanelFunction)(float width, float height);
typedef void (*CleanFunction)(void);
typedef void (*SelectFunction)(size_t index);

typedef struct {
    char name[MAX_NAME_LEN];
    RenderTexture2D texture;
    PanelFunction draw;
    PanelFunction update;
    CleanFunction clean;
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

typedef struct {
    size_t arbitrary_counter;
    BOOL arbitrary_bool;
} PersistantUIData;

UI* GenerateUI();

void SetupPanel(Panel* panel, const char* name);

void UpdateUI(UI* ui);

void DrawUI(UI* ui, size_t x, size_t y, size_t w, size_t h);

void PreRenderUI(UI* ui);

void DestroyUI(UI* ui);

void DestroyPanel(Panel* panel);

void UIDrawText(const char* text, ...);

BOOL UIDragFloat_(PersistantUIData* data, float* value, float min, float max, float speed, size_t w);
#define UIDragFloat(value, min, max, speed, w) \
    PERSISTANT_UI(UIDragFloat_, value, min, max, speed, w)

BOOL UIDragFloatLabeled_(PersistantUIData* data, const char* label, float* value, float min, float max, float speed, size_t w);
#define UIDragFloatLabeled(label, value, min, max, speed, w) \
    PERSISTANT_UI(UIDragFloatLabeled_, label, value, min, max, speed, w)

void UISetCursor(float x, float y);

void UISetPosition(float x, float y);

void UIMoveCursor(float x, float y);

Vector2 UIGetCursor();

void UICheckbox(BOOL* value);

void UICheckboxLabeled(const char* label, BOOL* value);

BOOL UIDragUInt_(PersistantUIData* data, uint32_t* value, uint32_t min, uint32_t max, uint32_t speed, size_t w);
#define UIDragUInt(value, min, max, speed, w) \
    PERSISTANT_UI(UIDragUInt_, value, min, max, speed, w)

BOOL UIDragUIntLabeled_(PersistantUIData* data, const char* label, uint32_t* value, uint32_t min, uint32_t max, uint32_t speed, size_t w);
#define UIDragUIntLabeled(label, value, min, max, speed, w) \
    PERSISTANT_UI(UIDragUIntLabeled_, label, value, min, max, speed, w)

BOOL UIButton(const char* label, size_t w);

void UIPopup(Popup* popup);

float UITextWidth(const char* text, ...);

void UIDivider(size_t w);

void UIDropList_(PersistantUIData* data, const char* label, size_t width, size_t num_items, char** items, SelectFunction func);
#define UIDropList(label, width, num_items, items, func) \
    PERSISTANT_UI(UIDropList_, label, width, num_items, items, func)

#endif
