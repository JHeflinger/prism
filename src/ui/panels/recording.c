#include "recording.h"
#include "renderer/animation.h"
#include "renderer/renderer.h"
#include "data/defaults.h"
#include <util/logger.h>
#include <ui/extra.h>
#include <stdlib.h>
#include <ui/ui.h>

typedef struct {
    FILE* pipe;
    int width;
    int height;
    int fps;
    int frames;
} VideoEncoder;

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
static float g_fps = 30.0f;
static float g_duration = 5.0f;
static VideoEncoder g_encoder = { 0 };
static float g_timer = 0.0f;

static BOOL VideoStart(VideoEncoder* rec, const char* filename, int width, int height, int fps) {
    rec->width = width;
    rec->height = height;
    rec->fps = fps;
    rec->frames = 0;
    char command[1024] = { 0 };
    snprintf(command, sizeof(command),
        "ffmpeg -y "
        "-loglevel error "
        "-nostats "
        "-f rawvideo "
        "-pixel_format rgba "
        "-video_size %dx%d "
        "-framerate %d "
        "-i - "
        "-an "
        "-c:v libx264 "
        "-preset fast "
        "-crf 18 "
        "-pix_fmt yuv420p "
        "\"%s\"",
        width, height, fps, filename
    );
    #ifdef _WIN32
        rec->pipe = _popen(command, "wb");
    #else
        rec->pipe = popen(command, "w");
    #endif
    return rec->pipe != NULL;
}

static void VideoRecordFrame(VideoEncoder* rec, Image image) {
    if (!rec->pipe) return;
    if (!IsImageValid(image)) return;
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    size_t bytes = (size_t)rec->width * rec->height * 4;
    size_t written = fwrite(image.data, 1, bytes, rec->pipe);
    if (written == bytes) rec->frames++;
}

static void VideoRecorderStop(VideoEncoder *rec) {
    if (!rec->pipe) return;
    fflush(rec->pipe);
    #ifdef _WIN32
        _pclose(rec->pipe);
    #else
        pclose(rec->pipe);
    #endif
    rec->pipe = NULL;
}

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
        g_timer = 0.0f;
        RenderConfig()->flags = PATHTRACE_PIPELINE_FLAGS;
        ResetAccumulation();
    } else if (g_inprogress && UIButton("Cancel", width - 20)) {
        g_inprogress = FALSE;
        g_progress = 0.0f;
        loginfo("Rendering cancelled");
    }
}

static void DrawVideoRenderSettings(float width, float height) {
    float rcolw = width - LEFT_COLUMN_WIDTH - 20;
    if (AvailableFFMPEG()) {
        if (g_inprogress) {
            size_t frames = 1.0f + g_duration * g_fps;
            float curr = GetRenderStats().accumulation;
            float frame_progress = 100.0f * curr / (float)g_samples;
            float global_progress = 100.0f * (float)g_encoder.frames / (float)frames;
            g_progress = global_progress + (1.0f / (float)frames) * frame_progress;
        }
        UIColumnHeader("Save to...", LEFT_COLUMN_WIDTH);
        UIChooseFile(g_videopath, 2048, rcolw, "mp4", FALSE);
        UIColumnHeader("Samples", LEFT_COLUMN_WIDTH);
        UIDragSize(&g_samples, 0, 9999999999, 1, rcolw);
        UIColumnHeader("Convergence", LEFT_COLUMN_WIDTH);
        UIDragFloat(&g_convergence, 0, 100.0f, 0.01f, rcolw);
        UIColumnHeader("Viewport Only", LEFT_COLUMN_WIDTH);
        UIMoveCursor(-2, 1);
        UICheckbox(&g_vonly);
        UIMoveCursor(0, -1);
        UIColumnHeader("FPS", LEFT_COLUMN_WIDTH);
        UIDragFloat(&g_fps, 0.1f, 120.0f, 1.0f, rcolw);
        UIColumnHeader("Duration", LEFT_COLUMN_WIDTH);
        UIDragFloat(&g_duration, 0.1f, FLT_MAX, 1.0f, rcolw);
        UIMoveCursor(0, 10);
        EnableUI();
        if (!g_inprogress && UIButton("Render", width - 20)) {
            g_inprogress = TRUE;
            g_progress = 0.0f;
            g_timer = 0.0f;
            RenderConfig()->flags = PATHTRACE_PIPELINE_FLAGS;
            ResetAccumulation();
            OverrideTimelineDT(0.0f);
            if (NumAnimations() > 0) {
                Timeline()->frame = Timeline()->start;
                Timeline()->playing = TRUE;
            }
            VideoStart(&g_encoder, g_videopath, RenderResolution().x, RenderResolution().y, g_fps);
        } else if (g_inprogress && UIButton("Cancel", width - 20)) {
            g_inprogress = FALSE;
            g_progress = 0.0f;
            OverrideTimelineDT(-1.0f);
            Timeline()->playing = FALSE;
            VideoRecorderStop(&g_encoder);
            loginfo("Rendering cancelled");
        }
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
        char buf[64] = { 0 };
        sprintf(buf, "(%.0fs)", g_timer);
        UIProgressBar(width - 20, g_progress, buf);
    } else {
        Vector2 backup = UIGetCursor();
        UISetCursor(10, height - 60);
        char buf[64] = { 0 };
        sprintf(buf, "(%.0fs)", g_timer);
        UIProgressBar(width - 20, g_progress, buf);
        UISetCursor(backup.x, backup.y);
    }
    EnableUI();
}

static void UpdateRecordingPanel(float width, float height) {
    if (g_inprogress) g_timer += GetFrameTime();
    if (g_render_mode == 0 && g_inprogress && g_progress >= 100.0f) {
        if (g_vonly) {
            SaveViewportRender("render.png");
        } else {
            SaveRender("render.png");
        }
        loginfo("Finished rendering image in %.3fs! Saved to %s", g_timer, g_imagepath);
        g_inprogress = FALSE;
    } else if (g_render_mode == 1 && g_inprogress) {
        OverrideTimelineDT(0.0f);
        if (g_progress >= 100.0f) {
            VideoRecorderStop(&g_encoder);
            OverrideTimelineDT(-1.0f);
            Timeline()->playing = FALSE;
            g_inprogress = FALSE;
            loginfo("Finished rendering videoin %.3fs! Saved to %s", g_timer, g_videopath);
        }
        float curr = GetRenderStats().accumulation;
        float frame_progress = 100.0f * curr / (float)g_samples;
        if (RenderConverged(g_convergence / 100.0f)) frame_progress = 100.0f;
        if (frame_progress >= 100.0f) {
            OverrideTimelineDT(1.0f / g_fps);
            Image image = RenderToImage();
            VideoRecordFrame(&g_encoder, image);
            UnloadImage(image);
            ResetAccumulation();
        }
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
