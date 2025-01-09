#include "devpanel.h"

void DrawDevPanel() {
    DrawRectangle(10, 50, 200, 200, RED);
    DrawText("whattup", 20, 60, 18, WHITE);
}

void ConfigureDevPanel(Panel* panel) {
    SetupPanel(panel, "Developer Settings");
    panel->draw = DrawDevPanel;
}