#include "ui.h"
#include "core/log.h"
#include "data/input.h"
#include "data/colors.h"
#include "data/assets.h"
#include <easymemory.h>
#include <string.h>

UI* g_divider_instance = NULL;
BOOL g_divider_active = FALSE;
Vector2 g_ui_cursor = { 0 };
Vector2 g_ui_position = { 0 };
char g_ui_text_buffer[MAX_LINE_WIDTH] = { 0 };

#define LINE_HEIGHT 20

UI* GenerateUI() {
    UI* ui = EZALLOC(1, sizeof(UI));
    return ui;
}

void SetupPanel(Panel* panel, const char* name) {
    strcpy(panel->name, name);
    panel->texture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
}

void UpdateUI(UI* ui) {   
    LOG_ASSERT((ui->left && ui->right) || (!ui->left && !ui->right), "UI branches must be split evenly");
    if (ui->left && ui->right) {
        // update further down
        UpdateUI((UI*)(ui->left));
        UpdateUI((UI*)(ui->right));
        
        // handle hovering and active dragging
        size_t buffer = 5;
        if (!g_divider_active) {
            if (ui->vertical) {
                if ((size_t)GetMouseX() < ui->x + ui->w &&
                    (size_t)GetMouseX() > ui->x &&
                    (size_t)GetMouseY() < ui->y + ui->divide + buffer &&
                    (size_t)GetMouseY() > ui->y + ui->divide - buffer) {
                    g_divider_instance = ui;
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_divider_active = TRUE;
                }
            } else {
                if ((size_t)GetMouseY() < ui->y + ui->h &&
                    (size_t)GetMouseY() > ui->y &&
                    (size_t)GetMouseX() < ui->x + ui->divide + buffer &&
                    (size_t)GetMouseX() > ui->x + ui->divide - buffer) {
                    g_divider_instance = ui;
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_divider_active = TRUE;
                }
            }
        } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            g_divider_active = FALSE;
        }
    }


    // dev split
    if (InputKeyDown(IK_DEV) &&
        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || 
        IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) &&
        CheckCollisionPointRec(
            GetMousePosition(), 
            (Rectangle){ui->x, ui->y, ui->w, ui->h}) &&
        !ui->left &&
        !ui->right) {
        ui->vertical = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        UI* left = GenerateUI();
        UI* right = GenerateUI();
        if (ui->vertical) {
            ui->divide = ui->h / 2;
        } else {
            ui->divide = ui->w / 2;
        }
        ui->left = (void*)left;
        ui->right = (void*)right;
    }

    // divisor dragging
    if (g_divider_instance == ui && g_divider_active) {
        if (ui->vertical) {
            if ((size_t)GetMouseY() < ui->y + ui->h && (size_t)GetMouseY() > ui->y)
                ui->divide = GetMouseY() - ui->y;
        } else {
            if ((size_t)GetMouseX() < ui->x + ui->w && (size_t)GetMouseX() > ui->x)
                ui->divide = GetMouseX() - ui->x;
        }
    }

    // update panel
    if (ui->panel.update) ui->panel.update(ui->w, ui->h);
}

void DrawUI(UI* ui, size_t x, size_t y, size_t w, size_t h) {
    LOG_ASSERT((ui->left && ui->right) || (!ui->left && !ui->right), "UI branches must be split evenly");
    ui->w = w;
    ui->h = h;
    ui->x = x;
    ui->y = y;

    if (ui->left && ui->right) {
        // draw inner ui
        if (ui->vertical) {
            DrawUI((UI*)(ui->left), x, y, w, ui->divide);
            DrawUI((UI*)(ui->right), x, y + ui->divide, w, h - ui->divide);
        } else {
            DrawUI((UI*)(ui->left), x, y, ui->divide, h);
            DrawUI((UI*)(ui->right), x + ui->divide, y, w - ui->divide, h);
        }

        // draw divider
        if (ui == g_divider_instance) {
            size_t sth = 2;
            if (ui->vertical) {
                DrawLineEx(
                    (Vector2){x, y + ui->divide + (sth/2)},
                    (Vector2){x + w, y + ui->divide + (sth/2)},
                    sth,
                    g_divider_active ? MappedColor(PANEL_DIVIDER_ACTIVE_COLOR) : MappedColor(PANEL_DIVIDER_HOVER_COLOR));
            } else {
                DrawLineEx(
                    (Vector2){x + ui->divide + (sth/2), y},
                    (Vector2){x + ui->divide + (sth/2), y + h},
                    sth,
                    g_divider_active ? MappedColor(PANEL_DIVIDER_ACTIVE_COLOR) : MappedColor(PANEL_DIVIDER_HOVER_COLOR));
            }
            if (!g_divider_active) g_divider_instance = NULL;
        }
    } else {
        DrawRectangle(x, y, w, h, MappedColor(PANEL_BG_COLOR));
        if (IsRenderTextureValid(ui->panel.texture))
            DrawTexturePro(ui->panel.texture.texture, (Rectangle){ 0, 0, w, -1*((int)h) }, (Rectangle){ x, y, w, h }, (Vector2){ 0, 0 }, 0.0f, (Color){ 255, 255, 255, 255 });
        size_t th = 1;
        if (y != 0) DrawLineEx((Vector2){x, y + (th/2)}, (Vector2){x + w, y + (th/2)}, th, MappedColor(PANEL_DIVIDER_COLOR));
        if (x != 0) DrawLineEx((Vector2){x + (th/2), y}, (Vector2){x + (th/2), y + h}, th, MappedColor(PANEL_DIVIDER_COLOR));
        if (x + w < (size_t)GetScreenWidth()) DrawLineEx((Vector2){x + w - (th/2), y + h}, (Vector2){x + w - (th/2), y}, th, MappedColor(PANEL_DIVIDER_COLOR));
        if (y + h < (size_t)GetScreenHeight()) DrawLineEx((Vector2){x + w, y + h - (th/2)}, (Vector2){x, y + h - (th/2)}, th, MappedColor(PANEL_DIVIDER_COLOR));
    }
}

void PreRenderUI(UI* ui) {
    LOG_ASSERT((ui->left && ui->right) || (!ui->left && !ui->right), "UI branches must be split evenly");
    if (ui->left && ui->right) {
        PreRenderUI((UI*)(ui->left));
        PreRenderUI((UI*)(ui->right));
    } else if (IsRenderTextureValid(ui->panel.texture) && ui->panel.draw) {
        g_ui_cursor = (Vector2){ 10, 10 };
        g_ui_position = (Vector2){ ui->x , ui->y };
        BeginTextureMode(ui->panel.texture);
        ClearBackground((Color){0, 0, 0, 0});
        ui->panel.draw(ui->w, ui->h);
        EndTextureMode();
    }
}

void DestroyUI(UI* ui) {
    if (ui->left) DestroyUI((UI*)ui->left);
    if (ui->right) DestroyUI((UI*)ui->right);
    if (!ui->left && !ui->right) DestroyPanel(&(ui->panel));
    EZFREE(ui);
}

void DestroyPanel(Panel* panel) {
    if (IsRenderTextureValid(panel->texture)) UnloadRenderTexture(panel->texture);
}

void UIDrawText(const char* text, ...) {
    va_list args;
    va_start(args, text);
    vsnprintf(g_ui_text_buffer, MAX_LINE_WIDTH - 1, text, args);
    DrawTextEx(FontAsset(), g_ui_text_buffer, g_ui_cursor, LINE_HEIGHT, 0, WHITE);
    g_ui_cursor.y += LINE_HEIGHT;
    g_ui_cursor.x = 10;
}

void UIDragFloat(float* value, float min, float max, float speed, size_t w) {
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(
            GetMousePosition(),
            (Rectangle){g_ui_cursor.x + g_ui_position.x, g_ui_cursor.y + g_ui_position.y + 2, w, LINE_HEIGHT - 4})) {
        *value += GetMouseDelta().x * speed;
        if (*value < min) *value = min;
        if (*value > max) *value = max;
    }
    char buffer[32] = { 0 };
    snprintf(buffer, 32, "%.3f", *value);
    Vector2 text_size = MeasureTextEx(FontAsset(), buffer, LINE_HEIGHT, 0);
    DrawRectangle(g_ui_cursor.x, g_ui_cursor.y + 2, w, LINE_HEIGHT - 4, MappedColor(UI_DRAG_FLOAT_COLOR));
    DrawTextEx(FontAsset(), buffer, (Vector2){ g_ui_cursor.x + (w/2) - (text_size.x/2), g_ui_cursor.y }, LINE_HEIGHT, 0, WHITE);
    g_ui_cursor.y += LINE_HEIGHT;
    g_ui_cursor.x = 10;
}

void UIDragFloatLabeled(const char* label, float* value, float min, float max, float speed, size_t w) {
    UIDrawText(label);
    float xdif = MeasureTextEx(FontAsset(), label, LINE_HEIGHT, 0).x;
    UIMoveCursor(xdif + 5, -LINE_HEIGHT);
    UIDragFloat(value, min, max, speed, w - 5 - xdif);
}

void UIMoveCursor(float x, float y) {
    g_ui_cursor.x += x;
    g_ui_cursor.y += y;
}

void UICheckbox(BOOL* value) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(
            GetMousePosition(),
            (Rectangle){g_ui_cursor.x + g_ui_position.x + 2, g_ui_cursor.y + g_ui_position.y + 2, LINE_HEIGHT - 4, LINE_HEIGHT - 4})) {
        *value = !(*value);
    }
	DrawRectangle(g_ui_cursor.x + 2, g_ui_cursor.y + 2, LINE_HEIGHT - 4, LINE_HEIGHT - 4, MappedColor(UI_CHECKBOX_COLOR));
	if (*value) {
		float cmx = g_ui_cursor.x + 2.0f;
		float cmy = g_ui_cursor.y + 2.0f;
		DrawRectanglePro((Rectangle){cmx + 4, cmy + 4, 8, 4}, (Vector2){ 0, 0 }, 45.0f, MappedColor(UI_CHECKMARK_COLOR)); 
		DrawRectanglePro((Rectangle){cmx - 9, cmy + 0, 4, 12}, (Vector2){ -16, 16 }, 45.0f, MappedColor(UI_CHECKMARK_COLOR)); 
	}
    g_ui_cursor.y += LINE_HEIGHT;
    g_ui_cursor.x = 10;
}

void UICheckboxLabeled(const char* label, BOOL* value) {
    UIDrawText(label);
    float xdif = MeasureTextEx(FontAsset(), label, LINE_HEIGHT, 0).x;
    UIMoveCursor(xdif + 5, -LINE_HEIGHT);
	UICheckbox(value);
}

void UIDragUInt(uint32_t* value, uint32_t min, uint32_t max, uint32_t speed, size_t w) {
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(
            GetMousePosition(),
            (Rectangle){g_ui_cursor.x + g_ui_position.x, g_ui_cursor.y + g_ui_position.y + 2, w, LINE_HEIGHT - 4})) {
        if (GetMouseDelta().x * speed < 0 && GetMouseDelta().x * speed * -1 > *value)
            *value = 0;
        else
            *value += GetMouseDelta().x * speed;
        if (*value < min) *value = min;
        if (*value > max) *value = max;
    }
    char buffer[32] = { 0 };
    snprintf(buffer, 32, "%llu", (long long unsigned int)(*value));
    Vector2 text_size = MeasureTextEx(FontAsset(), buffer, LINE_HEIGHT, 0);
    DrawRectangle(g_ui_cursor.x, g_ui_cursor.y + 2, w, LINE_HEIGHT - 4, MappedColor(UI_DRAG_INT_COLOR));
    DrawTextEx(FontAsset(), buffer, (Vector2){ g_ui_cursor.x + (w/2) - (text_size.x/2), g_ui_cursor.y }, LINE_HEIGHT, 0, WHITE);
    g_ui_cursor.y += LINE_HEIGHT;
    g_ui_cursor.x = 10;
}

void UIDragUIntLabeled(const char* label, uint32_t* value, uint32_t min, uint32_t max, uint32_t speed, size_t w) {
    UIDrawText(label);
    float xdif = MeasureTextEx(FontAsset(), label, LINE_HEIGHT, 0).x;
    UIMoveCursor(xdif + 5, -LINE_HEIGHT);
    UIDragUInt(value, min, max, speed, w - 5 - xdif);
}