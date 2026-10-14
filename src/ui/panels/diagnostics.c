#include "diagnostics.h"
#include "renderer/renderer.h"
#include "renderer/trainer.h"
#include "ui/shared.h"
#include <core/config.h>

// WARN: this is mostly a scrap panel for temp controls and settings

static BOOL g_vsync_enabled = TRUE;
static size_t g_res_width = 0;
static size_t g_res_height = 0;
static Vector2 g_res_last = { 0 };

static const char* mem_size_descriptor(size_t count) {
    if (count > 1000000000) {
        return "GB";
    } else if (count > 1000000) {
        return "MB";
    } else if (count > 1000) {
        return "KB";
    } else {
        return "bytes";
    }
}

static float mem_size_compact(size_t count) {
    float fcount = count;
    if (count > 1000000000) {
        return fcount / 1000000000.0f;
    } else if (count > 1000000) {
        return fcount / 1000000.0f;
    } else if (count > 1000) {
        return fcount / 1000.0f;
    } else {
        return fcount;
    }
}

static void DrawResolutionControls(float width) {
    Vector2 res = RenderResolution();
    if (res.x != g_res_last.x || res.y != g_res_last.y) {
        g_res_width = (size_t)res.x;
        g_res_height = (size_t)res.y;
        g_res_last = res;
    }
    float component_width = (width - 20 - (2 * 15) - 10) / 2.0f;
    UIDrawText("w");
    UIMoveCursor(15, -20);
    UIDragSize(&g_res_width, 1, 16384, 1, component_width);
    UIMoveCursor(component_width + 25, -20);
    UIDrawText("h");
    UIMoveCursor(component_width + 40, -20);
    UIDragSize(&g_res_height, 1, 16384, 1, component_width);
    UIMoveCursor(0, 5);
    BOOL changed = g_res_width != (size_t)res.x || g_res_height != (size_t)res.y;
    BOOL capturing = TrainingScenePhaseNow() == TRAINSCENE_CAPTURING;
    float button_width = (width - 30.0f) / 2.0f;
    if (!changed || capturing) DisableUI();
    if (UIButton("Apply Resolution", button_width)) {
        if (!ResizeRenderer(g_res_width, g_res_height)) {
            g_res_width = (size_t)res.x;
            g_res_height = (size_t)res.y;
        }
    }
    UIMoveCursor(button_width + 10, -20);
    if (UIButton("Revert", button_width)) {
        g_res_width = (size_t)res.x;
        g_res_height = (size_t)res.y;
    }
    EnableUI();
    if (capturing) UIDrawText("(locked while capturing training views)");
}

static void DrawDevPanel(float width, float height) {
    UIDrawText("Application FPS: %d", (int)(1.0f / GetFrameTime()));
    UIDrawText("Frame time: %.6f ms", (1000.0f * GetFrameTime()));
    BOOL vsync = g_vsync_enabled;
	UICheckboxLabeled("VSYNC:", &vsync);
    if (vsync != g_vsync_enabled) {
        g_vsync_enabled = vsync;
        if (g_vsync_enabled)
            SetWindowState(FLAG_VSYNC_HINT);
        else
            ClearWindowState(FLAG_VSYNC_HINT);
    }
    UIDrawText("CPU Memory Usage: %.3f %s (%llu bytes)", mem_size_compact(EZ_ALLOCATED()), mem_size_descriptor(EZ_ALLOCATED()), (unsigned long long)EZ_ALLOCATED());
    PollGPUCache(FALSE);
    size_t numheaps = GPUHeapCount();
	if (numheaps == 0) UIDrawText("GPU Memory Usage: UNAVAILABLE");
	else UIDrawText("GPU Memory Usage:");
    for (size_t i = 0; i < numheaps; i++) {
        size_t allocated = GPUHeapUsage(i);
        size_t budget = GPUHeapBudget(i);
        UIDrawText("    %s: %.3f %s / %.3f %s (%.3f%c)",
            GPUHeapType(i),
            mem_size_compact(allocated),
            mem_size_descriptor(allocated),
            mem_size_compact(budget),
            mem_size_descriptor(budget),
            100.0f*((float)allocated)/((float)budget),
            '%');
    }

    UIMoveCursor(0, 20.0f);
    UIDrawText("Renderer FPS: %d", (int)(1.0f / ((float)RenderTime() * RenderConfig()->multiplier / 1000.0f)));
    UIDrawText("Render time: %.6f ms", (float)RenderTime() * RenderConfig()->multiplier);
    UIDrawText("Triangles: %d", (int)NumTriangles());
    UIDrawText("Vertices: %d", (int)NumVertices());
    UIDrawText("Normals: %d", (int)NumNormals());
    UIDrawText("Emissives: %d", (int)NumEmissives());
    UIDrawText("Splats: %d", (int)NumSplats());
    UIDrawText("Animations: %d", (int)NumAnimations());
    UIDrawText("Render Resolution: %dx%d", (int)RenderResolution().x, (int)RenderResolution().y);
    DrawResolutionControls(width);
}

Panel GenerateDiagnosticsPanel() {
    g_vsync_enabled = CONFGET(Bool, "default_vsync", TRUE);
	Panel p = { 0 };
	SetupPanel(&p, "Diagnostics");
	p.draw = DrawDevPanel;
    p.scrollable = TRUE;
	return p;
}
