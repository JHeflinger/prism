#include "mesh.h"
#include "renderer/renderer.h"
#include "ui/shared.h"

void DrawMeshPanel(float width, float height) {
    UIMoveCursor((width - 20 - UITextWidth("Edit Mesh")) / 2.0f, 0);
    UIDrawText("Edit Mesh");
    UIMoveCursor(0, 15);
    UIDrawText("Geometry Operations");
    UIDivider(width - 20);
    if (UIButton("Subdivide", width - 20)) {
        Subdivide();
    }
    static uint32_t faces_to_reduce_by = 0;
    if (faces_to_reduce_by > NumTriangles()) faces_to_reduce_by = NumTriangles() - 4;
    if (UIButton("Simplify", (width - 20.0f)/2.0f)) {
        Simplify(faces_to_reduce_by);
    }
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    uint32_t fmax = NumTriangles() >= 4 ? NumTriangles() - 4 : 0;
    UIDragUInt(&faces_to_reduce_by, 0, fmax, 1, (width - 20.0f)/2.0f);
    static float displacement = 0.0f;
    if (UIButton("Displace", (width - 20)/2.0f)) {
        Displace(displacement);
    }
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    UIDragFloat(&displacement, 0, FLT_MAX, 0.001f, (width - 20.0f)/2.0f);
    static float smoothening = 0.3f;
    if (UIButton("Smoothen", (width - 20)/2.0f)) {
        Smoothen(smoothening);
    }
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    UIDragFloat(&smoothening, 0, FLT_MAX, 0.001f, (width - 20.0f)/2.0f);
    static float nudgening = 0.5f;
    if (UIButton("Remesh", (width - 20)/2.0f)) {
        Remesh(nudgening);
    }
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    UIDragFloat(&nudgening, 0, 1.0f, 0.001f, (width - 20.0f)/2.0f);
    UIMoveCursor(0, 15);
    UIDrawText("Mesh Deformation");
    UIDivider(width - 20);
    if (UIButton("Set Pose", width - 20)) {
        SavePose();
    }
    UIDrawText("Iterations");
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    UIDragUInt(&(RenderConfig()->arap.iterations), 0, 100, 1, (width - 20.0f)/2.0f);
    UIDrawText("Style");
    UIMoveCursor((width - 20.0f)/2.0f, -20);
    UIDropdownMenu((width - 20.0f)/2.0f, 2, ARAPModelLabels(), DropdownSelectARAPModel, NULL);
    if (RenderConfig()->arap.style == 1) {
        UIMoveCursor(0, 15);
        UIDrawText("Cubify Configuration");
        UIDivider(width - 20);
        if (UIButton("Cubify", width - 20)) {
            RigidDeform();
        }
        UIDrawText("Iterations");
        UIMoveCursor((width - 20.0f)/2.0f, -20);
        uint32_t addm = RenderConfig()->arap.addm;
        UIDragUInt(&(addm), 1, 100, 1, (width - 20.0f)/2.0f);
        RenderConfig()->arap.addm = addm;
        UIDrawText("Lambda");
        UIMoveCursor((width - 20.0f)/2.0f, -20);
        UIDragFloat(&(RenderConfig()->arap.cube_lambda), 0.01, 1.0, 0.01f, (width - 20.0f)/2.0f);
        UIDrawText("Rho");
        UIMoveCursor((width - 20.0f)/2.0f, -20);
        UIDragFloat(&(RenderConfig()->arap.cube_rho), 0.0001, 0.1, 0.0005f, (width - 20.0f)/2.0f);
    }
}

Panel GenerateMeshPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Mesh");
	p.draw = DrawMeshPanel;
	return p;
}
