#include "viewport.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"
#include "renderer/rmath.h"
#include "ui/panels/edit.h"
#include "ui/gizmo.h"
#include <data/colors.h>
#include <data/fonts.h>
#include <core/binds.h>

#define TOOLBAR_HEIGHT 20.0f
#define TOOLBAR_GAP 2.0f
#define TOOLBAR_PAD 8.0f
#define TOOLBAR_MARGIN 10.0f
#define TOOLBAR_FONT 16.0f

typedef enum {
    BTN_MOVE = 0,
    BTN_ROTATE,
    BTN_SCALE,
    BTN_SPACE,
    BTN_PIVOT,
    BTN_SNAP,
    BTN_COUNT,
} ToolbarButton;

static RenderTexture2D g_viewport_target;
static BOOL g_show_hints = TRUE;
static BOOL g_rfocused = FALSE;
static BOOL g_lfocused = FALSE;
static BOOL g_zfocused = FALSE;
static vec2 g_mousepoint = { 0 };
static Vector2 g_viewport_position = { 0 };
static Vector2 g_viewport_dimensions = { 0 };
static Gizmo g_gizmo = { 0 };
static ViewProjection g_view = { 0 };
static TransformSpace g_space = TRANSFORM_SPACE_WORLD;
static BOOL g_pivot_center = TRUE;
static BOOL g_snap = FALSE;
static BOOL g_dragging = FALSE;
static TargetSnapshot g_snapshot;
static int g_toolbar_hot = -1;
static TargetCaps g_caps = { 0 };
static BOOL g_gizmos = TRUE;

static void ResetViewportCamera() {
    SimpleCamera camera = RendererCamera()->core;
    SETVEC3(camera.position, 0.0f, 2.133f, 2.11f);
    SETVEC3(camera.look, 0.0f, 0.0f, 0.0f);
    SETVEC3(camera.up, 0.0f, 1.0f, 0.0f);
    camera.fov = 90.0f;
    camera.orthosize = 4.0f;
    camera.aperature = 0.0f;
    camera.focus = 0.0f;
    RendererCamera()->core = camera;
}

static void ToggleHints() {
    g_show_hints = !g_show_hints;
}

static void RotateCameraControls() {
    if (g_rfocused) {
        NavWidgetCancelSnap();
        SimpleCamera camera = RendererCamera()->core;
        vec3 offset;
        glm_vec3_sub(camera.position, camera.look, offset);
        float radius = glm_vec3_norm(offset);
        if (radius < 1e-6f) radius = 1e-6f;
        float phi = acosf(offset[1] / radius) - (GetMouseDelta().y / 225.0);
        float theta = atan2f(offset[2], offset[0]) + (GetMouseDelta().x / 400.0f);
        if (phi < 0.001f) phi = 0.001f;
        if (phi > M_PI - 0.001f) phi = M_PI - 0.001f;
        camera.position[0] = camera.look[0] + (radius * sin(phi) * cos(theta));
        camera.position[1] = camera.look[1] + (radius * cos(phi));
        camera.position[2] = camera.look[2] + (radius * sin(phi) * sin(theta));
        RendererCamera()->core = camera;
    }
}

static void ZoomCameraControls() {
    if (g_zfocused) {
        vec2 mousedelta = { GetMouseDelta().x, GetMouseDelta().y };
        vec2 prev = { GetMousePosition().x, GetMousePosition().y };
        vec2 current = { GetMousePosition().x, GetMousePosition().y };
        glm_vec2_sub(prev, mousedelta, prev);
        vec2 o2c, o2p;
        glm_vec2_sub(g_mousepoint, prev, o2p);
        glm_vec2_sub(g_mousepoint, current, o2c);
        float to_previous = glm_vec2_norm(o2p);
        float to_current = glm_vec2_norm(o2c);
        float mousedelta_offset = glm_vec2_norm(mousedelta);
        SimpleCamera camera = RendererCamera()->core;
        vec3 offset;
        glm_vec3_sub(camera.position, camera.look, offset);
        float radius = glm_vec3_norm(offset);
        float rbefore = radius;
        radius -= (mousedelta_offset * radius / 512.0f) * (to_previous < to_current ? -1.0f : 1.0f);
        if (radius < 1e-6f) radius = 1e-6f;
        if (camera.ortho) {
            if (rbefore != radius) {
                camera.orthosize *= radius / rbefore;
                if (camera.orthosize < 1e-4f) camera.orthosize = 1e-4f;
                RendererCamera()->core = camera;
            }
            return;
        }
        glm_vec3_normalize(offset);
        glm_vec3_scale(offset, radius, offset);
        glm_vec3_add(camera.look, offset, camera.position);
        if (rbefore != radius) RendererCamera()->core = camera;
    }
}

static void PanCameraControls() {
    if (g_lfocused) {
        SimpleCamera camera = RendererCamera()->core;
        vec3 offset;
        glm_vec3_sub(camera.position, camera.look, offset);
        float radius = glm_vec3_norm(offset);
        if (radius < 1e-6f) radius = 1e-6f;
        float phi = acosf(offset[1] / radius);
        float theta = atan2f(offset[2], offset[0]);
        float distance_correction = radius / 600.0f;
        if (camera.ortho) distance_correction = g_view.ortho && g_view.orthoscale > 1e-6f ? 1.0f / g_view.orthoscale : camera.orthosize / 600.0f;
        vec2 mouse_delta = { GetMouseDelta().x * distance_correction, GetMouseDelta().y * distance_correction };
        vec3 forward;
        glm_vec3_sub(camera.look, camera.position, forward);
        glm_vec3_normalize(forward);
        vec3 right, up;
        glm_vec3_cross(camera.up, forward, right);
        glm_vec3_normalize(right);
        glm_vec3_cross(forward, right, up);
        glm_vec3_normalize(up);
        vec3 movement;
        glm_vec3_scale(right, mouse_delta[0], right);
        glm_vec3_scale(up, mouse_delta[1], up);
        glm_vec3_add(right, up, movement);
        glm_vec3_add(camera.look, movement, camera.look);
        camera.position[0] = camera.look[0] + (radius * sin(phi) * cos(theta));
        camera.position[1] = camera.look[1] + (radius * cos(phi));
        camera.position[2] = camera.look[2] + (radius * sin(phi) * sin(theta));
        RendererCamera()->core = camera;
    }
}

static void PanSelectedObject() {
    if (g_lfocused) {
        vec3 selected, offset, u, v, w, _w;
        if (GetSelectedVertex() != (VertexID)-1) {
            float* vref = VertexReference(GetSelectedVertex());
            glm_vec3_copy(vref, selected);
        } else return;
        SimpleCamera camera = RendererCamera()->core;
        CameraUVW(camera, u, v, w);
        glm_vec3_scale(w, -1.0f, _w);
        glm_vec3_sub(selected, camera.position, offset);
        float cz = glm_vec3_dot(offset, _w);
        float r = RenderResolution().x * 0.5f;
        float b = RenderResolution().y * 0.5f;
        float fov = glm_rad(camera.fov);
        float d = (cos(fov/2.0f) / sin(fov/2.0f)) * r;
        float mx = GetMousePosition().x - (g_viewport_position.x + (g_viewport_dimensions.x / 2.0f) - (RenderResolution().x / 2.0f));
        float my = GetMousePosition().y - (g_viewport_position.y + (g_viewport_dimensions.y / 2.0f) - (RenderResolution().y / 2.0f));
        float px = ((mx / RenderResolution().x) * 2.0f * r) - r;
        float py = b - ((my / RenderResolution().y) * 2.0f * b);
        float cx = (px / d) * cz;
        float cy = (py / d) * cz;
        if (camera.ortho) {
            float world_per_pixel = (camera.orthosize > 1e-4f ? camera.orthosize : 1e-4f) / RenderResolution().y;
            cx = px * world_per_pixel;
            cy = py * world_per_pixel;
        }
        glm_vec3_scale(u, cx, u);
        glm_vec3_scale(v, cy, v);
        glm_vec3_scale(w, cz, w);
        glm_vec3_add(u, v, selected);
        glm_vec3_sub(selected, w, selected);
        glm_vec3_add(camera.position, selected, VertexReference(GetSelectedVertex()));
        if (VertexLocked(GetSelectedVertex())) RigidDeform();
        UpdateVertices();
    }
}

static void TogglePreview() {
    BOOL preview = !(RenderConfig()->flags & PATHTRACE_SHADER_FLAG);
    RenderConfig()->flags = !preview ? PREVIEW_PIPELINE_FLAGS : PATHTRACE_PIPELINE_FLAGS;
}

static GizmoMode EffectiveMode(const TargetCaps* caps) {
    if (!caps->valid || caps->can[g_gizmo.mode]) return g_gizmo.mode;
    for (int m = 0; m < 3; m++) if (caps->can[m]) return (GizmoMode)m;
    return g_gizmo.mode;
}

static const char* ButtonLabel(int b) {
    switch (b) {
        case BTN_MOVE: return "Move";
        case BTN_ROTATE: return "Rotate";
        case BTN_SCALE: return "Scale";
        case BTN_SPACE: return g_space == TRANSFORM_SPACE_LOCAL ? "Local" : "World";
        case BTN_PIVOT: return g_pivot_center ? "Pivot: Center" : "Pivot: Origin";
        default: return "Snap";
    }
}

static void ToolbarLayout(float height, Rectangle rects[BTN_COUNT]) {
    Font font = FontAsset();
    float x = TOOLBAR_MARGIN;
    float y = height - TOOLBAR_HEIGHT - TOOLBAR_MARGIN;
    for (int b = 0; b < BTN_COUNT; b++) {
        float w = MeasureTextEx(font, ButtonLabel(b), TOOLBAR_FONT, 0).x + TOOLBAR_PAD * 2.0f;
        if (b == BTN_SPACE) x += TOOLBAR_GAP * 2.0f;
        rects[b] = (Rectangle){ x, y, w, TOOLBAR_HEIGHT };
        x += w + TOOLBAR_GAP;
    }
}

static BOOL ButtonEnabled(int b, const TargetCaps* caps) {
    if (b > BTN_SCALE) return TRUE;
    return caps->valid && caps->can[b];
}

static BOOL ButtonActive(int b, const TargetCaps* caps) {
    switch (b) {
        case BTN_MOVE: return EffectiveMode(caps) == GIZMO_TRANSLATE;
        case BTN_ROTATE: return EffectiveMode(caps) == GIZMO_ROTATE;
        case BTN_SCALE: return EffectiveMode(caps) == GIZMO_SCALE;
        case BTN_SPACE: return g_space == TRANSFORM_SPACE_LOCAL;
        case BTN_PIVOT: return g_pivot_center;
        default: return g_snap;
    }
}

static void ToolbarClick(int b) {
    switch (b) {
        case BTN_MOVE: g_gizmo.mode = GIZMO_TRANSLATE; break;
        case BTN_ROTATE: g_gizmo.mode = GIZMO_ROTATE; break;
        case BTN_SCALE: g_gizmo.mode = GIZMO_SCALE; break;
        case BTN_SPACE: g_space = g_space == TRANSFORM_SPACE_LOCAL ? TRANSFORM_SPACE_WORLD : TRANSFORM_SPACE_LOCAL; break;
        case BTN_PIVOT: g_pivot_center = !g_pivot_center; break;
        default: g_snap = !g_snap; break;
    }
}

static void DrawViewTools(float width, float height) {
    Vector2 res = RenderResolution();
    ViewProjection vp = g_view;
    if (vp.width != width || vp.height != height) vp = ViewProjectionFrom(RendererCamera()->core, res.x, res.y, width, height);
    TargetCaps caps = TargetQuery();
    GizmoFrame frame;
    GizmoMode saved = g_gizmo.mode;
    if (!g_dragging) g_gizmo.mode = EffectiveMode(&caps);
    if (TargetFrame(&caps, g_gizmo.mode, g_space, g_pivot_center, &frame)) GizmoDraw(&g_gizmo, &vp, &frame);
    g_gizmo.mode = saved;
    NavWidgetDraw(&vp);
    Rectangle rects[BTN_COUNT];
    ToolbarLayout(height, rects);
    Font font = FontAsset();
    for (int b = 0; b < BTN_COUNT; b++) {
        BOOL enabled = ButtonEnabled(b, &caps);
        BOOL active = ButtonActive(b, &caps) && enabled;
        Color bg = active ? MappedColor(PANEL_BTN_PRS_COLOR) : (b == g_toolbar_hot && enabled ? MappedColor(PANEL_BTN_HVR_COLOR) : MappedColor(PANEL_BTN_BG_COLOR));
        bg.a = 225;
        DrawRectangleRec(rects[b], bg);
        if (active) DrawRectangleLinesEx(rects[b], 1.5f, (Color){ 255, 255, 255, 180 });
        Color tc = enabled ? MappedColor(UI_TEXT_COLOR) : MappedColor(UI_TEXT_DISABLED);
        DrawTextEx(font, ButtonLabel(b), (Vector2){ rects[b].x + TOOLBAR_PAD, rects[b].y + (TOOLBAR_HEIGHT - TOOLBAR_FONT) * 0.5f }, TOOLBAR_FONT, 0, tc);
    }
}

static void DrawViewportPanel(float width, float height) {
    SetViewportSlice(width, height);
    SetViewportRec((Rectangle){ UIGetPosition().x, UIGetPosition().y, width, height });
    DrawTexturePro(
        g_viewport_target.texture,
        (Rectangle){ 0, 0, g_viewport_target.texture.width, -g_viewport_target.texture.height },
        (Rectangle){ 0, 0, g_viewport_target.texture.width, g_viewport_target.texture.height },
        (Vector2){ 0, 0 }, 0, WHITE);
    if (g_show_hints && HoveredPanel() && strcmp(HoveredPanel(), "Viewport") == 0) {
        DrawCurrentBinds(0, 0);
    }
    g_viewport_dimensions = (Vector2){ width, height };
    g_viewport_position = UIGetPosition();
    if (g_gizmos) DrawViewTools(width, height);
}

static void SetTranslateMode() {
    if (g_caps.valid && g_caps.can[GIZMO_TRANSLATE]) g_gizmo.mode = GIZMO_TRANSLATE;
}

static void SetRotateMode() {
    if (g_caps.valid && g_caps.can[GIZMO_ROTATE]) g_gizmo.mode = GIZMO_ROTATE;
}

static void SetScaleMode() {
    if (g_caps.valid && g_caps.can[GIZMO_SCALE]) g_gizmo.mode = GIZMO_SCALE;
}

static void ToggleTransformSpace() {
    if (!g_caps.valid) return;
    g_space = g_space == TRANSFORM_SPACE_LOCAL ? TRANSFORM_SPACE_WORLD : TRANSFORM_SPACE_LOCAL;
}

static void ToggleSnapping() {
    g_snap = !g_snap;
}

static void SnapZAxis() {
    NavWidgetSnap(2, FALSE, FALSE);
}

static void SnapXAxis() {
    NavWidgetSnap(0, FALSE, FALSE);
}

static void SnapYAxis() {
    NavWidgetSnap(1, FALSE, FALSE);
}

static void SnapNZAxis() {
    NavWidgetSnap(2, TRUE, FALSE);
}

static void SnapNXAxis() {
    NavWidgetSnap(0, TRUE, FALSE);
}

static void SnapNYAxis() {
    NavWidgetSnap(1, TRUE, FALSE);
}

static void ToggleGizmos() {
    g_gizmos = !g_gizmos;
}

static BOOL UpdateViewTools(float width, float height, Vector2 panel_position, BOOL hovered) {
    Vector2 m = GetMousePosition();
    vec2 mouse = { m.x - panel_position.x, m.y - panel_position.y };
    Vector2 res = RenderResolution();
    g_view = ViewProjectionFrom(RendererCamera()->core, res.x, res.y, width, height);
    BOOL inputs_ok = !InputsBlocked();
    BOOL camera_keys = InputKeyDown(IK_PAN_CAMERA) || InputKeyDown(IK_PAN_SELECTED) || InputKeyDown(IK_ZOOM);
    BOOL pressed = InputButtonPressed(IK_MOUSELEFT) && hovered && !camera_keys;
    BOOL down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    BOOL interactive = hovered && inputs_ok && !camera_keys;
    TargetCaps caps = TargetQuery();
    if (hovered && inputs_ok && !InputKeyDown(IK_DEV)) {
        g_caps = caps;
    } else {
        g_caps = (TargetCaps){ 0 };
    }
    Rectangle rects[BTN_COUNT];
    ToolbarLayout(height, rects);
    g_toolbar_hot = -1;
    BOOL toolbar_over = FALSE;
    if (interactive && !g_dragging) {
        Vector2 mp = { mouse[0], mouse[1] };
        for (int b = 0; b < BTN_COUNT; b++) if (CheckCollisionPointRec(mp, rects[b])) g_toolbar_hot = b;
        Rectangle all = { rects[0].x - 4, rects[0].y - 4, rects[BTN_COUNT - 1].x + rects[BTN_COUNT - 1].width - rects[0].x + 8, TOOLBAR_HEIGHT + 8 };
        toolbar_over = CheckCollisionPointRec(mp, all);
        if (pressed && g_toolbar_hot >= 0 && ButtonEnabled(g_toolbar_hot, &caps)) ToolbarClick(g_toolbar_hot);
    }
    BOOL nav_enabled = interactive && !toolbar_over && !g_dragging;
    NavWidgetUpdate(&g_view, mouse, nav_enabled, pressed && nav_enabled, down);
    GizmoMode mode = EffectiveMode(&caps);
    g_gizmo.mode = g_dragging ? g_gizmo.mode : mode;
    GizmoFrame frame;
    BOOL have_frame = TargetFrame(&caps, g_gizmo.mode, g_space, g_pivot_center, &frame);
    if (!have_frame) {
        if (g_dragging) {
            TargetRestore(&g_snapshot);
            GizmoReset(&g_gizmo);
            g_dragging = FALSE;
        }
        g_gizmo.hover = GIZMO_NONE;
    } else {
        GizmoInput in = { 0 };
        glm_vec2_copy(mouse, in.mouse);
        in.hover_enabled = interactive && !toolbar_over && !NavWidgetWantsMouse();
        in.pressed = pressed && in.hover_enabled;
        in.down = down;
        in.cancel = IsKeyPressed(KEY_ESCAPE);
        in.snap = g_snap != InputKeyDown(IK_LEFT_COMMAND);
        GizmoResult result;
        GizmoUpdate(&g_gizmo, &g_view, &frame, &in, &result);
        if (result.began) {
            TargetBegin(&g_snapshot, &caps, &frame, g_pivot_center);
            g_dragging = TRUE;
        }
        if (result.cancelled && g_dragging) {
            TargetRestore(&g_snapshot);
            g_dragging = FALSE;
        } else if (g_dragging && (result.active || result.ended)) {
            TargetApply(&g_snapshot, g_gizmo.mode, &result);
            if (result.ended) g_dragging = FALSE;
        }
    }
    return toolbar_over || NavWidgetWantsMouse() || GizmoWantsMouse(&g_gizmo);
}

static void UpdateViewportPanel(float width, float height) {
    const char* hpanel = HoveredPanel();
    BOOL hovered = hpanel && strcmp(hpanel, "Viewport") == 0;

    // the view widget, toolbar and transform gizmo get the first claim on the mouse
    BOOL overlay_captured = g_gizmos ? UpdateViewTools(width, height, g_viewport_position, hovered) : FALSE;

    if (InputButtonReleased(IK_MOUSERIGHT)) g_rfocused = FALSE;
    if (InputButtonReleased(IK_MOUSELEFT)) g_lfocused = FALSE;
    if (InputKeyReleased(IK_ZOOM)) {
        g_zfocused = FALSE;
        g_mousepoint[0] = GetMouseDelta().x;
        g_mousepoint[1] = GetMouseDelta().y;
    }
    if (InputButtonPressed(IK_MOUSERIGHT) && hovered) g_rfocused = TRUE;
    if (InputButtonPressed(IK_MOUSELEFT) && hovered && !overlay_captured) g_lfocused = TRUE;
    if (InputKeyPressed(IK_ZOOM) && hovered) g_zfocused = TRUE;
    SetViewportSlice(width, height);

    // camera scroll zoom
    {
        SimpleCamera camera = RendererCamera()->core;
        vec3 offset;
        glm_vec3_sub(camera.position, camera.look, offset);
        float radius = glm_vec3_norm(offset);
        float rbefore = radius;
        if (hovered) radius -= GetMouseWheelMove() * (0.05f * radius);
        if (radius < 1e-6f) radius = 1e-6f;
        if (camera.ortho) {
            if (rbefore != radius) {
                camera.orthosize *= radius / rbefore;
                if (camera.orthosize < 1e-4f) camera.orthosize = 1e-4f;
                RendererCamera()->core = camera;
            }
        } else {
        glm_vec3_normalize(offset);
        glm_vec3_scale(offset, radius, offset);
        glm_vec3_add(camera.look, offset, camera.position);
        if (rbefore != radius) RendererCamera()->core = camera;
        }
    }

    // selection controls
    BOOL camera_modifier = InputKeyDown(IK_PAN_CAMERA) || InputKeyDown(IK_PAN_SELECTED) || InputKeyDown(IK_ZOOM);
    if (InputButtonPressed(IK_MOUSELEFT) && g_lfocused && !camera_modifier) {
        switch (GetOverlayMode()) {
            case TRIANGLE_SELECT_MODE:
                TriangleID tindex = HoveredTriangle();
                if (tindex != (TriangleID)-1) {
                    SetEditTriangle(tindex);
                } else {
                    DeselectEditTarget();
                }
                break;
            case VERTEX_SELECT_MODE:
                VertexID vindex = HoveredVertex();
                if (vindex != (VertexID)-1) {
                    SetEditVertex(vindex);
                } else {
                    DeselectEditTarget();
                }
                break;
            default: DeselectEditTarget();
        }
    }

    // render
    if (g_viewport_target.texture.width != GetScreenWidth() || g_viewport_target.texture.height != GetScreenHeight()) {
        UnloadRenderTexture(g_viewport_target);
        g_viewport_target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    }
    for (size_t i = 0; i < RenderConfig()->multiplier; i++) Render();
    BeginTextureMode(g_viewport_target);
    Draw(0, 0, width, height);
    EndTextureMode();
}

Panel GenerateViewportPanel() {
	Panel p = { 0 };
    SetupPanel(&p, "Viewport");
	p.flush = TRUE;
    p.draw = DrawViewportPanel;
    p.update = UpdateViewportPanel;
    g_viewport_target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    InitializeGizmo(&g_gizmo);
    AddBind("rotate viewport camera", RotateCameraControls, (BindCommand){ IK_PAN_CAMERA, BIND_KEY_DOWN }, (BindCommand){ IK_MOUSERIGHT, BIND_BUTTON_END });
    AddBind("pan viewport camera", PanCameraControls, (BindCommand){ IK_PAN_CAMERA, BIND_KEY_DOWN }, (BindCommand){ IK_MOUSELEFT, BIND_BUTTON_END });
    AddBind("yank selected vertex", PanSelectedObject, (BindCommand){ IK_PAN_SELECTED, BIND_KEY_DOWN }, (BindCommand){ IK_MOUSELEFT, BIND_BUTTON_END });
    AddBind("zoom viewport camera", ZoomCameraControls, (BindCommand){ IK_ZOOM, BIND_KEY_END });
	AddBind("reset viewport camera", ResetViewportCamera, (BindCommand){ IK_RESET_CAMERA, BIND_KEY_PRESSED });
	AddBind("fit viewport camera to model", FitCamera, (BindCommand){ IK_FIT_CAMERA, BIND_KEY_PRESSED });
    AddBind("toggle input hints", ToggleHints, (BindCommand){ IK_TOGGLE_HINTS, BIND_KEY_PRESSED });
    AddBind("toggle primary camera preview", TogglePreview, (BindCommand){ IK_TOGGLE_PREVIEW, BIND_KEY_PRESSED });
    AddBind("translate selected mode", SetTranslateMode, (BindCommand){ IK_TRANSLATE_GIZMO, BIND_KEY_PRESSED });
    AddBind("rotate selected mode", SetRotateMode, (BindCommand){ IK_ROTATE_GIZMO, BIND_KEY_PRESSED });
    AddBind("scale selected mode", SetScaleMode, (BindCommand){ IK_SCALE_GIZMO, BIND_KEY_PRESSED });
    AddBind("toggle transform space", ToggleTransformSpace, (BindCommand){ IK_TOGGLE_SPACE, BIND_KEY_PRESSED });
    AddBind("toggle gizmo snapping", ToggleSnapping, (BindCommand){ IK_TOGGLE_SNAP, BIND_KEY_PRESSED });
    AddBind("toggle gizmos", ToggleGizmos, (BindCommand){ IK_TOGGLE_GIZMOS, BIND_KEY_PRESSED });
    AddBind("snap to X", SnapXAxis, (BindCommand){ IK_ALIGN_X, BIND_KEY_PRESSED });
    AddBind("snap to Y", SnapYAxis, (BindCommand){ IK_ALIGN_Y, BIND_KEY_PRESSED });
    AddBind("snap to Z", SnapZAxis, (BindCommand){ IK_ALIGN_Z, BIND_KEY_PRESSED });
    AddBind("snap to -X", SnapNXAxis, (BindCommand){ IK_LEFT_COMMAND, BIND_KEY_DOWN }, (BindCommand){ IK_ALIGN_X, BIND_KEY_PRESSED });
    AddBind("snap to -Y", SnapNYAxis, (BindCommand){ IK_LEFT_COMMAND, BIND_KEY_DOWN }, (BindCommand){ IK_ALIGN_Y, BIND_KEY_PRESSED });
    AddBind("snap to -Z", SnapNZAxis, (BindCommand){ IK_LEFT_COMMAND, BIND_KEY_DOWN }, (BindCommand){ IK_ALIGN_Z, BIND_KEY_PRESSED });
    return p;
}
