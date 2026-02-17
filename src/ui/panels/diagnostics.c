#include "diagnostics.h"
#include "renderer/renderer.h"
#include "data/assets.h"
#include "data/input.h"
#include "renderer/loader.h"
#include <easymemory.h>

BOOL g_vsync_enabled = TRUE;

float poop = 0.0f;

const char* mem_size_descriptor(size_t count) {
    if (count > 1000000000) {
        return "GB";
    } else if (count > 1000000) {
        return "MB";
    } else if (count >1000) {
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
    UICheckboxLabeled("Grid:", &(RenderConfig()->grid));
    UICheckboxLabeled("Smooth Normals:", &(RenderConfig()->normals));
    BOOL preview = !(RenderConfig()->flags & PATHTRACE_SHADER_FLAG);
    UICheckboxLabeled("Preview:", &preview);
    SetPipelineFlags(preview ? PREVIEW_PIPELINE_FLAGS : PATHTRACE_PIPELINE_FLAGS);
    UICheckboxLabeled("Direct Light Sampling:", &(RenderConfig()->direct));
    UICheckboxLabeled("Direct Light Only:", &(RenderConfig()->directonly));
    
    UIMoveCursor(0, 20.0f);
    UIDragFloatLabeled("Whitepoint:", &(RenderConfig()->whitepoint), 0.01f, 999999999.0f, 0.1f, width - 20);
    UIDragFloatLabeled("Gamma:", &(RenderConfig()->gamma), 0.01f, 999999999.0f, 0.01f, width - 20);

    UIMoveCursor(0, 20.0f);
    UIDrawText("Renderer FPS: %d", (int)(1.0f / ((float)RenderTime() / 1000.0f)));
    UIDrawText("Render time: %.6f ms", (float)RenderTime());
    UIDrawText("Triangles: %d", (int)NumTriangles());
    UIDrawText("Emissives: %d", (int)NumEmissives());
    UIDrawText("Render Resolution: %dx%d", (int)RenderResolution().x, (int)RenderResolution().y);

    SimpleCamera c = GetCamera();
    SimpleCamera oldc = GetCamera();
    BOOL used = FALSE;
    UIMoveCursor(0, 20.0f);
    UIDragFloatLabeled("Aperature:", &(c.aperature), 0.0f, 999999999.0f, 0.01f, width - 20);
    used |= UIWasJustUsed();
    UIDragFloatLabeled("Focus:", &(c.focus), 0.0f, 999999999.0f, 0.01f, width - 20);
    used |= UIWasJustUsed();
	if (memcmp(&c, &oldc, sizeof(SimpleCamera))) MoveCamera(c);
    RenderConfig()->showdof = used;
}

Panel GenerateDiagnosticsPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Diagnostics");
	p.draw = DrawDevPanel;
	return p;
}
