#include "baking.h"
#include "renderer/subsurface.h"
#include "renderer/renderer.h"
#include "renderer/trainer.h"
#include "ui/panels/edit.h"
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

static void DrawStatusSubStage(float width, int current, int target, int done, const char* fmt, const char* status, int soffset, BOOL first) {
    DrawRectangle(UIGetCursor().x, UIGetCursor().y, width - 20, 20, (Color){ 255, 255, current == target ? 0 : 255, current == target ? 90 : 60 });
    UIMoveCursor(10, 0);
    Color c = current == target ? GOLD : (current > target && current <= done ? GREEN : GRAY);
    DrawRectangle(UIGetCursor().x + 5, UIGetCursor().y + 9, 10, 2, c);
    if (first) DrawRectangle(UIGetCursor().x + 4, UIGetCursor().y - 5, 2, 16, c);
    else DrawRectangle(UIGetCursor().x + 4, UIGetCursor().y - 9, 2, 20, c);
    DrawCircle(UIGetCursor().x + 15, UIGetCursor().y + 10, 5, c);
    UIMoveCursor(30, 0);
    UIDrawText(fmt, current == target ? "-" : "", current == target ? status + soffset : "");
    DrawRectangle(width - 10, UIGetCursor().y - 20, 10, 20, MappedColor(PANEL_BG_COLOR));
}

static void DrawStatusStage(float width, int current, int target, int done, const char* fmt, const char* status, int soffset) {
    DrawRectangle(UIGetCursor().x, UIGetCursor().y, width - 20, 20, (Color){ 255, 255, current == target ? 0 : 255, current == target ? 90 : 60 });
    Color c = current == target ? GOLD : (current > target && current <= done ? GREEN : GRAY);
    DrawCircle(UIGetCursor().x + 15, UIGetCursor().y + 10, 5, c);
    UIMoveCursor(30, 0);
    UIDrawText(fmt, current == target ? "-" : "", current == target ? status + soffset : "");
    DrawRectangle(width - 10, UIGetCursor().y - 20, 10, 20, MappedColor(PANEL_BG_COLOR));
}

static void DrawSplatTrainingSection(float width) {
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

static void DrawCloudTrainingSection(float width) {
    static BOOL s_init = FALSE;
    static size_t s_surface = 0;
    static size_t s_internal = 0;
    static size_t s_walks = 0;
    static size_t s_lobes = 0;
    if (!s_init) {
        GSSSConfig d = SubsurfaceDefaultConfig();
        s_surface = d.surface_gaussians;
        s_internal = d.internal_gaussians;
        s_walks = d.walks;
        s_lobes = d.lobes;
        s_init = TRUE;
    }
    float rcolw = width - LEFT_COLUMN_WIDTH - 20;
    EditType etype = 0;
    size_t mesh = 0;
    BOOL selected = GetEditTarget(&etype, &mesh);
    SubsurfaceTrainPhase phase = SubsurfaceTrainPhaseNow();
    SubsurfaceCloudInfo info = { 0 };
    BOOL training = phase == SUBSURFACE_TRAINING;
    BOOL ours = training && selected && SubsurfaceTrainMesh() == mesh;
    BOOL linked = selected && QuerySubsurfaceCloudInfo(mesh, &info);
    if (training) DisableUI();
    UIColumnHeader("Surface Gaussians", LEFT_COLUMN_WIDTH);
    UIDragSize(&s_surface, 16, 65536, 8, rcolw);
    UIColumnHeader("Exit Lobes", LEFT_COLUMN_WIDTH);
    UIDragSize(&s_lobes, 1, 8, 1, rcolw);
    UIColumnHeader("Internal Gaussians", LEFT_COLUMN_WIDTH);
    UIDragSize(&s_internal, 16, 262144, 16, rcolw);
    UIColumnHeader("Reference Walks", LEFT_COLUMN_WIDTH);
    UIDragSize(&s_walks, 10000, 20000000, 10000, rcolw);
    UIMoveCursor(0, 30);
    UIDividerLabeled(width, "Status");
    EnableUI();
    UIMoveCursor(0, 5);
    int channel = phase == SUBSURFACE_IDLE ? -1 : (phase == SUBSURFACE_DONE ? 3 : SubsurfaceTrainChannelNow());
    int fitphase = phase == SUBSURFACE_TRAINING ? (int)SubsurfaceFittingPhaseNow() : (phase == SUBSURFACE_DONE ? 9999 : -1);
    DrawStatusStage(width, channel, 0, 3, "Red channel %s %s", "actively training", 0);
    DrawStatusSubStage(width, channel * 100 + fitphase, 0 * 100 + SSSFIT_SETUP, 99999, "Setup %s %s", SubsurfaceTrainStatus(), 0, TRUE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 0 * 100 + SSSFIT_ORIENT, 99999, "Orientation %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 0 * 100 + SSSFIT_SURFACE, 99999, "Surfaces %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 0 * 100 + SSSFIT_WALKS, 99999, "Walks %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 0 * 100 + SSSFIT_LOBES, 99999, "Lobes %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 0 * 100 + SSSFIT_CLOUD, 99999, "Cloud %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 0 * 100 + SSSFIT_CHORDS, 99999, "Chords %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 0 * 100 + SSSFIT_OPTIMISE, 99999, "Optimization %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 0 * 100 + SSSFIT_VALIDATE, 99999, "Validation %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusStage(width, channel, 1, 3, "Green channel %s %s", "actively training", 0);
    DrawStatusSubStage(width, channel * 100 + fitphase, 1 * 100 + SSSFIT_SETUP, 99999, "Setup %s %s", SubsurfaceTrainStatus(), 0, TRUE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 1 * 100 + SSSFIT_ORIENT, 99999, "Orientation %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 1 * 100 + SSSFIT_SURFACE, 99999, "Surfaces %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 1 * 100 + SSSFIT_WALKS, 99999, "Walks %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 1 * 100 + SSSFIT_LOBES, 99999, "Lobes %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 1 * 100 + SSSFIT_CLOUD, 99999, "Cloud %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 1 * 100 + SSSFIT_CHORDS, 99999, "Chords %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 1 * 100 + SSSFIT_OPTIMISE, 99999, "Optimization %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 1 * 100 + SSSFIT_VALIDATE, 99999, "Validation %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusStage(width, channel, 2, 3, "Blue channel %s %s", "actively training", 0);
    DrawStatusSubStage(width, channel * 100 + fitphase, 2 * 100 + SSSFIT_SETUP, 99999, "Setup %s %s", SubsurfaceTrainStatus(), 0, TRUE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 2 * 100 + SSSFIT_ORIENT, 99999, "Orientation %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 2 * 100 + SSSFIT_SURFACE, 99999, "Surfaces %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 2 * 100 + SSSFIT_WALKS, 99999, "Walks %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 2 * 100 + SSSFIT_LOBES, 99999, "Lobes %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 2 * 100 + SSSFIT_CLOUD, 99999, "Cloud %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 2 * 100 + SSSFIT_CHORDS, 99999, "Chords %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 2 * 100 + SSSFIT_OPTIMISE, 99999, "Optimization %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    DrawStatusSubStage(width, channel * 100 + fitphase, 2 * 100 + SSSFIT_VALIDATE, 99999, "Validation %s %s", SubsurfaceTrainStatus(), 0, FALSE);
    {
        DrawRectangle(UIGetCursor().x, UIGetCursor().y, width - 20, 20, (Color){ 255, 255, 255, 60 });
        Color c = phase == SUBSURFACE_DONE ? GREEN : GRAY;
        DrawCircle(UIGetCursor().x + 15, UIGetCursor().y + 10, 5, c);
        UIMoveCursor(30, 0);
        UIDrawText("%s%s", phase == SUBSURFACE_DONE ? "" : "Finish", phase == SUBSURFACE_DONE ? SubsurfaceTrainStatus() : "");
        DrawRectangle(width - 10, UIGetCursor().y - 20, 10, 20, MappedColor(PANEL_BG_COLOR));
    }
    if (!selected) DisableUI();
    if (ours) {
        if (UIButton("Cancel", width - 20)) SubsurfaceTrainCancel();
    } else if (training) {
        DisableUI();
        UIButton("Unavailable", width - 20); // turn this into a "queue" later?
    } else if (UIButton(linked ? "Retrain Subsurface Splats" : "Train Subsurface Splats", width - 20)) {
        GSSSConfig cfg = SubsurfaceDefaultConfig();
        cfg.surface_gaussians = (uint32_t)s_surface;
        cfg.lobes = (uint32_t)s_lobes;
        cfg.internal_gaussians = (uint32_t)s_internal;
        cfg.walks = (uint32_t)s_walks;
        SubsurfaceTrainStart(mesh, &cfg);
    }
    EnableUI();
    if (linked) {
        UIMoveCursor(0, 30);
        UIDividerLabeled(width, "Statistics");
        UIMoveCursor(0, 5);
        UIColumnHeader("Channels", LEFT_COLUMN_WIDTH);
        DrawRectangle(UIGetCursor().x, UIGetCursor().y, rcolw, 20, (Color){ 0, 0, 0, 245 });
        UIDrawText("%u", info.channels);
        UIColumnHeader("Surface splats", LEFT_COLUMN_WIDTH);
        DrawRectangle(UIGetCursor().x, UIGetCursor().y, rcolw, 20, (Color){ 0, 0, 0, 245 });
        UIDrawText("%u", info.surface_gaussians);
        UIColumnHeader("Lobes", LEFT_COLUMN_WIDTH);
        DrawRectangle(UIGetCursor().x, UIGetCursor().y, rcolw, 20, (Color){ 0, 0, 0, 245 });
        UIDrawText("%u", info.lobes);
        UIColumnHeader("Internal splats", LEFT_COLUMN_WIDTH);
        DrawRectangle(UIGetCursor().x, UIGetCursor().y, rcolw, 20, (Color){ 0, 0, 0, 245 });
        UIDrawText("%u", info.internal_gaussians);
        UIColumnHeader("Memory", LEFT_COLUMN_WIDTH);
        DrawRectangle(UIGetCursor().x, UIGetCursor().y, rcolw, 20, (Color){ 0, 0, 0, 245 });
        UIDrawText("%.0f KB", info.bytes / 1024.0f);
    }
}

static void DrawBakingPanel(float width, float height) {
    float rcolw = width - LEFT_COLUMN_WIDTH - 20;
    UIDividerLabeled(width, "Bake Settings");
    UIMoveCursor(0, 5);
    UIColumnHeader("Bake Mode", LEFT_COLUMN_WIDTH);
    UIDropdownMenu(rcolw, 2, g_mode_labels, DropdownSelectBakeMode, NULL);
    if (g_bake_mode == 0) {
        DrawSplatTrainingSection(width);
    } else {
        DrawCloudTrainingSection(width);
    }
}

Panel GenerateBakingPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Baking");
	p.draw = DrawBakingPanel;
	p.scrollable = TRUE;
	return p;
}
