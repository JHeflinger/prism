#include "overview.h"

void DrawOverviewPanel(float width, float height) {
    UIButton("This is my button", 0);
}

void UpdateOverviewPanel(float width, float height) {

}

void ConfigureOverviewPanel(Panel* panel) {
    SetupPanel(panel, "Overview");
    panel->draw = DrawOverviewPanel;
    //panel->update = UpdateOverviewPanel;
}
