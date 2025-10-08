#include "editor.h"
#include "data/config.h"
#include "data/input.h"
#include "data/colors.h"
#include "data/assets.h"
#include <easylogger.h>
#include "ui/ui.h"
#include "ui/panels/diagnostics.h"
#include "ui/panels/viewport.h"
#include "ui/panels/overview.h"
#include "ui/panels/edit.h"
#include "renderer/renderer.h"
#include <raylib.h>
#include <easymemory.h>

UI* g_ui = NULL;

void InitEditor() {
	SetTraceLogLevel(LOG_NONE);
    SetConfigFlags(FLAG_VSYNC_HINT /*| FLAG_WINDOW_RESIZABLE*/);
    InitWindow(EDITOR_DEFAULT_WIDTH, EDITOR_DEFAULT_HEIGHT, "Prism");
    Image icon = LoadImage("assets/images/appico.png");
    SetWindowIcon(icon);
    UnloadImage(icon);
    InitializeInput();
    InitializeColors();
    InitializeAssets();
    InitializeRenderer();
    g_ui = GenerateUI();
    g_ui->left = GenerateUI();
    g_ui->right = GenerateUI();
    ((UI*)g_ui->right)->right = GenerateUI();
    ((UI*)g_ui->right)->left = GenerateUI();
    ((UI*)g_ui->right)->divide = GetScreenHeight() - 540;
    ((UI*)g_ui->right)->vertical = TRUE;
    ((UI*)g_ui->left)->right = GenerateUI();
    ((UI*)g_ui->left)->left = GenerateUI();
    ((UI*)g_ui->left)->divide = 300;
    ConfigureDiagnosticsPanel(&(((UI*)(((UI*)g_ui->right)->right))->panel));
    ConfigureOverviewPanel(&(((UI*)(((UI*)g_ui->right)->left))->panel));
    ConfigureViewportPanel(&(((UI*)(((UI*)g_ui->left)->right))->panel));
    ConfigureEditPanel(&(((UI*)(((UI*)g_ui->left)->left))->panel));
    g_ui->divide = (3 * GetScreenWidth())/4;
}

void UpdateEditor() {
    UpdateUI(g_ui);
}

void PreRenderEditor() {
    PreRenderUI(g_ui);
}

void DrawEditor() {
    ClearBackground(RAYWHITE);
    DrawUI(g_ui, 0, 0, GetScreenWidth(), GetScreenHeight());
}

void CleanEditor() {
    DestroyUI(g_ui);
    DestroyAssets();
    DestroyRenderer();
    CloseWindow();
}

void RunEditor() {
    // Record memory status for clean check
    #ifndef PROD_BUILD
    size_t memcheck = EZ_ALLOCATED();
    #endif

    // Initialize editor
    InitEditor();

    // Run editor
    while(!WindowShouldClose()) {
        // update editor
        UpdateEditor();

        // prerender steps
        PreRenderEditor();

        // draw editor
        BeginDrawing();
        DrawEditor();
        EndDrawing();
    }

    // Close game
    CleanEditor();

    // Clean memory check
    EZ_ASSERT(memcheck == EZ_ALLOCATED(), "Memory cleanup revealed a leak of %d bytes", (int)(EZ_ALLOCATED() - memcheck));
}
