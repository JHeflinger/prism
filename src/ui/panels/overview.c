#include "overview.h"
#include "renderer/renderer.h"
#include "ui/panels/edit.h"
#include "ui/shared.h"

static size_t SelectedIndex(EditType type) {
    EditType curr = 0;
    size_t ind = (size_t)-1;
    if (GetEditTarget(&curr, &ind)) {
        if (type == curr) return ind;
    }
    return (size_t)-1;
}

static void DrawOverviewPanel(float width, float height) {
    UIDrawText("Add To Scene...");
    UIMoveCursor(width - 45, -20);
    if (UIButton("+", 0)) {
        UIPopup(RenderConfig()->flags & FLUID_SHADER_FLAG ? GenerateAddSimObjectPopup() : GenerateAddObjectPopup());
    }
    UIDivider(width - 20);
    if (RenderConfig()->flags & FLUID_SHADER_FLAG) {
        UIDropList("Forces", width - 20, NumForces(), ForceNameReference(0), SetEditForce, SelectedIndex(EDIT_SINGLE_FORCE));
        UIMoveCursor(0, 5);
        UIDropList("Sources", width - 20, NumSources(), SourceNameReference(0), SetEditSource, SelectedIndex(EDIT_SINGLE_SOURCE));
    } else {
        UIDropList("Cameras", width - 20, NumCameras(), CameraNameReference(0), SetEditCamera, SelectedIndex(EDIT_CAMERA));
        UIMoveCursor(0, 5);
        UIDropList("Materials", width - 20, NumMaterials(), MaterialNameReference(0), SetEditMaterial, SelectedIndex(EDIT_MATERIAL));
        UIMoveCursor(0, 5);
        UIDropList("Lights", width - 20, NumLights(), LightNameReference(0), SetEditLight, SelectedIndex(EDIT_LIGHT));
        UIMoveCursor(0, 5);
        UIDropList("Objects", width - 20, NumMeshes(), MeshNameReference(0), SetEditMesh, SelectedIndex(EDIT_MESH));
    }
}

Panel GenerateOverviewPanel() {
	Panel p = { 0 };
    SetupPanel(&p, "Overview");
    p.draw = DrawOverviewPanel;
    p.scrollable = TRUE;
	return p;
}
