#include "actions.h"
#include "renderer/renderer.h"
#include "renderer/trainer.h"
#include "ui/shared.h"


static void DrawSplatTrainingSection(float width) {
    static TrainingSceneConfig s_config;
    static BOOL s_initialized = FALSE;
    static size_t s_views = 0, s_samples = 0, s_points = 0, s_iterations = 0, s_maxgs = 0, s_trainwidth = 0;
    static BOOL s_pathtrace = TRUE;
    static BOOL s_replace = TRUE;
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
    float half = (width - 20.0f) / 2.0f;
    TrainingScenePhase phase = TrainingScenePhaseNow();
    BOOL running = phase == TRAINSCENE_CAPTURING || phase == TRAINSCENE_TRAINING;
    UIDrawText("Train Gaussian Splats");
    UIDivider(width - 20);
    if (running) DisableUI();
    UIDrawText("Views");
    UIMoveCursor(half, -20);
    UIDragSize(&s_views, 4, 500, 1, half);
    UIMoveCursor(0, 5);
    UIDrawText("Image width");
    UIMoveCursor(half, -20);
    UIDragSize(&s_trainwidth, 64, 1600, 8, half);
    UIMoveCursor(0, 5);
    UIDrawText("Iterations");
    UIMoveCursor(half, -20);
    UIDragSize(&s_iterations, 100, 100000, 100, half);
    UIMoveCursor(0, 5);
    UIDrawText("Seed gaussians");
    UIMoveCursor(half, -20);
    UIDragSize(&s_points, 100, 2000000, 500, half);
    UIMoveCursor(0, 5);
    UIDrawText("Max gaussians");
    UIMoveCursor(half, -20);
    UIDragSize(&s_maxgs, 1000, 4000000, 1000, half);
    UIMoveCursor(0, 5);
    UIDrawText("Path traced views");
    UIMoveCursor(half, -20);
    UICheckbox(&s_pathtrace);
    UIMoveCursor(0, 5);
    if (!s_pathtrace) DisableUI();
    UIDrawText("Samples per view");
    UIMoveCursor(half, -20);
    UIDragSize(&s_samples, 1, 4096, 1, half);
    if (!running) EnableUI();
    if (running) DisableUI();
    UIMoveCursor(0, 5);
    UIDrawText("Replace scene splats");
    UIMoveCursor(half, -20);
    UICheckbox(&s_replace);
    UIMoveCursor(0, 10);
    if (UIButton("Start Training", width - 20.0f)) {
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
    }
    EnableUI();
    if (running) {
        if (UIButton("Cancel", width - 20.0f)) TrainingSceneCancel();
    }
    UIMoveCursor(0, 5);
    if (phase != TRAINSCENE_IDLE) {
        UIDrawText(TrainingSceneStatus());
    }
}

static void DrawActionsPanel(float width, float height) {
    DrawSplatTrainingSection(width);
}

Panel GenerateActionsPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Actions");
	p.draw = DrawActionsPanel;
	p.scrollable = TRUE;
	return p;
}
