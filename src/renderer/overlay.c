#include "overlay.h"
#include "renderer/renderer.h"
#include "ui/panels/edit.h"
#include <core/binds.h>

static Renderer* g_overlay_renderer_ref = NULL;
static Rectangle g_viewport_dims = { 0 };
static OverlaySSBO g_exposed_overlay_ssbo = (OverlaySSBO){ (TriangleID)-1, { 0, 0 }, { 0, 0 } };
static TriangleID g_single_selected_triangle = -1;
static VertexID g_single_selected_vertex = -1;
static LightID g_single_selected_light = -1;
static OverlayMode g_overlay_mode = NO_SELECT_MODE;

static void SelectNoneMode() {
    g_overlay_mode = NO_SELECT_MODE;
}

static void SelectTriangleMode() {
    g_overlay_mode = TRIANGLE_SELECT_MODE;
}

static void SelectVertexMode() {
    g_overlay_mode = VERTEX_SELECT_MODE;
}

static void SelectLightMode() {
    g_overlay_mode = LIGHT_SELECT_MODE;
}

static void SelectObjectMode() {
    g_overlay_mode = OBJECT_SELECT_MODE;
}

static void SelectCameraMode() {
    g_overlay_mode = CAMERA_SELECT_MODE;
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
    AddBind("light select mode", SelectLightMode,
        (BindCommand){ IK_SELECT, BIND_KEY_DOWN },
        (BindCommand){ IK_SELECT_LIGHT, BIND_KEY_PRESSED });
    AddBind("object select mode", SelectObjectMode,
        (BindCommand){ IK_SELECT, BIND_KEY_DOWN },
        (BindCommand){ IK_SELECT_OBJECT, BIND_KEY_PRESSED });
    AddBind("camera select mode", SelectCameraMode,
        (BindCommand){ IK_SELECT, BIND_KEY_DOWN },
        (BindCommand){ IK_SELECT_CAMERA, BIND_KEY_PRESSED });
}

void SetViewportRec(Rectangle rec) {
    g_viewport_dims = rec;
}

Rectangle GetViewportRec() {
    return g_viewport_dims;
}

TriangleID HoveredTriangle() {
    if (g_overlay_mode == TRIANGLE_SELECT_MODE) return g_exposed_overlay_ssbo.hovered_tid;
    return (TriangleID)-1;
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

VertexID HoveredVertex() {
    if (g_overlay_mode == VERTEX_SELECT_MODE) return g_exposed_overlay_ssbo.hovered_tid;
    return (VertexID)-1;
}

void SetSelectedVertex(VertexID vid) {
    g_single_selected_vertex = vid;
}

VertexID GetSelectedVertex() {
    return g_single_selected_vertex;
}

LightID HoveredLight() {
    if (g_overlay_mode != LIGHT_SELECT_MODE) return (LightID)-1;
    LightID lid = g_exposed_overlay_ssbo.hovered_tid;
    if (g_overlay_renderer_ref && lid >= g_overlay_renderer_ref->geometry.lights.size) return (LightID)-1;
    return lid;
}

void SetSelectedLight(LightID lid) {
    g_single_selected_light = lid;
}

LightID GetSelectedLight() {
    return g_single_selected_light;
}

size_t HoveredObject() {
    if (g_overlay_mode != OBJECT_SELECT_MODE || !g_overlay_renderer_ref) return (size_t)-1;
    uint32_t id = g_exposed_overlay_ssbo.hovered_tid;
    if (id == (uint32_t)-1) return (size_t)-1;
    size_t triangles = g_overlay_renderer_ref->geometry.triangles.size;
    size_t splats = g_overlay_renderer_ref->geometry.splats.size;
    size_t object = (size_t)-1;
    if (id < triangles) object = FindMeshFromTriangle((TriangleID)id);
    else if (id - triangles < splats) object = FindMeshFromSplat((uint32_t)(id - triangles));
    if (object != (size_t)-1 && (object >= NumMeshes() || MeshReference(object)->disabled)) return (size_t)-1;
    return object;
}

size_t GetSelectedObject() {
    EditType type;
    size_t index;
    if (GetEditTarget(&type, &index) && type == EDIT_MESH && index < NumMeshes()) return index;
    return (size_t)-1;
}

size_t HoveredCamera() {
    if (g_overlay_mode != CAMERA_SELECT_MODE) return (size_t)-1;
    uint32_t cid = g_exposed_overlay_ssbo.hovered_tid;
    if (cid == (uint32_t)-1 || cid >= NumCameras() || cid >= MAX_CAMERA_GLYPHS || cid == PrimaryCameraID()) return (size_t)-1;
    return (size_t)cid;
}

size_t GetSelectedCamera() {
    EditType type;
    size_t index;
    if (GetEditTarget(&type, &index) && type == EDIT_CAMERA && index < NumCameras()) return index;
    return (size_t)-1;
}
