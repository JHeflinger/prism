#include "overlay.h"
#include <easylogger.h>

Renderer* g_overlay_renderer_ref = NULL;
Rectangle g_viewport_dims = { 0 };
OverlaySSBO g_exposed_overlay_ssbo = (OverlaySSBO){ (uint32_t)-1 };
TriangleID g_single_selected_triangle = -1;

void SetOverlayContext(Renderer* renderer) {
    g_overlay_renderer_ref = renderer;
}

void SetViewportRec(Rectangle rec) {
    g_viewport_dims = rec;
}

Rectangle GetViewportRec() {
    return g_viewport_dims;
}

size_t HoveredTriangleIndex() {
    if (g_exposed_overlay_ssbo.hovered_tid == (uint32_t)-1) return (size_t)-1;
    return (TriangleID)g_exposed_overlay_ssbo.hovered_tid;
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
