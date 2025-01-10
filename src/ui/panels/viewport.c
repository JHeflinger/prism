#include "viewport.h"

void DrawViewportPanel() {
    DrawRectangle(100, 100, 100, 100, RED);
}

void ConfigureViewportPanel(Panel* panel) {
    SetupPanel(panel, "Viewport");
    panel->draw = DrawViewportPanel;
}