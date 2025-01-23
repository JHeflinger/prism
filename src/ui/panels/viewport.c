#include "viewport.h"
#include "renderer/renderer.h"

void DrawViewportPanel() {
    DrawRectangle(100, 100, 100, 100, RED);
    Render();
    Draw(100, 100);
}

void ConfigureViewportPanel(Panel* panel) {
    SetupPanel(panel, "Viewport");
    panel->draw = DrawViewportPanel;
}