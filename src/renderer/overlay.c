#include "overlay.h"
#include "core/binds.h"
#include <easylogger.h>

Renderer* g_overlay_renderer_ref = NULL;
Rectangle g_viewport_dims = { 0 };
OverlaySSBO g_exposed_overlay_ssbo = (OverlaySSBO){ (TriangleID)-1 };
TriangleID g_single_selected_triangle = -1;
OverlayMode g_overlay_mode = NO_SELECT_MODE;

void SelectNoneMode() {
    g_overlay_mode = NO_SELECT_MODE;
}

void SelectTriangleMode() {
    g_overlay_mode = TRIANGLE_SELECT_MODE;
}

void SelectVertexMode() {
    g_overlay_mode = VERTEX_SELECT_MODE;
}

void SetOverlayContext(Renderer* renderer) {
    g_overlay_renderer_ref = renderer;
    AddBind("no select mode", SelectNoneMode,
        (BindCommand){ IK_SELECT, BIND_KEY_DOWN },
        (BindCommand){ IK_SELECT_NONE, BIND_KEY_PRESSED });
    AddBind("triangle select mode", SelectTriangleMode,
        (BindCommand){ IK_SELECT, BIND_KEY_DOWN },
        (BindCommand){ IK_SELECT_FACE, BIND_KEY_PRESSED });
    AddBind("vertex select mode", SelectVertexMode,
        (BindCommand){ IK_SELECT, BIND_KEY_DOWN },
        (BindCommand){ IK_SELECT_VERTEX, BIND_KEY_PRESSED });
}

void SetViewportRec(Rectangle rec) {
    g_viewport_dims = rec;
}

Rectangle GetViewportRec() {
    return g_viewport_dims;
}

TriangleID HoveredTriangle() {
    return g_exposed_overlay_ssbo.hovered_tid;
}

OverlaySSBO* ExposedOverlaySSBO() {
    return &g_exposed_overlay_ssbo;
}

void SetSelectedTriangle(TriangleID tid) {
    g_single_selected_triangle = tid;
}

TriangleID GetSelectedTriangle() {
    return g_single_selected_triangle;
}

OverlayMode GetOverlayMode() {
    return g_overlay_mode;
}
