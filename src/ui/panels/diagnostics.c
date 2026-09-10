#include "diagnostics.h"
#include "renderer/renderer.h"
#include "ui/shared.h"

// WARN: this is mostly a scrap panel for temp controls and settings

static BOOL g_vsync_enabled = TRUE;

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
    UIDrawText("Render Resolution: %dx%d", (int)RenderResolution().x, (int)RenderResolution().y);
}

Panel GenerateDiagnosticsPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Diagnostics");
	p.draw = DrawDevPanel;
    p.scrollable = TRUE;
	return p;
}
