#include "editor.h"
#include "data/config.h"
#include "data/input.h"
#include "data/colors.h"
#include "data/assets.h"
#include "core/log.h"
#include "ui/ui.h"
#include "ui/panels/diagnostics.h"
#include "ui/panels/viewport.h"
#include "renderer/renderer.h"
#include <raylib.h>
#include <easymemory.h>

UI* g_ui = NULL;

void InitEditor() {
	SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(EDITOR_DEFAULT_WIDTH, EDITOR_DEFAULT_HEIGHT, "Prism");
    InitializeInput();
    InitializeColors();
    InitializeAssets();
    InitializeRenderer();
    g_ui = GenerateUI();
    g_ui->left = GenerateUI();
    g_ui->right = GenerateUI();
    ConfigureDiagnosticsPanel(&(((UI*)(g_ui->right))->panel));
    ConfigureViewportPanel(&(((UI*)(g_ui->left))->panel));
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
    size_t memcheck = EZALLOCATED();
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
    LOG_ASSERT(memcheck == EZALLOCATED(), "Memory cleanup revealed a leak of %d bytes", (int)(EZALLOCATED() - memcheck));
}
