#include "diagnostics.h"
#include "renderer/renderer.h"
#include "data/assets.h"
#include <easymemory.h>

BOOL g_vsync_enabled = TRUE;
BOOL g_time_paused = TRUE;

float poop = 0.0f;

const char* mem_size_descriptor(size_t count) {
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

float mem_size_compact(size_t count) {
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

void DrawDevPanel(float width, float height) {
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
    UIDrawText("GPU Memory Usage:");
    size_t numheaps = GPUHeapCount();
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
    UIDrawText("Renderer FPS: %d", (int)(1.0f / ((float)RenderTime() / 1000.0f)));
    UIDrawText("Render time: %.6f ms", (float)RenderTime());
    UIDrawText("Triangles: %d", (int)NumTriangles());
    UIDrawText("SDF Objects: %d", (int)NumSDFs());
    UIDrawText("Render Resolution: %dx%d", (int)RenderResolution().x, (int)RenderResolution().y);
	UICheckboxLabeled("Time Paused:", &g_time_paused);
    if (!g_time_paused) RenderConfig()->time += GetFrameTime();
    UIDragFloatLabeled("Time:", &(RenderConfig()->time), 0.0f, 999999999.0f, 1.00f, width - 20);
	UICheckboxLabeled("Automatic Frameless:", &(RenderConfig()->autoframeless));
    UIDragFloatLabeled("Frameless:", &(RenderConfig()->frameless), 0.0f, 1.0f, 0.001f, width - 20);
	UICheckboxLabeled("Anti-Aliasing:", &(RenderConfig()->antialiasing));
	UICheckboxLabeled("Grid:", &(RenderConfig()->grid));

    UIMoveCursor(0, 20.0f);
	UICheckboxLabeled("Raytrace:", &(RenderConfig()->raytrace));
	UICheckboxLabeled("Shadows:", &(RenderConfig()->shadows));
	UICheckboxLabeled("Reflections:", &(RenderConfig()->reflections));
	UICheckboxLabeled("Lighting:", &(RenderConfig()->lighting));

    UIMoveCursor(0, 20.0f);
	UICheckboxLabeled("SDF:", &(RenderConfig()->sdf));
    UIDragUIntLabeled("Max Marches:", &(RenderConfig()->maxmarches), 0, 10000000, 1, width - 20);
    UIDragFloatLabeled("Smooth:", &(RenderConfig()->sdfsmooth), 0.0f, 10000.0f, 0.05f, width - 20);
}

void ConfigureDiagnosticsPanel(Panel* panel) {
    SetupPanel(panel, "Diagnostics");
    panel->draw = DrawDevPanel;
}
