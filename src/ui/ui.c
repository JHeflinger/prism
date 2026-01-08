#include "ui.h"
#include <easylogger.h>
#include "data/input.h"
#include "data/colors.h"
#include "data/assets.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"
#include <easymemory.h>
#include <string.h>

IMPL_ARRLIST(Panel);

UI* g_divider_instance = NULL;
BOOL g_divider_active = FALSE;
Vector2 g_ui_cursor = { 0 };
Vector2 g_ui_position = { 0 };
char g_ui_text_buffer[MAX_LINE_WIDTH] = { 0 };
Popup* g_popup = NULL;
Popup* g_popup_origin = NULL;
PersistantUIData* g_active_ui_element = NULL;

#define LINE_HEIGHT 20
#define NAMEBAR_HEIGHT 25

UI* GenerateUI() {
    UI* ui = EZ_ALLOC(1, sizeof(UI));
    return ui;
}

void SetupPanel(Panel* panel, const char* name) {
    strcpy(panel->name, name);
    panel->texture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
}

void UpdateUI(UI* ui) {
    EZ_ASSERT((ui->left && ui->right) || (!ui->left && !ui->right), "UI branches must be split evenly");
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
                    if (InputButtonPressed(IK_MOUSELEFT)) g_divider_active = TRUE;
                }
            } else {
                if ((size_t)GetMouseY() < ui->y + ui->h &&
                    (size_t)GetMouseY() > ui->y &&
                    (size_t)GetMouseX() < ui->x + ui->divide + buffer &&
                    (size_t)GetMouseX() > ui->x + ui->divide - buffer) {
                    g_divider_instance = ui;
                    if (InputButtonPressed(IK_MOUSELEFT)) g_divider_active = TRUE;
                }
            }
        } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            g_divider_active = FALSE;
        }
    }

    // dev split
    if (InputKeyDown(IK_DEV) &&
        (InputButtonPressed(IK_MOUSELEFT) || 
        InputButtonPressed(IK_MOUSERIGHT)) &&
        CheckCollisionPointRec(
            GetMousePosition(), 
            (Rectangle){ui->x, ui->y, ui->w, ui->h}) &&
        !ui->left &&
        !ui->right) {
        ui->vertical = InputButtonPressed(IK_MOUSELEFT);
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
	for (size_t i = 0; i < ui->panels.size; i++)
	    if (ui->panels.data[i].update) ui->panels.data[i].update(ui->w, ui->h);
}

void DrawUI_helper(UI* ui, size_t x, size_t y, size_t w, size_t h) {
    EZ_ASSERT((ui->left && ui->right) || (!ui->left && !ui->right), "UI branches must be split evenly");
    ui->w = w;
    ui->h = h;
    ui->x = x;
    ui->y = y;

    if (ui->left && ui->right) {
        // draw inner ui
        if (ui->vertical) {
            DrawUI_helper((UI*)(ui->left), x, y, w, ui->divide);
            DrawUI_helper((UI*)(ui->right), x, y + ui->divide, w, h - ui->divide);
        } else {
            DrawUI_helper((UI*)(ui->left), x, y, ui->divide, h);
            DrawUI_helper((UI*)(ui->right), x + ui->divide, y, w - ui->divide, h);
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
        float namebar_dif = ui->panels.data[ui->selected].name[0] != 0 && !ui->panels.data[ui->selected].flush ? NAMEBAR_HEIGHT : 0.0f;
        if (strcmp(ui->panels.data[ui->selected].name, "Viewport") == 0) SetViewportRec((Rectangle){ x, y + namebar_dif, w, h - namebar_dif });
        if (namebar_dif > 0.0) {
            DrawRectangle(x, y, w, NAMEBAR_HEIGHT, MappedColor(PANEL_NB_COLOR));
			int xplus = x;
			for (size_t i = 0; i < ui->panels.size; i++) {
				if (ui->panels.data[i].name[0] == 0) continue;
				BOOL selected = ui->selected == i;
				Color deselectedtc = MappedColor(UI_TEXT_COLOR);
				deselectedtc.a = 150;
				float tag_len = MeasureTextEx(FontAsset(), ui->panels.data[i].name, LINE_HEIGHT, 0).x;
				if (InputButtonReleased(IK_MOUSELEFT))
					if (GetMouseX() > xplus && GetMouseX() < xplus + tag_len + 20 && GetMouseY() > (int)y && GetMouseY() < (int)y + NAMEBAR_HEIGHT)
						ui->selected = i;
				DrawRectangle(xplus, y + NAMEBAR_HEIGHT / 2, tag_len + 20, NAMEBAR_HEIGHT / 2, selected ? MappedColor(PANEL_NBGS_COLOR) : MappedColor(PANEL_NBG_COLOR));
				DrawRectangleRounded((Rectangle){xplus, y, tag_len + 20, 3 * NAMEBAR_HEIGHT / 4}, 10, 10, selected ? MappedColor(PANEL_NBGS_COLOR) : MappedColor(PANEL_NBG_COLOR));
				DrawTextEx(FontAsset(), ui->panels.data[i].name, (Vector2){ xplus + 10, y + NAMEBAR_HEIGHT - LINE_HEIGHT - 2 }, LINE_HEIGHT, 0, selected ? MappedColor(UI_TEXT_COLOR) : deselectedtc);	
				xplus += tag_len + 20 + 5;
			}
        }
        if (IsRenderTextureValid(ui->panels.data[ui->selected].texture))
            DrawTexturePro(
                ui->panels.data[ui->selected].texture.texture,
                (Rectangle){ 0, ui->panels.data[ui->selected].texture.texture.height - h + namebar_dif, w, -1*((int)h - namebar_dif) },
                (Rectangle){ x, y + namebar_dif, w, h - namebar_dif },
                (Vector2){ 0, 0 },
                0.0f,
                (Color){ 255, 255, 255, 255 });

        size_t th = 1;
        if (y != 0) DrawLineEx((Vector2){x, y + (th/2)}, (Vector2){x + w, y + (th/2)}, th, MappedColor(PANEL_DIVIDER_COLOR));
        if (x != 0) DrawLineEx((Vector2){x + (th/2), y}, (Vector2){x + (th/2), y + h}, th, MappedColor(PANEL_DIVIDER_COLOR));
        if (x + w < (size_t)GetScreenWidth()) DrawLineEx((Vector2){x + w - (th/2), y + h}, (Vector2){x + w - (th/2), y}, th, MappedColor(PANEL_DIVIDER_COLOR));
        if (y + h < (size_t)GetScreenHeight()) DrawLineEx((Vector2){x + w, y + h - (th/2)}, (Vector2){x, y + h - (th/2)}, th, MappedColor(PANEL_DIVIDER_COLOR));
    }
}

void DrawPopup(size_t x, size_t y, size_t w, size_t h) {
    EZ_ASSERT(g_popup != NULL, "Cannot draw a null popup!");
    DrawRectangle(x, y, w, h, (Color){ 255, 255, 255, 100 });
    if (g_popup->behavior != NULL) {
        int result = g_popup->behavior(x, y, w, h);
        if (result >= (int)g_popup->options) {
            CleanPopup(g_popup_origin);
            g_popup_origin = NULL;
            g_popup = NULL;
        } else if (result >= 0) {
            g_popup = ((Popup**)g_popup->results)[result];
        }
    }
}

void DrawUI(UI* ui, size_t x, size_t y, size_t w, size_t h) {
    if (InputButtonUp(IK_MOUSELEFT)) g_active_ui_element = NULL;
    DrawUI_helper(ui, x, y, w, h);
    if (g_popup != NULL) DrawPopup(x, y, w, h);
}

void PreRenderUI_helper(UI* ui) {
    EZ_ASSERT((ui->left && ui->right) || (!ui->left && !ui->right), "UI branches must be split evenly");
    if (ui->left && ui->right) {
        PreRenderUI_helper((UI*)(ui->left));
        PreRenderUI_helper((UI*)(ui->right));
    } else if (IsRenderTextureValid(ui->panels.data[ui->selected].texture) && ui->panels.data[ui->selected].draw) {
        g_ui_cursor = (Vector2){ 10, 5 };
        g_ui_position = (Vector2){ ui->x , ui->y };
        if (ui->panels.data[ui->selected].name[0] != 0 && !ui->panels.data[ui->selected].flush) g_ui_position.y += NAMEBAR_HEIGHT;
        BeginTextureMode(ui->panels.data[ui->selected].texture);
        ClearBackground((Color){0, 0, 0, 0});
        ui->panels.data[ui->selected].draw(ui->w, ui->h);
        EndTextureMode();
    }
}

void PreRenderUI(UI* ui) {
    if (g_popup != NULL) BlockInput();
    PreRenderUI_helper(ui);
    UnblockInput();
}

void DestroyUI(UI* ui) {
    if (g_popup_origin != NULL) {
        CleanPopup(g_popup_origin);
        g_popup_origin = NULL;
    }
    if (ui->left) DestroyUI((UI*)ui->left);
    if (ui->right) DestroyUI((UI*)ui->right);
	for (size_t i = 0; i < ui->panels.size; i++) DestroyPanel(&(ui->panels.data[i]));
	ARRLIST_Panel_clear(&(ui->panels));
    EZ_FREE(ui);
}

void DestroyPanel(Panel* panel) {
    if (IsRenderTextureValid(panel->texture)) UnloadRenderTexture(panel->texture);
    if (panel->clean) panel->clean();
}

void UIDrawText(const char* text, ...) {
    va_list args;
    va_start(args, text);
    vsnprintf(g_ui_text_buffer, MAX_LINE_WIDTH - 1, text, args);
    DrawTextEx(FontAsset(), g_ui_text_buffer, g_ui_cursor, LINE_HEIGHT, 0, MappedColor(UI_TEXT_COLOR));
    g_ui_cursor.y += LINE_HEIGHT;
    g_ui_cursor.x = 10;
}

BOOL UIDragFloat_(PersistantUIData* data, float* value, float min, float max, float speed, size_t w) {
    BOOL ret = FALSE;
    if (InputButtonPressed(IK_MOUSELEFT) &&
        CheckCollisionPointRec(
            GetMousePosition(),
            (Rectangle){g_ui_cursor.x + g_ui_position.x, g_ui_cursor.y + g_ui_position.y + 2, w, LINE_HEIGHT - 4})) {
        g_active_ui_element = data;
    }
    if (g_active_ui_element == data) {
        float prev = *value;
        *value += GetMouseDelta().x * speed;
        if (*value < min) *value = min;
        if (*value > max) *value = max;
        if (prev != *value) ret = TRUE;
    }
    char buffer[32] = { 0 };
    snprintf(buffer, 32, "%.3f", *value);
    Vector2 text_size = MeasureTextEx(FontAsset(), buffer, LINE_HEIGHT, 0);
    DrawRectangle(g_ui_cursor.x, g_ui_cursor.y + 2, w, LINE_HEIGHT - 4, MappedColor(UI_DRAG_FLOAT_COLOR));
    DrawTextEx(FontAsset(), buffer, (Vector2){ g_ui_cursor.x + (w/2) - (text_size.x/2), g_ui_cursor.y }, LINE_HEIGHT, 0, MappedColor(UI_TEXT_COLOR));
    g_ui_cursor.y += LINE_HEIGHT;
    g_ui_cursor.x = 10;
    return ret;
}

BOOL UIDragFloatLabeled_(PersistantUIData* data, const char* label, float* value, float min, float max, float speed, size_t w) {
    UIDrawText(label);
    float xdif = MeasureTextEx(FontAsset(), label, LINE_HEIGHT, 0).x;
    UIMoveCursor(xdif + 5, -LINE_HEIGHT);
    return UIDragFloat_(data, value, min, max, speed, w - 5 - xdif);
}

void UISetCursor(float x, float y) {
    g_ui_cursor.x = x;
    g_ui_cursor.y = y;
}

void UISetPosition(float x, float y) {
    g_ui_position.x = x;
    g_ui_position.y = y;
}

void UIMoveCursor(float x, float y) {
    g_ui_cursor.x += x;
    g_ui_cursor.y += y;
}

Vector2 UIGetCursor() {
    return g_ui_cursor;
}

void UICheckbox(BOOL* value) {
    if (InputButtonPressed(IK_MOUSELEFT) &&
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

BOOL UIDragUInt_(PersistantUIData* data, uint32_t* value, uint32_t min, uint32_t max, uint32_t speed, size_t w) {
    BOOL ret = FALSE;
    if (InputButtonPressed(IK_MOUSELEFT) &&
        CheckCollisionPointRec(
            GetMousePosition(),
            (Rectangle){g_ui_cursor.x + g_ui_position.x, g_ui_cursor.y + g_ui_position.y + 2, w, LINE_HEIGHT - 4})) {
        g_active_ui_element = data;
    }
    if (g_active_ui_element == data) {
        uint32_t prev = *value;
        if (GetMouseDelta().x * speed < 0 && GetMouseDelta().x * speed * -1 > *value)
            *value = 0;
        else
            *value += GetMouseDelta().x * speed;
        if (*value < min) *value = min;
        if (*value > max) *value = max;
        if (prev != *value) ret = TRUE;
    }
    char buffer[32] = { 0 };
    snprintf(buffer, 32, "%llu", (long long unsigned int)(*value));
    Vector2 text_size = MeasureTextEx(FontAsset(), buffer, LINE_HEIGHT, 0);
    DrawRectangle(g_ui_cursor.x, g_ui_cursor.y + 2, w, LINE_HEIGHT - 4, MappedColor(UI_DRAG_INT_COLOR));
    DrawTextEx(FontAsset(), buffer, (Vector2){ g_ui_cursor.x + (w/2) - (text_size.x/2), g_ui_cursor.y }, LINE_HEIGHT, 0, MappedColor(UI_TEXT_COLOR));
    g_ui_cursor.y += LINE_HEIGHT;
    g_ui_cursor.x = 10;
    return ret;
}

BOOL UIDragUIntLabeled_(PersistantUIData* data, const char* label, uint32_t* value, uint32_t min, uint32_t max, uint32_t speed, size_t w) {
    UIDrawText(label);
    float xdif = MeasureTextEx(FontAsset(), label, LINE_HEIGHT, 0).x;
    UIMoveCursor(xdif + 5, -LINE_HEIGHT);
    return UIDragUInt_(data, value, min, max, speed, w - 5 - xdif);
}

BOOL UIButton(const char* label, size_t w) {
    Vector2 text_size = MeasureTextEx(FontAsset(), label, LINE_HEIGHT, 0);
    float button_width = w < text_size.x + 20 ? text_size.x + 20 : w;
    Color color = MappedColor(PANEL_BTN_BG_COLOR);
    BOOL clicked = FALSE;
    if (CheckCollisionPointRec(
            GetMousePosition(),
            (Rectangle){g_ui_cursor.x + g_ui_position.x, g_ui_cursor.y + g_ui_position.y + 1, button_width, LINE_HEIGHT - 2})) {
        color = MappedColor(PANEL_BTN_HVR_COLOR);
        if (InputButtonDown(IK_MOUSELEFT)) color = MappedColor(PANEL_BTN_PRS_COLOR);
        clicked = InputButtonPressed(IK_MOUSELEFT);
    }
    DrawRectangle(g_ui_cursor.x, g_ui_cursor.y + 1, button_width, LINE_HEIGHT - 2, color);
    Vector2 texpos = g_ui_cursor;
    texpos.x += (button_width - text_size.x) / 2.0f;
    DrawTextEx(FontAsset(), label, texpos, LINE_HEIGHT, 0, MappedColor(UI_TEXT_COLOR));
    g_ui_cursor.y += LINE_HEIGHT;
    g_ui_cursor.x = 10;
    return clicked;
}

void UIPopup(Popup* popup) {
    g_popup = popup;
    g_popup_origin = popup;
}

float UITextWidth(const char* text, ...) {
    va_list args;
    va_start(args, text);
    vsnprintf(g_ui_text_buffer, MAX_LINE_WIDTH - 1, text, args);
    return MeasureTextEx(FontAsset(), g_ui_text_buffer, LINE_HEIGHT, 0).x;
}

void UIDivider(size_t w) {
    DrawRectangle(g_ui_cursor.x, g_ui_cursor.y + (LINE_HEIGHT/2.0f) - 1, w, 2, MappedColor(UI_DIVIDER_COLOR));
    g_ui_cursor.y += LINE_HEIGHT;
    g_ui_cursor.x = 10;
}

void UIDropList_(PersistantUIData* data, const char* label, size_t width, size_t num_items, char** items, SelectFunction func) {
    float clickwidth = UITextWidth(label) + 25;
    clickwidth = clickwidth < width ? width : clickwidth;
    if (CheckCollisionPointRec(
            GetMousePosition(),
            (Rectangle){g_ui_cursor.x + g_ui_position.x, g_ui_cursor.y + g_ui_position.y + 2, clickwidth, LINE_HEIGHT - 4})) {
        DrawRectangle(
            g_ui_cursor.x, g_ui_cursor.y + 2, clickwidth, LINE_HEIGHT - 4,
            InputButtonDown(IK_MOUSELEFT) ? MappedColor(PANEL_BTN_PRS_COLOR) : MappedColor(PANEL_BTN_HVR_COLOR));
        if (InputButtonPressed(IK_MOUSELEFT)) data->arbitrary_bool = !data->arbitrary_bool;
    }
    if (data->arbitrary_bool) {
        DrawTriangle(
            (Vector2){g_ui_cursor.x + 5, g_ui_cursor.y + LINE_HEIGHT/2.0f - 5},
            (Vector2){g_ui_cursor.x + 10, g_ui_cursor.y + LINE_HEIGHT/2.0f + 5},
            (Vector2){g_ui_cursor.x + 15, g_ui_cursor.y + LINE_HEIGHT/2.0f - 5},
            MappedColor(UI_TEXT_COLOR));
    } else {
        DrawTriangle(
            (Vector2){g_ui_cursor.x + 5, g_ui_cursor.y + LINE_HEIGHT/2.0f - 5},
            (Vector2){g_ui_cursor.x + 5, g_ui_cursor.y + LINE_HEIGHT/2.0f + 5},
            (Vector2){g_ui_cursor.x + 15, g_ui_cursor.y + LINE_HEIGHT/2.0f},
            MappedColor(UI_TEXT_COLOR));
    }
    g_ui_cursor.x += 20;
    UIDrawText(label);
    if (data->arbitrary_bool) {
        float top = g_ui_cursor.y;
        for (size_t i = 0; i < num_items; i++) {
            g_ui_cursor.x += 30;
            if (func && CheckCollisionPointRec(
                GetMousePosition(),
                (Rectangle){g_ui_cursor.x + g_ui_position.x, g_ui_cursor.y + g_ui_position.y + 2, clickwidth - 30, LINE_HEIGHT - 4})) {
                DrawRectangle(
                    g_ui_cursor.x, g_ui_cursor.y + 2, clickwidth - 30, LINE_HEIGHT - 4,
                    InputButtonDown(IK_MOUSELEFT) ? MappedColor(PANEL_BTN_PRS_COLOR) : MappedColor(PANEL_BTN_HVR_COLOR));
                if (InputButtonPressed(IK_MOUSELEFT)) func(i);
            }
            UIDrawText(items[i]);
        }
        if (num_items > 0) DrawRectangle(g_ui_cursor.x + 20, top + 5, 2, num_items*LINE_HEIGHT - 5, MappedColor(PANEL_BTN_HVR_COLOR));
    }
}
