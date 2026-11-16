#include "recording.h"
#include "data/defaults.h"
#include <util/logger.h>
#include <ui/extra.h>
#include <stdlib.h>
#include <ui/ui.h>

static char* g_mode_labels[] = { "image", "video" };
static size_t g_render_mode = 0;
static BOOL g_ffmpeg = FALSE;

static BOOL PollFFMPEG() {
    #ifdef _WIN32
        return system("where ffmpeg >NUL 2>&1") == 0;
    #else
        return system("command -v ffmpeg >/dev/null 2>&1") == 0;
    #endif
}

static BOOL AvailableFFMPEG() {
    return g_ffmpeg;
}

static size_t DropdownSelectRenderMode(void* data, size_t index, BOOL cancel) {
    if (index == (size_t)-1) return g_render_mode;
    g_render_mode = index;
    if (index == 1) {
        g_ffmpeg = PollFFMPEG();
        if (!g_ffmpeg) logwarn("ffmpeg was not detected on your path - unable to do video encoding without it!");
    }
    return index;
}

static void DrawImageRenderSettings(float width) {
    float rcolw = width - LEFT_COLUMN_WIDTH - 20;
    static size_t w = 1600;
    static size_t h = 900;
    static BOOL vonly = FALSE;
    static float conv = 0.0f;
    UIColumnHeader("Save to...", LEFT_COLUMN_WIDTH);
    UIDrawText("TODO");
    UIColumnHeader("Samples", LEFT_COLUMN_WIDTH);
    UIDrawText("TODO");
    UIColumnHeader("Convergence", LEFT_COLUMN_WIDTH);
    UIDragFloat(&conv, 0, 100.0f, 0.01f, rcolw);
    UIColumnHeader("Viewport Only", LEFT_COLUMN_WIDTH);
    UIMoveCursor(-4, 1);
    UICheckbox(&vonly);
    UIMoveCursor(0, -1);
    UIColumnHeader("Resolution", LEFT_COLUMN_WIDTH);
    UIDoublet("wh", UI_SIZES, &w, &h, (UIMultiValue){ ._size = 10 }, (UIMultiValue){ ._size = 8000 }, (UIMultiValue){ ._size = 1 }, (UIMultiValue){ ._size = 900 }, rcolw + 20);
}

static void DrawVideoRenderSettings(float width, float height) {
    if (AvailableFFMPEG()) {
        UIColumnHeader("Save to...", LEFT_COLUMN_WIDTH);
        UIDrawText("TODO");
        UIColumnHeader("Samples", LEFT_COLUMN_WIDTH);
        UIDrawText("TODO");
        UIColumnHeader("Convergence", LEFT_COLUMN_WIDTH);
        UIDrawText("TODO");
        UIColumnHeader("Viewport Only", LEFT_COLUMN_WIDTH);
        UIDrawText("TODO");
        UIColumnHeader("Resolution", LEFT_COLUMN_WIDTH);
        UIDrawText("TODO");
        UIColumnHeader("FPS", LEFT_COLUMN_WIDTH);
        UIDrawText("TODO");
        UIColumnHeader("Duration", LEFT_COLUMN_WIDTH);
        UIDrawText("TODO");
    } else {
        UISetCursor((width - UITextWidth("FFMPEG Not Available"))/2.0f, height / 2.0f - 20);
        UIDrawText("FFMPEG Not Available");
    }
}

static void DrawRecordingPanel(float width, float height) {
    float rcolw = width - LEFT_COLUMN_WIDTH - 20;
    UIDividerLabeled(width, "Render Settings");
    UIMoveCursor(0, 5);
    UIColumnHeader("Render Mode", LEFT_COLUMN_WIDTH);
    UIDropdownMenu(rcolw, 2, g_mode_labels, DropdownSelectRenderMode, NULL);
    if (g_render_mode == 0) {
        DrawImageRenderSettings(width);
    } else {
        DrawVideoRenderSettings(width, height);
    }
}

Panel GenerateRecordingPanel() {
    g_ffmpeg = PollFFMPEG();
	Panel p = { 0 };
	SetupPanel(&p, "Recording");
	p.draw = DrawRecordingPanel;
    p.scrollable = TRUE;
	return p;
}
