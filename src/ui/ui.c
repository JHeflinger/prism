#include "ui.h"
#include <easylogger.h>
#include "data/input.h"
#include "data/colors.h"
#include "data/assets.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"
#include "core/binds.h"
#include <easymemory.h>
#include <string.h>

IMPL_ARRLIST(Panel);

typedef struct {
    PersistantUIData* data;
    char** items;
    size_t count;
    BOOL active;
    float width;
    void* funcdata;
    DropdownSelectFunction func;
    Vector2 origin;
} DropdownMenuData;

typedef struct {
    char* buffer;
    size_t size;
    BOOL active;
    PersistantUIData* data;
    size_t cursor;
    Vector2 origin;
    float width;
} TextInputData;

UI* g_primary_ui = NULL;
UI* g_divider_instance = NULL;
BOOL g_divider_active = FALSE;
Vector2 g_ui_cursor = { 0 };
Vector2 g_ui_position = { 0 };
char g_ui_text_buffer[MAX_LINE_WIDTH] = { 0 };
Popup* g_popup = NULL;
Popup* g_popup_origin = NULL;
PersistantUIData* g_active_ui_element = NULL;
DropdownMenuData g_dropdownmenu_data = { 0 };
TextInputData g_textinput_data = { 0 };
BOOL g_was_ui_element_just_used = FALSE;
RenderTexture2D g_ui_scratch_target = { 0 };
RenderTexture2D g_current_ui_target = { 0 };
BOOL g_scratch_target_in_use = FALSE;

#define LINE_HEIGHT 20
#define NAMEBAR_HEIGHT 25

UI* GetLeftUI(UI* ui) {
    return (UI*)(ui->left);
}

UI* GetRightUI(UI* ui) {
    return (UI*)(ui->right);
}

void SetPrimaryUI(UI* ui) {
    g_primary_ui = ui;
}

UI* GenerateUI() {
    UI* ui = EZ_ALLOC(1, sizeof(UI));
    return ui;
}

void SetupPanel(Panel* panel, const char* name) {
    strcpy(panel->name, name);
    panel->texture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
}

BOOL UIRequestsBlockInput() {
    return g_popup != NULL || g_dropdownmenu_data.active || g_textinput_data.active;
}

void UpdateUI(UI* ui) {
    EZ_ASSERT((ui->left && ui->right) || (!ui->left && !ui->right), "UI branches must be split evenly");
    if (ui->left && ui->right) {
        // update further down
        UpdateUI((UI*)(ui->left));
        UpdateUI((UI*)(ui->right));

        // handle hovering and active dragging
        if (UIRequestsBlockInput()) BlockInput();
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
    if (UIRequestsBlockInput()) BlockInput();
	for (size_t i = 0; i < ui->panels.size; i++)
	    if (ui->panels.data[i].update) ui->panels.data[i].update(ui->w, ui->h);
    UnblockInput();
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
    g_current_ui_target = (RenderTexture2D){ 0 };
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

void DrawDropdownMenu() {
    Vector2 cursor = g_dropdownmenu_data.origin;
    cursor.y += LINE_HEIGHT - 2;
    for (size_t i = 0; i < g_dropdownmenu_data.count; i++) {
        if (i == g_dropdownmenu_data.data->arbitrary_counter) continue;
        Vector2 text_size = MeasureTextEx(FontAsset(), g_dropdownmenu_data.items[i], LINE_HEIGHT, 0);
        float w = g_dropdownmenu_data.width < text_size.x + 10 ? text_size.x + 10 : g_dropdownmenu_data.width;
        Color color = MappedColor(UI_DROPDOWN_MENU_COLOR);
        if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){cursor.x, cursor.y, w, LINE_HEIGHT})) {
            if (InputButtonDown(IK_MOUSELEFT)) color = MappedColor(UI_DROPDOWN_MENU_PRS_COLOR);
            else color = MappedColor(UI_DROPDOWN_MENU_HVR_COLOR);
            if (InputButtonReleased(IK_MOUSELEFT)) g_dropdownmenu_data.data->arbitrary_counter = g_dropdownmenu_data.func(g_dropdownmenu_data.funcdata, i);
        }
        DrawRectangle(cursor.x, cursor.y, w, LINE_HEIGHT, color);
        DrawRectangle(cursor.x + 2, cursor.y + 2, 2, LINE_HEIGHT - 4, MappedColor(UI_DIVIDER_COLOR));
        DrawTextEx(FontAsset(), g_dropdownmenu_data.items[i], (Vector2){cursor.x + 8, cursor.y + 0}, LINE_HEIGHT, 0, MappedColor(UI_TEXT_COLOR));
        cursor.y += LINE_HEIGHT;
    }
    if (InputButtonReleased(IK_MOUSELEFT) && g_dropdownmenu_data.active == 2) g_dropdownmenu_data.active = FALSE;
    else if (InputButtonReleased(IK_MOUSELEFT)) g_dropdownmenu_data.active = 2;
}

void HandleTextInput() {
    int c;
    size_t pre_cursor = g_textinput_data.cursor;
    static float backspace_timer = 0.0f;
    backspace_timer += GetFrameTime();
    if (!InputKeyDown(IK_BACKSPACE)) backspace_timer = 0.0f;
    while ((c = GetCharPressed()) != 0) {
        if (g_textinput_data.cursor >= g_textinput_data.size - 1) continue;
        if (g_textinput_data.cursor < strlen(g_textinput_data.buffer)) {
            for (size_t i = strlen(g_textinput_data.buffer); i > g_textinput_data.cursor; i--) {
                g_textinput_data.buffer[i] = g_textinput_data.buffer[i - 1];
            }
        }
        char prev = g_textinput_data.buffer[g_textinput_data.cursor];
        g_textinput_data.buffer[g_textinput_data.cursor] = (char)c;
        g_textinput_data.cursor++;
        g_textinput_data.buffer[g_textinput_data.cursor] = prev;
    }
    if (InputKeyPressed(IK_LEFT) && g_textinput_data.cursor > 0) g_textinput_data.cursor--;
    if (InputKeyPressed(IK_RIGHT) && g_textinput_data.cursor < strlen(g_textinput_data.buffer)) g_textinput_data.cursor++;
    if (InputKeyPressed(IK_UP)) g_textinput_data.cursor = 0;
    if (InputKeyPressed(IK_DOWN)) g_textinput_data.cursor = strlen(g_textinput_data.buffer);
    if ((backspace_timer > 0.5f || InputKeyPressed(IK_BACKSPACE)) && g_textinput_data.cursor > 0) {
        g_textinput_data.buffer[g_textinput_data.cursor - 1] = '\0';
        for (size_t i = g_textinput_data.cursor; i < g_textinput_data.size - 1; i++)
            g_textinput_data.buffer[i] = g_textinput_data.buffer[i+1];
        g_textinput_data.cursor--;
    }
    if (InputKeyPressed(IK_ENTER) || (InputButtonPressed(IK_MOUSELEFT) && !CheckCollisionPointRec(
        GetMousePosition(),
        (Rectangle){ g_textinput_data.origin.x, g_textinput_data.origin.y, g_textinput_data.width, LINE_HEIGHT - 2 }))) {
        g_textinput_data.active = FALSE;
        g_textinput_data.data = NULL;
    }
    if (InputButtonPressed(IK_MOUSELEFT) && CheckCollisionPointRec(
        GetMousePosition(),
        (Rectangle){ g_textinput_data.origin.x, g_textinput_data.origin.y, g_textinput_data.width, LINE_HEIGHT - 2 })) {
        g_textinput_data.cursor = strlen(g_textinput_data.buffer);
        float text_start_pos = 2.0f;
        Vector2 text_size = MeasureTextEx(FontAsset(), g_textinput_data.buffer, LINE_HEIGHT, 0);
        if (text_size.x > g_textinput_data.width) text_start_pos -= (text_size.x - g_textinput_data.width) + 4.0f;
        for (size_t i = 0; i < g_textinput_data.size; i++) {
            if (g_textinput_data.buffer[i] == '\0') break;
            char b[1024] = { 0 };
            memcpy(b, g_textinput_data.buffer, i + 1);
            Vector2 tsize = MeasureTextEx(FontAsset(), b, LINE_HEIGHT, 0);
            if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){ g_textinput_data.origin.x + text_start_pos, g_textinput_data.origin.y, tsize.x, LINE_HEIGHT })) {
                g_textinput_data.cursor = i;
                break;
            }
        }
    }
    if (pre_cursor != g_textinput_data.cursor) g_textinput_data.data->arbitrary_timer = 0.0f;
}

void DrawUI(UI* ui, size_t x, size_t y, size_t w, size_t h) {
    if (InputButtonUp(IK_MOUSELEFT)) g_active_ui_element = NULL;
    if (UIRequestsBlockInput()) BlockInput();
    DrawUI_helper(ui, x, y, w, h);
    if (g_popup != NULL) {
        UnblockInput();
        DrawPopup(x, y, w, h);
    }
    if (g_dropdownmenu_data.active) {
        UnblockInput();
        DrawDropdownMenu();
    }
    if (g_textinput_data.active) {
        UnblockInput();
        HandleTextInput();
    }
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
        g_current_ui_target = ui->panels.data[ui->selected].texture;
        ClearBackground((Color){0, 0, 0, 0});
        ui->panels.data[ui->selected].draw(ui->w, ui->h);
        EndTextureMode();
    }
}

void PreRenderUI(UI* ui) {
    if (UIRequestsBlockInput()) BlockInput();
    PreRenderUI_helper(ui);
    UnblockInput();
}

void DestroyUI(UI* ui) {
    if (g_scratch_target_in_use) {
        UnloadRenderTexture(g_ui_scratch_target);
        g_scratch_target_in_use = FALSE;
    }
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

const char* HoveredPanelHelper(UI* ui) {
    if (!ui) return NULL;
    if (!ui->left && !ui->right) {
        if (GetMousePosition().x > ui->x &&
            GetMousePosition().x < ui->x + ui->w &&
            GetMousePosition().y > ui->y &&
            GetMousePosition().y < ui->y + ui->h) {
            return ui->panels.data[ui->selected].name;
        }
    } else {
        const char* l = HoveredPanelHelper((UI*)ui->left);
        if (l) return l;
        return HoveredPanelHelper((UI*)ui->right);
    }
    return NULL;
}

const char* HoveredPanel() {
    return HoveredPanelHelper(g_primary_ui);
}

void ClearJustUsedUI() {
    g_was_ui_element_just_used = FALSE;
}

BOOL UIWasJustUsed() {
    return g_was_ui_element_just_used;
}

void UIDrawText(const char* text, ...) {
    va_list args;
    va_start(args, text);
    vsnprintf(g_ui_text_buffer, MAX_LINE_WIDTH - 1, text, args);
    DrawTextEx(FontAsset(), g_ui_text_buffer, g_ui_cursor, LINE_HEIGHT, 0, MappedColor(UI_TEXT_COLOR));
    g_ui_cursor.y += LINE_HEIGHT;
    g_ui_cursor.x = 10;
}

void UIDrawSubtleText(const char* text, ...) {
    va_list args;
    va_start(args, text);
    vsnprintf(g_ui_text_buffer, MAX_LINE_WIDTH - 1, text, args);
    DrawTextEx(FontAsset(), g_ui_text_buffer, g_ui_cursor, LINE_HEIGHT, 0, MappedColor(UI_SUBTLE_TEXT_COLOR));
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
        g_was_ui_element_just_used = TRUE;
    }
    char buffer[32] = { 0 };
    snprintf(buffer, 32, "%.3f", *value);
    Vector2 text_size = MeasureTextEx(FontAsset(), buffer, LINE_HEIGHT, 0);
    DrawRectangle(g_ui_cursor.x, g_ui_cursor.y + 1, w, LINE_HEIGHT - 2, MappedColor(UI_DRAG_FLOAT_COLOR));
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

Vector2 UIGetPosition() {
    return g_ui_position;
}

void UICheckbox(BOOL* value) {
    if (InputButtonPressed(IK_MOUSELEFT) &&
        CheckCollisionPointRec(
            GetMousePosition(),
            (Rectangle){g_ui_cursor.x + g_ui_position.x + 2, g_ui_cursor.y + g_ui_position.y + 2, LINE_HEIGHT - 4, LINE_HEIGHT - 4})) {
        *value = !(*value);
        g_was_ui_element_just_used = TRUE;
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
        g_was_ui_element_just_used = TRUE;
    }
    char buffer[32] = { 0 };
    snprintf(buffer, 32, "%llu", (long long unsigned int)(*value));
    Vector2 text_size = MeasureTextEx(FontAsset(), buffer, LINE_HEIGHT, 0);
    DrawRectangle(g_ui_cursor.x, g_ui_cursor.y + 1, w, LINE_HEIGHT - 2, MappedColor(UI_DRAG_INT_COLOR));
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
    if (clicked) g_was_ui_element_just_used = TRUE;
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

float UITextHeight(const char* text, ...) {
    va_list args;
    va_start(args, text);
    vsnprintf(g_ui_text_buffer, MAX_LINE_WIDTH - 1, text, args);
    return MeasureTextEx(FontAsset(), g_ui_text_buffer, LINE_HEIGHT, 0).y;
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
                if (InputButtonPressed(IK_MOUSELEFT)) {
                    func(i);
                    g_was_ui_element_just_used = TRUE;
                }
            }
            UIDrawText(items[i]);
        }
        if (num_items > 0) DrawRectangle(g_ui_cursor.x + 20, top + 5, 2, num_items*LINE_HEIGHT - 5, MappedColor(PANEL_BTN_HVR_COLOR));
    }
}

void UIDropdownMenu_(PersistantUIData* data, size_t width, size_t num_items, char** items, DropdownSelectFunction func, void* param) {
    data->arbitrary_counter = func(param, (size_t)-1);
    Vector2 text_size = MeasureTextEx(FontAsset(), items[data->arbitrary_counter], LINE_HEIGHT, 0);
    float button_width = width < text_size.x + 20 ? text_size.x + 20 : width;
    Color color = MappedColor(PANEL_BTN_BG_COLOR);
    BOOL clicked = FALSE;
    if (CheckCollisionPointRec(
            GetMousePosition(),
            (Rectangle){g_ui_cursor.x + g_ui_position.x, g_ui_cursor.y + g_ui_position.y + 1, button_width, LINE_HEIGHT - 2})) {
        color = MappedColor(PANEL_BTN_HVR_COLOR);
        if (InputButtonDown(IK_MOUSELEFT)) color = MappedColor(PANEL_BTN_PRS_COLOR);
        clicked = InputButtonPressed(IK_MOUSELEFT);
    }
    if (clicked) g_was_ui_element_just_used = TRUE;
    DrawRectangle(g_ui_cursor.x, g_ui_cursor.y + 1, button_width, LINE_HEIGHT - 2, color);
    Vector2 texpos = g_ui_cursor;
    texpos.x += (button_width - text_size.x) / 2.0f;
    DrawTextEx(FontAsset(), items[data->arbitrary_counter], texpos, LINE_HEIGHT, 0, MappedColor(UI_TEXT_COLOR));
    if (clicked) {
        g_dropdownmenu_data = (DropdownMenuData) {
            data, items, num_items, TRUE, button_width, param, func,
            (Vector2) { g_ui_cursor.x + g_ui_position.x, g_ui_cursor.y + g_ui_position.y }
        };
    }
    g_ui_cursor.y += LINE_HEIGHT;
    g_ui_cursor.x = 10;
}

void UITextInput_(PersistantUIData* data, const char* label, char* buffer, size_t size, size_t width) {
    constexpr float s_cursor_limit = 0.5f;
    DrawTextEx(FontAsset(), label, g_ui_cursor, LINE_HEIGHT, 0, MappedColor(UI_TEXT_COLOR));
    Vector2 text_size = MeasureTextEx(FontAsset(), label, LINE_HEIGHT, 0);
    float box_width = width - text_size.x - 10;
    if (box_width < 0) return;
    g_ui_cursor.x += text_size.x + 10;
    if (!g_scratch_target_in_use) {
        g_ui_scratch_target = LoadRenderTexture(EDITOR_DEFAULT_WIDTH, EDITOR_DEFAULT_HEIGHT);
        g_scratch_target_in_use = TRUE;
    }
    if (g_current_ui_target.id != 0) EndTextureMode();
    BeginTextureMode(g_ui_scratch_target);
    if (g_textinput_data.data == data) ClearBackground(MappedColor(UI_TEXT_INPUT_FOCUS_COLOR));
    else ClearBackground(MappedColor(UI_TEXT_INPUT_BG_COLOR));
    Vector2 buffer_text_size = MeasureTextEx(FontAsset(), buffer, LINE_HEIGHT, 0);
    float xstart = 2.0f;
    if (g_textinput_data.data == data && buffer_text_size.x > box_width) xstart -= (buffer_text_size.x - box_width) + 4.0f;
    DrawTextEx(FontAsset(), buffer, (Vector2){ xstart, 0 }, LINE_HEIGHT, 0, MappedColor(UI_TEXT_COLOR));
    if (g_textinput_data.data == data) {
        data->arbitrary_timer += GetFrameTime();
        if (data->arbitrary_timer < s_cursor_limit / 2.0f) {
            char b[1024] = { 0 };
            memcpy(b, buffer, g_textinput_data.cursor);
            DrawRectangle(MeasureTextEx(FontAsset(), b, LINE_HEIGHT, 0).x + xstart, 2, 2, LINE_HEIGHT - 5, MappedColor(UI_TEXT_COLOR));
        } else if (data->arbitrary_timer > s_cursor_limit) data->arbitrary_timer = 0.0f;
    }
    EndTextureMode();
    if (g_current_ui_target.id != 0) BeginTextureMode(g_current_ui_target);
    DrawTexturePro(
        g_ui_scratch_target.texture,
        (Rectangle){ 0, g_current_ui_target.texture.height - LINE_HEIGHT + 2, box_width, -(LINE_HEIGHT - 2) },
        (Rectangle){ g_ui_cursor.x, g_ui_cursor.y, box_width, LINE_HEIGHT - 2 },
        (Vector2){ 0, 0 },
        0.0f,
        (Color){ 255, 255, 255, 255 });
    if (CheckCollisionPointRec(
            GetMousePosition(),
            (Rectangle){g_ui_cursor.x + g_ui_position.x, g_ui_cursor.y + g_ui_position.y, box_width, LINE_HEIGHT - 2})) {
        if (InputButtonDown(IK_MOUSELEFT)) {
            g_textinput_data = (TextInputData){
                buffer, size, TRUE, data, strlen(buffer), 
                (Vector2){g_ui_cursor.x + g_ui_position.x, g_ui_cursor.y + g_ui_position.y}, box_width};
            data->arbitrary_timer = 0.0f;
        }
    }
    g_ui_cursor.x = 10;
    g_ui_cursor.y += LINE_HEIGHT;
}
