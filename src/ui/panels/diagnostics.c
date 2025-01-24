#include "diagnostics.h"
#include "renderer/renderer.h"
#include <easymemory.h>

BOOL g_vsync_enabled = TRUE;

void DrawDevPanel(float width, float height) {
    UIDrawText("FPS: %d", (int)(1.0f / GetFrameTime()));
    UIDrawText("Frame time: %.6f ms", (1000.0f * GetFrameTime()));
    UIDrawText("Render time: %.6f ms", (float)RenderTime());
    UIDrawText("VSYNC: %s", g_vsync_enabled ? "ENABLED" : "DISABLED");
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
}

void ConfigureDiagnosticsPanel(Panel* panel) {
    SetupPanel(panel, "Diagnostics");
    panel->draw = DrawDevPanel;
}