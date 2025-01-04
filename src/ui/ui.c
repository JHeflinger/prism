#include "ui.h"
#include "core/log.h"
#include "data/input.h"
#include "raylib.h"
#include "easymemory.h"

UI* GenerateUI() {
    UI* ui = EZALLOC(1, sizeof(UI));
    return ui;
}

void UpdateUI(UI* ui) {   
    LOG_ASSERT((ui->left && ui->right) || (!ui->left && !ui->right), "UI branches must be split evenly");
    if (ui->left && ui->right) {
        UpdateUI((UI*)(ui->left));
        UpdateUI((UI*)(ui->right));
    }
    if (InputKeyDown(IK_DEV) &&
        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || 
        IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) &&
        CheckCollisionPointRec(
            GetMousePosition(), 
            (Rectangle){ui->x, ui->y, ui->w, ui->h}) &&
        !ui->left &&
        !ui->right
        ) {
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
}

void DrawUI(UI* ui, size_t x, size_t y, size_t w, size_t h) {
    LOG_ASSERT((ui->left && ui->right) || (!ui->left && !ui->right), "UI branches must be split evenly");
    ui->w = w;
    ui->h = h;
    ui->x = x;
    ui->y = y;

    if (ui->left && ui->right) {
        if (ui->vertical) {
            DrawUI((UI*)(ui->left), x, y, w, ui->divide);
            DrawUI((UI*)(ui->right), x, y + ui->divide, w, h - ui->divide);
        } else {
            DrawUI((UI*)(ui->left), x, y, ui->divide, h);
            DrawUI((UI*)(ui->right), x + ui->divide, y, w - ui->divide, h);
        }
    } else {
        DrawRectangle(x, y, w, h, RED);
        size_t th = 2;
        DrawLineEx((Vector2){x, y + (th/2)}, (Vector2){x + w, y + (th/2)}, th, GREEN);
        DrawLineEx((Vector2){x + (th/2), y}, (Vector2){x + (th/2), y + h}, th, GREEN);
        DrawLineEx((Vector2){x + w - (th/2), y + h}, (Vector2){x + w - (th/2), y}, th, GREEN);
        DrawLineEx((Vector2){x + w, y + h - (th/2)}, (Vector2){x, y + h - (th/2)}, th, GREEN);
    }
}

void DestroyUI(UI* ui) {
    if (ui->left) DestroyUI((UI*)ui->left);
    if (ui->right) DestroyUI((UI*)ui->right);
    EZFREE(ui);
}