#include "diagnostics.h"
#include <easymemory.h>

void DrawDevPanel() {
    UIDrawText("FPS: %d", (int)(1.0f / GetFrameTime()));
    UIDrawText("Frametime: %.6f ms", (1000.0f * GetFrameTime()));
    if (EZALLOCATED() > 1000000000) {
        UIDrawText("Memory Usage: %.3f GB (%d bytes)", ((float)EZALLOCATED()) / 1000000000, (int)EZALLOCATED());
    } else if (EZALLOCATED() > 1000000) {
        UIDrawText("Memory Usage: %.3f MB (%d bytes)", ((float)EZALLOCATED()) / 1000000, (int)EZALLOCATED());
    } else if (EZALLOCATED() > 1000) {
        UIDrawText("Memory Usage: %.3f KB (%d bytes)", ((float)EZALLOCATED()) / 1000, (int)EZALLOCATED());
    } else {
        UIDrawText("Memory Usage: %d bytes", (int)EZALLOCATED());
    }
}

void ConfigureDiagnosticsPanel(Panel* panel) {
    SetupPanel(panel, "Diagnostics");
    panel->draw = DrawDevPanel;
}