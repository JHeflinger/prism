#include "recording.h"
#include "renderer/renderer.h"
#include "data/defaults.h"
#include <util/logger.h>
#include <ui/extra.h>
#include <stdlib.h>
#include <ui/ui.h>

static char* g_mode_labels[] = { "image", "video" };
static size_t g_render_mode = 0;
static BOOL g_ffmpeg = FALSE;
static float g_progress = 0.0f;
static BOOL g_inprogress = FALSE;
static BOOL g_vonly = FALSE;
static float g_convergence = 0.0f;
static size_t g_samples = 500;
static char g_imagepath[2048] = "render.png";
static char g_videopath[2048] = "render.mp4";

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
    if (g_inprogress) {
        float curr = GetRenderStats().accumulation;
        g_progress = 100.0f * curr / (float)g_samples;
        if (RenderConverged(g_convergence / 100.0f)) g_progress = 100.0f;
    }
    UIColumnHeader("Save to...", LEFT_COLUMN_WIDTH);
    UIChooseFile(g_imagepath, 2048, rcolw, "png", FALSE);
    UIColumnHeader("Samples", LEFT_COLUMN_WIDTH);
    UIDragSize(&g_samples, 0, 9999999999, 1, rcolw);
    UIColumnHeader("Convergence", LEFT_COLUMN_WIDTH);
    UIDragFloat(&g_convergence, 0, 100.0f, 0.01f, rcolw);
    UIColumnHeader("Viewport Only", LEFT_COLUMN_WIDTH);
    UIMoveCursor(-2, 1);
    UICheckbox(&g_vonly);
    UIMoveCursor(0, -1);
    UIMoveCursor(0, 10);
    EnableUI();
    if (!g_inprogress && UIButton("Render", width - 20)) {
        g_inprogress = TRUE;
        g_progress = 0.0f;
        RenderConfig()->flags = PATHTRACE_PIPELINE_FLAGS;
        ResetAccumulation();
    } else if (g_inprogress && UIButton("Cancel", width - 20)) {
        g_inprogress = FALSE;
        g_progress = 0.0f;
    }
}

static void DrawVideoRenderSettings(float width, float height) {
    float rcolw = width - LEFT_COLUMN_WIDTH - 20;
    if (AvailableFFMPEG()) {
        UIColumnHeader("Save to...", LEFT_COLUMN_WIDTH);
        UIChooseFile(g_videopath, 2048, rcolw, "mp4", FALSE);
        UIColumnHeader("Samples", LEFT_COLUMN_WIDTH);
        UIDragSize(&g_samples, 0, 9999999999, 1, rcolw);
        UIColumnHeader("Convergence", LEFT_COLUMN_WIDTH);
        UIDragFloat(&g_convergence, 0, 100.0f, 0.01f, rcolw);
        UIColumnHeader("Viewport Only", LEFT_COLUMN_WIDTH);
        UIMoveCursor(-4, 1);
        UICheckbox(&g_vonly);
        UIMoveCursor(0, -1);
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
    if (g_inprogress) DisableUI();
    float rcolw = width - LEFT_COLUMN_WIDTH - 20;
    float cursory = UIGetCursor().y;
    UIDividerLabeled(width, "Render Settings");
    UIMoveCursor(0, 5);
    UIColumnHeader("Render Mode", LEFT_COLUMN_WIDTH);
    UIDropdownMenu(rcolw, 2, g_mode_labels, DropdownSelectRenderMode, NULL);
    if (g_render_mode == 0) {
        DrawImageRenderSettings(width);
    } else {
        DrawVideoRenderSettings(width, height);
    }
    float drawnheight = UIGetCursor().y - cursory;
    if (drawnheight > height - 70) {
        UIMoveCursor(0, 20);
        UIProgressBar(width - 20, g_progress);
    } else {
        Vector2 backup = UIGetCursor();
        UISetCursor(10, height - 60);
        UIProgressBar(width - 20, g_progress);
        UISetCursor(backup.x, backup.y);
    }
    EnableUI();
}

static void UpdateRecordingPanel(float width, float height) {
    if (g_inprogress && g_progress >= 100.0f) {
        if (g_vonly) {
            SaveViewportRender("render.png");
        } else {
            SaveRender("render.png");
        }
        loginfo("Finished rendering image! Saved to %s", g_imagepath);
        g_inprogress = FALSE;
    }
}

BOOL RequestingIgnoreScissor() {
    return g_inprogress && !g_vonly;
}

Panel GenerateRecordingPanel() {
    g_ffmpeg = PollFFMPEG();
	Panel p = { 0 };
	SetupPanel(&p, "Recording");
	p.draw = DrawRecordingPanel;
    p.update = UpdateRecordingPanel;
    p.scrollable = TRUE;
	return p;
}
