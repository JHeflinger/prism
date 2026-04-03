#include "editor.h"
#include "data/config.h"
#include "data/input.h"
#include "data/colors.h"
#include "data/assets.h"
#include "ui/ui.h"
#include "ui/panels/diagnostics.h"
#include "ui/panels/simulate.h"
#include "ui/panels/viewport.h"
#include "ui/panels/overview.h"
#include "ui/panels/edit.h"
#include "ui/panels/mesh.h"
#include "ui/panels/actions.h"
#include "ui/panels/graph.h"
#include "renderer/renderer.h"
#include "core/dev.h"
#include "core/binds.h"
#include <raylib.h>
#include <easymemory.h>
#include <easylogger.h>

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
    printf("\nEnvironment configuration:\n\tGPU: %s\n\tOperating System: %s\n\n", GPUModel(), OPSYS);
    g_ui = GenerateUI();
    g_ui->left = GenerateUI();
    g_ui->right = GenerateUI();
    ((UI*)g_ui->right)->right = GenerateUI();
    ((UI*)g_ui->right)->left = GenerateUI();
    ((UI*)g_ui->right)->divide = GetScreenHeight() - 560;
    ((UI*)g_ui->right)->vertical = TRUE;
    ((UI*)g_ui->left)->right = GenerateUI();
    ((UI*)g_ui->left)->left = GenerateUI();
    ((UI*)((UI*)g_ui->left)->left)->left = GenerateUI();
    ((UI*)((UI*)g_ui->left)->left)->right = GenerateUI();
    ((UI*)((UI*)g_ui->left)->left)->divide = GetScreenHeight() - 420;
    ((UI*)((UI*)g_ui->left)->left)->vertical = TRUE;
    ((UI*)g_ui->left)->divide = 350;
    ARRLIST_Panel_add(&(((UI*)(((UI*)g_ui->right)->right))->panels), GenerateSimulatePanel());
    ARRLIST_Panel_add(&(((UI*)(((UI*)g_ui->right)->right))->panels), GenerateDiagnosticsPanel());
    ARRLIST_Panel_add(&(((UI*)(((UI*)g_ui->right)->left))->panels), GenerateOverviewPanel());
    ARRLIST_Panel_add(&(((UI*)(((UI*)g_ui->right)->left))->panels), GenerateActionsPanel());
    ARRLIST_Panel_add(&(((UI*)(((UI*)g_ui->left)->right))->panels), GenerateViewportPanel());
    ARRLIST_Panel_add(&(GetLeftUI(GetLeftUI(GetLeftUI(g_ui)))->panels), GenerateEditPanel());
    ARRLIST_Panel_add(&(GetLeftUI(GetLeftUI(GetLeftUI(g_ui)))->panels), GenerateMeshPanel());
    ARRLIST_Panel_add(&(GetRightUI(GetLeftUI(GetLeftUI(g_ui)))->panels), GenerateGraphPanel());
    g_ui->divide = 1250;
    SetPrimaryUI(g_ui);
    DevInitialize();
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
    CleanBinds();
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
        // dev overrides
        DevUpdate();

        // update editor
        UpdateEditor();

        // poll binds
        if (!UIRequestsBlockInput()) ListenBinds();

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
    #ifndef PROD_BUILD
    EZ_ASSERT(memcheck == EZ_ALLOCATED(), "Memory cleanup revealed a leak of %d bytes", (int)(EZ_ALLOCATED() - memcheck));
    #endif
}
