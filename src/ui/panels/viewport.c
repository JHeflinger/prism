#include "viewport.h"
#include "renderer/renderer.h"

void DrawViewportPanel(float width, float height) {
    Render();
    Draw(0, 0, width, height);
}

void ConfigureViewportPanel(Panel* panel) {
    SetupPanel(panel, "Viewport");
    panel->draw = DrawViewportPanel;
}