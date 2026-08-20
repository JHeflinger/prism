#include "editor.h"
#include <core/config.h>
#include "data/defaults.h"
#include "data/input.h"
#include "data/colors.h"
#include "data/fonts.h"
#include "ui/ui.h"
#include "ui/panels/diagnostics.h"
#include "ui/panels/console.h"
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
#include "core/extender.h"
#include <raylib.h>
#include <easymemory.h>
#include <easylogger.h>

static UI* g_ui = NULL;
static Vector2 g_windowsize = { -1.0f, -1.0f };
static ARRLIST_Panel g_shared_panels = { 0 };

static void InitEditor() {
	SetTraceLogLevel(LOG_NONE);
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(EDITOR_DEFAULT_WIDTH, EDITOR_DEFAULT_HEIGHT, "Prism");
    Image icon = LoadImage("assets/images/appico.png");
    SetWindowIcon(icon);
    UnloadImage(icon);
    InitializeInput();
    InitializeColors();
    InitializeFonts();
    InitializeRenderer();
    printf("\nEnvironment configuration:\n\tGPU: %s\n\tOperating System: %s\n\n", GPUModel(), OPSYS);
    ARRLIST_Panel_add(&g_shared_panels, GenerateDiagnosticsPanel());
    ARRLIST_Panel_add(&g_shared_panels, GenerateSimulatePanel());
    ARRLIST_Panel_add(&g_shared_panels, GenerateOverviewPanel());
    ARRLIST_Panel_add(&g_shared_panels, GenerateActionsPanel());
    ARRLIST_Panel_add(&g_shared_panels, GenerateViewportPanel());
    ARRLIST_Panel_add(&g_shared_panels, GenerateEditPanel());
    ARRLIST_Panel_add(&g_shared_panels, GenerateMeshPanel());
    ARRLIST_Panel_add(&g_shared_panels, GenerateGraphPanel());
    ARRLIST_UIConfig default_config = { 0 };
    ARRLIST_UIConfig_add(&default_config, (UIConfig){{ 0 }, 1250.0f, FALSE, TRUE, TRUE, FALSE}); // root
    ARRLIST_UIConfig_add(&default_config, (UIConfig){{ 0 }, 350.0f, FALSE, TRUE, TRUE, FALSE}); // [ scenes + assets + scripts | graph ] | viewport container
    ARRLIST_UIConfig_add(&default_config, (UIConfig){{ 0 }, GetScreenHeight() - 420.0f, TRUE, TRUE, TRUE, FALSE}); // scenes + assets + ascripts | graph container
    ARRLIST_UIConfig_add(&default_config, (UIConfig){"Edit Selected", 0.0f, FALSE, FALSE, FALSE, TRUE}); // scenes +
    ARRLIST_UIConfig_add(&default_config, (UIConfig){"Mesh", 0.0f, FALSE, FALSE, FALSE, TRUE}); // + assets +
    ARRLIST_UIConfig_add(&default_config, (UIConfig){"Simulate", 0.0f, FALSE, FALSE, FALSE, FALSE}); // + scripts
    ARRLIST_UIConfig_add(&default_config, (UIConfig){"Profiling", 0.0f, FALSE, FALSE, FALSE, FALSE}); // graph
    ARRLIST_UIConfig_add(&default_config, (UIConfig){"Viewport", 0.0f, FALSE, FALSE, FALSE, FALSE}); // viewport
    ARRLIST_UIConfig_add(&default_config, (UIConfig){{ 0 }, GetScreenHeight() - 360.0f, TRUE, TRUE, TRUE, FALSE}); // edit | console container
    ARRLIST_UIConfig_add(&default_config, (UIConfig){"Overview", 0.0f, FALSE, FALSE, FALSE, FALSE}); // edit
    ARRLIST_UIConfig_add(&default_config, (UIConfig){"Diagnostics", 0.0f, FALSE, FALSE, FALSE, FALSE}); // console
    SetUIConfig(&default_config);
    ARRLIST_UIConfig_clear(&default_config);
    LoadUIConfig(&g_ui, g_shared_panels);
    DevInitialize();
}

static void UpdateEditor() {
    UpdateUI(g_ui);
}

static void PreRenderEditor() {
    PreRenderUI(g_ui);
}

static void DrawEditor() {
    ClearBackground(RAYWHITE);
    DrawUI(g_ui, 0, 0, GetScreenWidth(), GetScreenHeight());
}

static void EditorResized() {
    if ((g_windowsize.x == -1.0f && g_windowsize.y == -1.0f) ||
        (g_windowsize.x != GetScreenWidth() || g_windowsize.y != GetScreenHeight())) {
        g_windowsize.x = GetScreenWidth();
        g_windowsize.y = GetScreenHeight();
        ResizeUI(g_ui);
    }
}

static void CleanEditor() {
    CleanBinds();
    SaveUIConfig(g_ui);
    DestroyUI(g_ui);
    DestroyFonts();
    DestroyRenderer();
    CleanConfig();
	for (size_t i = 0; i < g_shared_panels.size; i++) DestroyPanel(&(g_shared_panels.data[i]));
    ARRLIST_Panel_clear(&g_shared_panels);
    CleanConsoleLogs();
    CleanNotifications();
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
    while (!WindowShouldClose()) {
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

        // resize callback
        EditorResized();
    }

    // Close game
    CleanEditor();

    // Clean memory check
    #ifndef PROD_BUILD
    EZ_ASSERT(memcheck == EZ_ALLOCATED(), "Memory cleanup revealed a leak of %d bytes", (int)(EZ_ALLOCATED() - memcheck));
    #endif
}
