#include "viewport.h"
#include "renderer/animation.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"
#include "renderer/rmath.h"
#include "renderer/rig.h"
#include "ui/panels/edit.h"
#include "ui/gizmo.h"
#include <data/colors.h>
#include <data/input.h>
#include <data/fonts.h>
#include <core/binds.h>

#define BONE_PICK_PX 10.0f

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
static BOOL g_painting = FALSE;
static vec2 g_last_mouse = { -1, -1 };

static void DashedLine(Vector2 a, Vector2 b, float dash, float thick, Color col) {
    float len = hypotf(b.x - a.x, b.y - a.y);
    if (len < 1e-3f) return;
    Vector2 d = { (b.x - a.x) / len, (b.y - a.y) / len };
    for (float t = 0.0f; t < len; t += dash * 2.0f) {
        float e = fminf(t + dash, len);
        DrawLineEx((Vector2){ a.x + d.x * t, a.y + d.y * t }, (Vector2){ a.x + d.x * e, a.y + d.y * e }, thick, col);
    }
}

static BOOL IsIKTarget(const MeshAnimation* anim, size_t bone) {
    for (uint32_t i = 0; i < anim->numik; i++) if (anim->ik[i].target == bone || anim->ik[i].pole == bone) return TRUE;
    return FALSE;
}

static float Cross2(Vector2 o, Vector2 a, Vector2 b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

static void FillHull(const Vector2* pts, int n, Color col) {
    Vector2 hull[16];
    int h = 0;
    int start = 0;
    for (int i = 1; i < n; i++) if (pts[i].x < pts[start].x || (pts[i].x == pts[start].x && pts[i].y < pts[start].y)) start = i;
    int cur = start;
    do {
        hull[h++] = pts[cur];
        int next = (cur + 1) % n;
        for (int i = 0; i < n; i++) if (Cross2(pts[cur], pts[next], pts[i]) < 0.0f) next = i;
        cur = next;
    } while (cur != start && h < 16);
    for (int i = 1; i + 1 < h; i++) {
        DrawTriangle(hull[0], hull[i], hull[i + 1], col);
        DrawTriangle(hull[0], hull[i + 1], hull[i], col);
    }
}

static void DrawBone(const ViewProjection* vp, size_t animid, const MeshAnimation* anim, size_t b, BOOL active_rig, RigMode mode) {
    vec3 head, tail, axes[3];
    if (!RigBoneWorldOf(animid, b, head, tail) || !RigBoneWorldAxesOf(animid, b, axes)) return;
    const Bone* bone = &(anim->skeleton.bones[b]);
    BOOL selected = active_rig && RigBoneSelected(b);
    BOOL active = active_rig && RigActiveBone() == b;
    Color base = (Color){ 200, 205, 220, 255 };
    if (!(bone->flags & BONE_FLAG_DEFORM)) base = (Color){ 110, 200, 140, 255 };
    if (IsIKTarget(anim, b)) base = (Color){ 225, 120, 215, 255 };
    if (selected) base = (Color){ 255, 160, 50, 255 };
    if (active) base = (Color){ 255, 215, 110, 255 };
    if (!active_rig) base = (Color){ base.r / 2 + 40, base.g / 2 + 40, base.b / 2 + 40, 255 };
    Color fill = base;
    fill.a = active ? 140 : (selected ? 110 : (active_rig ? 80 : 45));
    vec3 dir;
    glm_vec3_sub(tail, head, dir);
    float len = glm_vec3_norm(dir);
    vec2 h2, t2;
    if (!ViewProject(vp, head, h2, NULL) || !ViewProject(vp, tail, t2, NULL)) return;
    Vector2 h = V2(h2), t = V2(t2);
    if (len < 1e-9f) {
        DrawCircleV(h, 4.0f, base);
        return;
    }
    glm_vec3_scale(dir, 1.0f / len, dir);
    vec3 x, z, proj;
    glm_vec3_copy(axes[0], x);
    glm_vec3_scale(dir, glm_vec3_dot(x, dir), proj);
    glm_vec3_sub(x, proj, x);
    if (glm_vec3_norm(x) < 1e-4f) {
        glm_vec3_copy(axes[2], x);
        glm_vec3_scale(dir, glm_vec3_dot(x, dir), proj);
        glm_vec3_sub(x, proj, x);
    }
    if (glm_vec3_norm(x) < 1e-6f) return;
    glm_vec3_normalize(x);
    glm_vec3_cross(dir, x, z);
    vec3 mid;
    glm_vec3_scale(dir, len * 0.1f, mid);
    glm_vec3_add(head, mid, mid);
    float w = fmaxf(len * 0.1f, 3.0f * ViewWorldPerPixel(vp, mid));
    Vector2 ring[4];
    for (int i = 0; i < 4; i++) {
        vec3 p;
        float c = (i == 0) ? 1.0f : (i == 2 ? -1.0f : 0.0f);
        float s = (i == 1) ? 1.0f : (i == 3 ? -1.0f : 0.0f);
        glm_vec3_copy(mid, p);
        glm_vec3_muladds(x, c * w, p);
        glm_vec3_muladds(z, s * w, p);
        vec2 r2;
        if (!ViewProject(vp, p, r2, NULL)) return;
        ring[i] = V2(r2);
    }
    Vector2 pts[6] = { h, t, ring[0], ring[1], ring[2], ring[3] };
    FillHull(pts, 6, fill);
    float thick = active ? 2.2f : (selected ? 1.8f : 1.2f);
    Color shadow = (Color){ 15, 15, 18, active_rig ? 200 : 120 };
    Color line = (Color){ base.r, base.g, base.b, active_rig ? 235 : 150 };
    for (int pass = 0; pass < 2; pass++) {
        float th = pass == 0 ? thick + 2.0f : thick;
        Color c = pass == 0 ? shadow : line;
        for (int i = 0; i < 4; i++) {
            DrawLineEx(h, ring[i], th, c);
            DrawLineEx(t, ring[i], th, c);
            DrawLineEx(ring[i], ring[(i + 1) % 4], th, c);
        }
    }
    Color joint = (Color){ 30, 30, 34, 255 };
    BOOL edit = mode == RIG_MODE_EDIT && active_rig;
    EditHandle handle = RigEditHandle();
    DrawCircleV(h, edit ? 4.5f : 3.5f, joint);
    DrawCircleLinesV(h, edit ? 4.5f : 3.5f, line);
    DrawCircleV(t, edit ? 3.5f : 2.5f, joint);
    DrawCircleLinesV(t, edit ? 3.5f : 2.5f, line);
    if (edit && active) {
        if (handle == EDIT_HANDLE_HEAD) DrawCircleV(h, 4.0f, (Color){ 255, 230, 140, 255 });
        if (handle == EDIT_HANDLE_TAIL) DrawCircleV(t, 3.0f, (Color){ 255, 230, 140, 255 });
    }
    if (active_rig && BoneIsPosed(anim, b) && mode != RIG_MODE_EDIT) {
        Vector2 m = { (ring[0].x + ring[2].x) * 0.5f, (ring[0].y + ring[2].y) * 0.5f };
        DrawCircleV(m, 2.5f, (Color){ 90, 200, 255, 255 });
    }
}

static BOOL RigShown(size_t a) {
    if (a >= NumAnimations()) return FALSE;
    MeshAnimation* anim = AnimationReference(a);
    if (anim->meshid >= NumMeshes() || MeshReference(anim->meshid)->disabled) return FALSE;
    return anim->enable || a == RigActiveAnimation();
}

static void DrawRig(const ViewProjection* vp, size_t a, RigMode mode) {
    MeshAnimation* anim = AnimationReference(a);
    const Skeleton* sk = &(anim->skeleton);
    BOOL active_rig = a == RigActiveAnimation();
    for (size_t b = 0; b < sk->bonecount; b++) {
        if (sk->bones[b].flags & BONE_FLAG_HIDDEN) continue;
        size_t p = sk->bones[b].parent;
        if (p >= sk->bonecount || (sk->bones[b].flags & BONE_FLAG_CONNECTED)) continue;
        vec3 ph, pt, h, t;
        vec2 a2, b2;
        if (!RigBoneWorldOf(a, p, ph, pt) || !RigBoneWorldOf(a, b, h, t)) continue;
        if (glm_vec3_distance(pt, h) < 1e-6f) continue;
        if (ViewProject(vp, pt, a2, NULL) && ViewProject(vp, h, b2, NULL)) DashedLine(V2(a2), V2(b2), 4.0f, 1.0f, (Color){ 150, 150, 160, active_rig ? 110 : 60 });
    }
    if (!(active_rig && mode == RIG_MODE_EDIT)) {
        for (uint32_t i = 0; i < anim->numik; i++) {
            IKConstraint* ik = &(anim->ik[i]);
            if (ik->bone >= sk->bonecount || ik->target >= sk->bonecount) continue;
            vec3 th, tt, gh, gt;
            vec2 a2, b2;
            if (!RigBoneWorldOf(a, ik->bone, th, tt) || !RigBoneWorldOf(a, ik->target, gh, gt)) continue;
            Color c = ik->enabled ? (Color){ 255, 220, 80, active_rig ? 200 : 100 } : (Color){ 140, 140, 140, 120 };
            if (ViewProject(vp, tt, a2, NULL) && ViewProject(vp, gh, b2, NULL)) DashedLine(V2(a2), V2(b2), 5.0f, 1.5f, c);
            if (ik->pole < sk->bonecount) {
                vec3 ph, pt;
                if (RigBoneWorldOf(a, ik->pole, ph, pt) && ViewProject(vp, th, a2, NULL) && ViewProject(vp, ph, b2, NULL))
                    DashedLine(V2(a2), V2(b2), 3.0f, 1.0f, (Color){ 225, 120, 215, 160 });
            }
        }
    }
    for (int pass = 0; pass < 2; pass++)
        for (size_t b = 0; b < sk->bonecount; b++) {
            if (sk->bones[b].flags & BONE_FLAG_HIDDEN) continue;
            if ((active_rig && RigBoneSelected(b) ? 1 : 0) != pass) continue;
            DrawBone(vp, a, anim, b, active_rig, mode);
        }
}

void RigViewportDraw(const ViewProjection* vp, float width, float height) {
    if (!RigViewportActive()) return;
    RigMode mode = RigGetMode();
    size_t active = RigActiveAnimation();
    for (size_t a = 0; a < NumAnimations(); a++) if (a != active && RigShown(a)) DrawRig(vp, a, mode);
    if (active != (size_t)-1 && RigShown(active)) DrawRig(vp, active, mode);
    MeshAnimation* anim = RigActive();
    if (anim && mode == RIG_MODE_WEIGHTS) {
        vec3 hit, normal;
        if (RigBrushHit(hit, normal)) {
            vec2 c2;
            if (ViewProject(vp, hit, c2, NULL)) {
                float px = RigBrush()->radius / fmaxf(ViewWorldPerPixel(vp, hit), 1e-9f);
                Color ring = g_painting ? (Color){ 255, 255, 255, 230 } : (Color){ 255, 255, 255, 160 };
                DrawCircleLinesV(V2(c2), px, ring);
                DrawCircleLinesV(V2(c2), px * glm_clamp(RigBrush()->strength, 0.05f, 1.0f), (Color){ 255, 255, 255, 70 });
            }
        }
    }
    const char* names[RIG_MODE_COUNT] = { "EDIT RIG", "POSE", "WEIGHT PAINT" };
    char label[400];
    size_t bone = RigActiveBone();
    if (anim) {
        snprintf(label, sizeof(label), "ANIMATION  |  %s  |  frame %.0f%s%.200s", names[mode], Timeline()->frame,
            bone != (size_t)-1 ? "  |  " : "", bone != (size_t)-1 ? anim->skeleton.bones[bone].name : "");
    } else {
        snprintf(label, sizeof(label), "ANIMATION  |  click a bone to pick a rig");
    }
    Font font = FontAsset();
    Vector2 size = MeasureTextEx(font, label, 16, 0);
    float x = (width - size.x) * 0.5f;
    DrawRectangle(x - 8, 6, size.x + 16, 22, (Color){ 0, 0, 0, 150 });
    DrawTextEx(font, label, (Vector2){ x, 9 }, 16, 0, mode == RIG_MODE_WEIGHTS ? (Color){ 255, 210, 120, 255 } : (Color){ 230, 230, 235, 255 });
}

static void PickBone(size_t animid, size_t b, BOOL shift) {
    if (animid != RigActiveAnimation()) {
        RigSetActiveMesh(AnimationReference(animid)->meshid);
        shift = FALSE;
    }
    if (shift && RigBoneSelected(b) && RigActiveBone() == b) RigDeselectBone(b);
    else RigSelectBone(b, shift);
    if (RigActiveBone() != (size_t)-1) SetEditBone(RigActiveBone());
    else DeselectEditTarget();
}

BOOL RigViewportUpdate(const ViewProjection* vp, const vec2 mouse, BOOL hovered, BOOL pressed, BOOL down) {
    if (!down) g_painting = FALSE;
    if (!RigViewportActive()) return FALSE;
    MeshAnimation* anim = RigActive();
    RigMode mode = RigGetMode();
    BOOL keys = hovered && !InputsBlocked();
    BOOL ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    BOOL alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    BOOL shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (keys && anim) {
        if (IsKeyPressed(KEY_K) && mode == RIG_MODE_POSE) RigKeySelected(KEY_CHANNEL_ALL);
        if (ctrl && mode == RIG_MODE_POSE) {
            if (IsKeyPressed(KEY_R)) RigResetPose(RigSelectedCount() > 0, KEY_CHANNEL_ROTATION);
            if (IsKeyPressed(KEY_G)) RigResetPose(RigSelectedCount() > 0, KEY_CHANNEL_LOCATION);
            if (IsKeyPressed(KEY_S)) RigResetPose(RigSelectedCount() > 0, KEY_CHANNEL_SCALE);
            if (IsKeyPressed(KEY_C)) RigCopyPose();
            if (IsKeyPressed(KEY_V) && !shift) RigPastePose(FALSE);
        }
        if (mode == RIG_MODE_EDIT && ctrl && IsKeyPressed(KEY_E) && RigActiveBone() != (size_t)-1) {
            size_t made = RigExtrudeBone(RigActiveBone());
            if (made != (size_t)-1) { RigSelectBone(made, FALSE); SetEditBone(made); }
        }
        if (mode == RIG_MODE_WEIGHTS) {
            if (IsKeyPressed(KEY_LEFT_BRACKET)) RigBrush()->radius *= 0.85f;
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) RigBrush()->radius /= 0.85f;
        }
    }
    if (anim && mode == RIG_MODE_WEIGHTS && !(pressed && alt)) {
        if (pressed) g_painting = TRUE;
        BOOL moved = mouse[0] != g_last_mouse[0] || mouse[1] != g_last_mouse[1];
        if (hovered && (moved || g_painting)) RigPaint(vp, mouse, g_painting && down, ctrl, GetFrameTime());
        g_last_mouse[0] = mouse[0];
        g_last_mouse[1] = mouse[1];
        return pressed;
    }
    if (!pressed) return FALSE;
    size_t animid = (size_t)-1;
    size_t b = RigPickAnyBone(vp, mouse, BONE_PICK_PX, &animid);
    if (b != (size_t)-1) {
        PickBone(animid, b, shift);
        return TRUE;
    }
    if (!shift) {
        RigClearSelection();
        EditType type;
        size_t index;
        if (GetEditTarget(&type, &index) && type == EDIT_BONE) DeselectEditTarget();
    }
    return TRUE;
}

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
    float y = height - TOOLBAR_HEIGHT - TOOLBAR_MARGIN - 25;
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
    {
        Vector2 res = RenderResolution();
        ViewProjection vp = g_view;
        if (vp.width != width || vp.height != height) vp = ViewProjectionFrom(RendererCamera()->core, res.x, res.y, width, height);
        RigViewportDraw(&vp, width, height);
    }
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
            if (result.ended) {
                g_dragging = FALSE;
                TargetEnd(&g_snapshot);
            }
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

    BOOL camera_modifier = InputKeyDown(IK_PAN_CAMERA) || InputKeyDown(IK_PAN_SELECTED) || InputKeyDown(IK_ZOOM);
    Vector2 res = RenderResolution();
    g_view = ViewProjectionFrom(RendererCamera()->core, res.x, res.y, width, height);
    vec2 rigmouse = { GetMousePosition().x - g_viewport_position.x, GetMousePosition().y - g_viewport_position.y };
    BOOL rigpressed = InputButtonPressed(IK_MOUSELEFT) && g_lfocused && !camera_modifier && !InputsBlocked();
    BOOL rigdown = IsMouseButtonDown(MOUSE_BUTTON_LEFT) && g_lfocused && !camera_modifier;
    BOOL rigconsumed = RigViewportUpdate(&g_view, rigmouse, hovered && !overlay_captured, rigpressed, rigdown);

    // selection controls
    if (InputButtonPressed(IK_MOUSELEFT) && g_lfocused && !camera_modifier && !rigconsumed) {
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
            case LIGHT_SELECT_MODE:
                LightID lindex = HoveredLight();
                if (lindex != (LightID)-1) {
                    SetEditLight(lindex);
                } else {
                    DeselectEditTarget();
                }
                break;
            case CAMERA_SELECT_MODE:
                size_t cindex = HoveredCamera();
                if (cindex != (size_t)-1) {
                    SetEditCamera(cindex);
                } else {
                    DeselectEditTarget();
                }
                break;
            case OBJECT_SELECT_MODE:
                size_t oindex = HoveredObject();
                if (oindex != (size_t)-1) {
                    SetEditMesh(oindex);
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
    AddBind("delete selected", DeleteEditTarget, (BindCommand){ IK_DELETE, BIND_KEY_PRESSED });
    return p;
}
