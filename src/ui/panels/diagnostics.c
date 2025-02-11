#include "diagnostics.h"
#include "renderer/renderer.h"
#include "data/assets.h"
#include <easymemory.h>

BOOL g_vsync_enabled = TRUE;

float poop = 0.0f;

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
    if (EZALLOCATED() > 1000000000) {
        UIDrawText("Memory Usage: %.3f GB (%d bytes)", ((float)EZALLOCATED()) / 1000000000, (int)EZALLOCATED());
    } else if (EZALLOCATED() > 1000000) {
        UIDrawText("Memory Usage: %.3f MB (%d bytes)", ((float)EZALLOCATED()) / 1000000, (int)EZALLOCATED());
    } else if (EZALLOCATED() > 1000) {
        UIDrawText("Memory Usage: %.3f KB (%d bytes)", ((float)EZALLOCATED()) / 1000, (int)EZALLOCATED());
    } else {
        UIDrawText("Memory Usage: %d bytes", (int)EZALLOCATED());
    }

    UIMoveCursor(0, 20.0f);
    UIDrawText("Renderer FPS: %d", (int)(1.0f / ((float)RenderTime() / 1000.0f)));
    UIDrawText("Render time: %.6f ms", (float)RenderTime());
    UIDrawText("Triangles: %d", (int)NumTriangles());
    UIDrawText("SDF Objects: %d", (int)NumSDFs());
    UIDrawText("Render Resolution: %dx%d", (int)RenderResolution().x, (int)RenderResolution().y);

    UIMoveCursor(0, 20.0f);
	UICheckboxLabeled("Raytrace:", &(RenderConfig()->raytrace));
    UIDragFloatLabeled("Frameless:", &(RenderConfig()->frameless), 0.0f, 1.0f, 0.001f, width - 20);
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
