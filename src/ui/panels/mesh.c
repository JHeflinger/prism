#include "mesh.h"
#include "renderer/renderer.h"

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
    UIDragUInt(&faces_to_reduce_by, 1, NumTriangles() - 4, 1, (width - 20.0f)/2.0f);
}

Panel GenerateMeshPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Mesh");
	p.draw = DrawMeshPanel;
	return p;
}
