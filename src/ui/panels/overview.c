#include "overview.h"
#include "core/log.h"

void DrawOverviewPanel(float width, float height) {
    if (UIButton("This is my button", 20)) {
        UIPopup(GenerateAddObjectPopup());
    }
}

void UpdateOverviewPanel(float width, float height) {

}

void ConfigureOverviewPanel(Panel* panel) {
    SetupPanel(panel, "Overview");
    panel->draw = DrawOverviewPanel;
    //panel->update = UpdateOverviewPanel;
}
