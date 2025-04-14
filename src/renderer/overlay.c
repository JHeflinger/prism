#include "overlay.h"
#include <easylogger.h>

Renderer* g_overlay_renderer_ref = NULL;
Rectangle g_viewport_dims = { 0 };
OverlaySSBO g_exposed_overlay_ssbo = { 0 };
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

TriangleID HoveredTriangle() {
    if (g_exposed_overlay_ssbo.hovered_tid == (uint32_t)-1) return (TriangleID)-1;
    return (TriangleID)g_exposed_overlay_ssbo.hovered_tid;
}

size_t HoveredTriangleIndex(TriangleID tid) {
    for (size_t i = 0; i < g_overlay_renderer_ref->geometry.tids.size; i++) {
        if (g_overlay_renderer_ref->geometry.tids.data[i] == tid) {
            return i;
        }
    }
    EZ_FATAL("This triangle does not exist!");
    return 0;
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
