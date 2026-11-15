#include "baking.h"
#include "renderer/renderer.h"
#include "renderer/trainer.h"
#include <data/colors.h>
#include <ui/shared.h>
#include <ui/extra.h>

static char* g_mode_labels[] = { "scene gaussians", "subsurface cloud" };
static size_t g_bake_mode = 0;

static size_t DropdownSelectBakeMode(void* data, size_t index, BOOL cancel) {
    if (index == (size_t)-1) return g_bake_mode;
    g_bake_mode = index;
    return index;
}

static void DrawStatusStage(float width, int current, int target, int done, const char* fmt, const char* status, int soffset) {
    DrawRectangle(UIGetCursor().x, UIGetCursor().y, width - 20, 20, (Color){ 255, 255, current == target ? 0 : 255, current == target ? 90 : 60 });
    Color c = current == target ? GOLD : (current > target && current <= done ? GREEN : GRAY);
    DrawCircle(UIGetCursor().x + 15, UIGetCursor().y + 10, 5, c);
    UIMoveCursor(30, 0);
    UIDrawText(fmt, current == target ? "-" : "", current == target ? status + soffset : "");
    DrawRectangle(width - 10, UIGetCursor().y - 20, 10, 20, MappedColor(PANEL_BG_COLOR));
}

static void DrawSplatTrainingSection(float width, float height, float cursory) {
    static TrainingSceneConfig s_config;
    static BOOL s_initialized = FALSE;
    static size_t s_views = 0;
    static size_t s_samples = 0;
    static size_t s_points = 0;
    static size_t s_iterations = 0;
    static size_t s_maxgs = 0;
    static size_t s_trainwidth = 0;
    static BOOL s_pathtrace = TRUE;
    static BOOL s_replace = TRUE;
    float rcolw = width - LEFT_COLUMN_WIDTH - 20;
    if (!s_initialized) {
        s_config = TrainingSceneDefaultConfig();
        s_views = s_config.views;
        s_samples = s_config.samples;
        s_points = s_config.init_points;
        s_iterations = s_config.iterations;
        s_maxgs = s_config.max_gaussians;
        s_trainwidth = s_config.train_width;
        s_initialized = TRUE;
    }
    TrainingScenePhase phase = TrainingScenePhaseNow();
    BOOL running = phase == TRAINSCENE_CAPTURING || phase == TRAINSCENE_TRAINING;
    if (running) DisableUI();
    UIColumnHeader("Views", LEFT_COLUMN_WIDTH);
    UIDragSize(&s_views, 4, 500, 1, rcolw);
    UIColumnHeader("Image width", LEFT_COLUMN_WIDTH);
    UIDragSize(&s_trainwidth, 64, 1600, 8, rcolw);
    UIColumnHeader("Iterations", LEFT_COLUMN_WIDTH);
    UIDragSize(&s_iterations, 100, 100000, 100, rcolw);
    UIColumnHeader("Seed gaussians", LEFT_COLUMN_WIDTH);
    UIDragSize(&s_points, 100, 2000000, 500, rcolw);
    UIColumnHeader("Max gaussians", LEFT_COLUMN_WIDTH);
    UIDragSize(&s_maxgs, 1000, 4000000, 1000, rcolw);
    UIColumnHeader("Path traced views", LEFT_COLUMN_WIDTH);
    UICheckbox(&s_pathtrace);
    if (!s_pathtrace) DisableUI();
    UIColumnHeader("Samples per view", LEFT_COLUMN_WIDTH);
    UIDragSize(&s_samples, 1, 4096, 1, rcolw);
    if (!running) EnableUI();
    if (running) DisableUI();
    UIColumnHeader("Replace scene splats", LEFT_COLUMN_WIDTH);
    UICheckbox(&s_replace);
    UIMoveCursor(0, 30);
    UIDividerLabeled(width, "Status");
    EnableUI();
    UIMoveCursor(0, 5);
    DrawStatusStage(width, phase, TRAINSCENE_CAPTURING, TRAINSCENE_DONE, "Capturing %s %s", TrainingSceneStatus(), 16);
    DrawStatusStage(width, phase, TRAINSCENE_TRAINING, TRAINSCENE_DONE, "Training %s %s", TrainingSceneStatus(), 9);
    {
        DrawRectangle(UIGetCursor().x, UIGetCursor().y, width - 20, 20, (Color){ 255, 255, 255, 60 });
        Color c = phase == TRAINSCENE_DONE ? GREEN : GRAY;
        DrawCircle(UIGetCursor().x + 15, UIGetCursor().y + 10, 5, c);
        UIMoveCursor(30, 0);
        UIDrawText("%s%s", phase == TRAINSCENE_DONE ? "" : "Finish", phase == TRAINSCENE_DONE ? TrainingSceneStatus() : "");
        DrawRectangle(width - 10, UIGetCursor().y - 20, 10, 20, MappedColor(PANEL_BG_COLOR));
    }
    if (!running && UIButton("Start Training", width - 20)) {
        TrainingSceneConfig config = TrainingSceneDefaultConfig();
        config.views = (uint32_t)s_views;
        config.train_width = (uint32_t)s_trainwidth;
        config.iterations = (uint32_t)s_iterations;
        config.init_points = (uint32_t)s_points;
        config.max_gaussians = (uint32_t)s_maxgs;
        config.samples = (uint32_t)s_samples;
        config.capture = s_pathtrace ? TRAINSCENE_CAPTURE_PATHTRACE : TRAINSCENE_CAPTURE_PREVIEW;
        config.replace_splats = s_replace;
        TrainingSceneStart(&config);
    } else if (running) {
        if (UIButton("Cancel", width - 20.0f)) TrainingSceneCancel();
    }
}

static void DrawBakingPanel(float width, float height) {
    float cursory = UIGetCursor().y;
    float rcolw = width - LEFT_COLUMN_WIDTH - 20;
    UIDividerLabeled(width, "Bake Settings");
    UIMoveCursor(0, 5);
    UIColumnHeader("Bake Mode", LEFT_COLUMN_WIDTH);
    UIDropdownMenu(rcolw, 2, g_mode_labels, DropdownSelectBakeMode, NULL);
    if (g_bake_mode == 0) {
        DrawSplatTrainingSection(width, height, cursory);
    }
}

Panel GenerateBakingPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Baking");
	p.draw = DrawBakingPanel;
	p.scrollable = TRUE;
	return p;
}
