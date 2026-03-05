#include "overview.h"
#include "renderer/renderer.h"
#include "data/strings.h"
#include "ui/panels/edit.h"

void DrawOverviewPanel(float width, float height) {
    UIDrawText("Add To Scene...");
    UIMoveCursor(width - 45, -20);
    if (UIButton("+", 0)) {
        UIPopup(GenerateAddObjectPopup());
    }
    UIDivider(width - 20);
    UIDropList("Materials", width - 20, NumMaterials(), MaterialNameReference(0), SetEditMaterial);
    UIMoveCursor(0, 5);
    UIDropList("Lights", width - 20, NumLights(), LightNameReference(0), SetEditLight);
}

Panel GenerateOverviewPanel() {
	Panel p = { 0 };
    SetupPanel(&p, "Overview");
    p.draw = DrawOverviewPanel;
	return p;
}
