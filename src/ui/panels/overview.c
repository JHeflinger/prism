#include "overview.h"
#include "renderer/renderer.h"
#include "data/strings.h"
#include "ui/panels/edit.h"

void DrawOverviewPanel(float width, float height) {
    UIDrawText("Add To Scene...");
    UIMoveCursor(width - 45, -20);
    if (UIButton("+", 0)) {
        UIPopup(RenderConfig()->flags & FLUID_SHADER_FLAG ? GenerateAddSimObjectPopup() : GenerateAddObjectPopup());
    }
    UIDivider(width - 20);
    if (RenderConfig()->flags & FLUID_SHADER_FLAG) {
        UIDropList("Forces", width - 20, NumForces(), ForceNameReference(0), SetEditForce);
        UIMoveCursor(0, 5);
        UIDropList("Sources", width - 20, NumSources(), SourceNameReference(0), SetEditSource);
    } else {
        UIDropList("Materials", width - 20, NumMaterials(), MaterialNameReference(0), SetEditMaterial);
        UIMoveCursor(0, 5);
        UIDropList("Lights", width - 20, NumLights(), LightNameReference(0), SetEditLight);
        UIMoveCursor(0, 5);
        UIDropList("Objects", width - 20, NumMeshes(), MeshNameReference(0), SetEditMesh);
    }
}

Panel GenerateOverviewPanel() {
	Panel p = { 0 };
    SetupPanel(&p, "Overview");
    p.draw = DrawOverviewPanel;
	return p;
}
