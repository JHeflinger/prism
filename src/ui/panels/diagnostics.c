#include "diagnostics.h"
#include "renderer/renderer.h"
#include "data/assets.h"
#include <easymemory.h>

BOOL g_vsync_enabled = TRUE;

float poop = 0.0f;

void DrawDevPanel(float width, float height) {
    UIDrawText("FPS: %d", (int)(1.0f / GetFrameTime()));
    UIDrawText("Frame time: %.6f ms", (1000.0f * GetFrameTime()));
    UIDrawText("Render time: %.6f ms", (float)RenderTime());
    UIDrawText("VSYNC: %s", g_vsync_enabled ? "ENABLED" : "DISABLED");
    UIDrawText("Triangles: %d", (int)NumTriangles());
    UIDrawText("Render Resolution: %dx%d", (int)RenderResolution().x, (int)RenderResolution().y);
    if (EZALLOCATED() > 1000000000) {
        UIDrawText("Memory Usage: %.3f GB (%d bytes)", ((float)EZALLOCATED()) / 1000000000, (int)EZALLOCATED());
    } else if (EZALLOCATED() > 1000000) {
        UIDrawText("Memory Usage: %.3f MB (%d bytes)", ((float)EZALLOCATED()) / 1000000, (int)EZALLOCATED());
    } else if (EZALLOCATED() > 1000) {
        UIDrawText("Memory Usage: %.3f KB (%d bytes)", ((float)EZALLOCATED()) / 1000, (int)EZALLOCATED());
    } else {
        UIDrawText("Memory Usage: %d bytes", (int)EZALLOCATED());
    }

    if (IsKeyPressed(KEY_V)) {
        g_vsync_enabled = !g_vsync_enabled;
        if (g_vsync_enabled)
            SetWindowState(FLAG_VSYNC_HINT);
        else
            ClearWindowState(FLAG_VSYNC_HINT);
    }

    UIMoveCursor(0, 5.0f);
    UIDrawText("Frameless:");
    float xdif =  MeasureTextEx(FontAsset(), "Frameless:", 20, 0).x;
    UIMoveCursor(xdif + 10, -20.0f);
    UIDragFloat(&(RenderConfig()->frameless), 0.0f, 1.0f, 0.001f, width - 30 - xdif);

	UIMoveCursor(0, 5.0f);
    UIDrawText("Shadows:");
    xdif =  MeasureTextEx(FontAsset(), "Shadows:", 20, 0).x;
    UIMoveCursor(xdif + 10, -20.0f);
	UICheckbox(&(RenderConfig()->shadows));

	UIMoveCursor(0, 5.0f);
    UIDrawText("Reflections:");
    xdif =  MeasureTextEx(FontAsset(), "Reflections:", 20, 0).x;
    UIMoveCursor(xdif + 10, -20.0f);
	UICheckbox(&(RenderConfig()->reflections));

	UIMoveCursor(0, 5.0f);
    UIDrawText("Lighting:");
    xdif =  MeasureTextEx(FontAsset(), "Lighting:", 20, 0).x;
    UIMoveCursor(xdif + 10, -20.0f);
	UICheckbox(&(RenderConfig()->lighting));
}

void ConfigureDiagnosticsPanel(Panel* panel) {
    SetupPanel(panel, "Diagnostics");
    panel->draw = DrawDevPanel;
}
