#include "gizmo.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include <data/colors.h>
#include <data/fonts.h>
#include <string.h>
#include <math.h>

static int g_hot = -1;
static BOOL g_over = FALSE;
static BOOL g_dragging = FALSE;
static BOOL g_animating = FALSE;
static float g_target_theta = 0.0f;
static float g_target_phi = 0.0f;
static BOOL g_target_keeps_theta = FALSE;
static const char* NAV_NAMES[3] = { "X", "Y", "Z" };

#define PICK_RADIUS 9.0f
#define CENTER_RADIUS 11.0f
#define VIEW_RING_PENALTY 3.0f
#define AXIS_MIN_VISIBLE 0.2f
#define PLANE_MIN_FACING 0.15f
#define PLANE_NEAR 0.28f
#define PLANE_FAR 0.52f
#define RING_RADIUS 0.80f
#define VIEW_RING_RADIUS 0.95f
#define ROTATE_PLANE_MIN_FACING 0.3f
#define MIN_SCALE 1e-3f
#define NAV_RADIUS 42.0f
#define NAV_BALL 11.0f
#define NAV_MARGIN 22.0f
#define POLE_EPSILON 0.001f
#define SNAP_SPEED 14.0f

static Color AxisColor(size_t i) {
    EZ_ASSERT(i < 3, "Invalid axis color requested");
    Color colors[3] = { MappedColor(X_AXIS_COLOR), MappedColor(Y_AXIS_COLOR), MappedColor(Z_AXIS_COLOR) };
    return colors[i];
}

static BOOL GizmoClosestOnLine(const vec3 point, const vec3 dir, const vec3 ray_origin, const vec3 ray_dir, float* t) {
    vec3 r = { 0 };
    glm_vec3_sub((float*)point, (float*)ray_origin, r);
    float b = glm_vec3_dot((float*)dir, (float*)ray_dir);
    float c = glm_vec3_dot((float*)dir, r);
    float e = glm_vec3_dot((float*)ray_dir, r);
    float den = 1.0f - b * b;
    if (den < 1e-4f) return FALSE;
    *t = (b * e - c) / den;
    return TRUE;
}

static BOOL GizmoRayPlane(const vec3 point, const vec3 normal, const vec3 ray_origin, const vec3 ray_dir, vec3 hit) {
    float denom = glm_vec3_dot((float*)ray_dir, (float*)normal);
    if (fabsf(denom) < 1e-5f) return FALSE;
    vec3 to_plane = { 0 };
    glm_vec3_sub((float*)point, (float*)ray_origin, to_plane);
    float s = glm_vec3_dot(to_plane, (float*)normal) / denom;
    if (s < 0.0f) return FALSE;
    vec3 offset = { 0 };
    glm_vec3_scale((float*)ray_dir, s, offset);
    glm_vec3_add((float*)ray_origin, offset, hit);
    return TRUE;
}

static GizmoHandle PickRing(const GizmoLayout* lay, const vec2 m, int* ring_out, int* segment_out) {
    float best = PICK_RADIUS;
    GizmoHandle handle = GIZMO_NONE;
    for (int i = 0; i < 3; i++) {
        for (int s = 0; s < GIZMO_RING_SEGMENTS; s++) {
            if (!lay->ring_front[i][s]) continue;
            float d = DistToSegment2D(m, lay->ring[i][s], lay->ring[i][s + 1]);
            if (d < best) {
                best = d;
                handle = (GizmoHandle)(GIZMO_AXIS_X + i);
                if (ring_out) *ring_out = i;
                if (segment_out) *segment_out = s;
            }
        }
    }
    float vd = fabsf(Dist2D(m, lay->pivot2d) - lay->view_ring_px) + VIEW_RING_PENALTY;
    if (vd < PICK_RADIUS && vd < best) handle = GIZMO_VIEW_RING;
    return handle;
}

static GizmoHandle Pick(const Gizmo* g, const GizmoLayout* lay, const vec2 m) {
    if (!lay->valid) return GIZMO_NONE;
    if (g->mode == GIZMO_ROTATE) return PickRing(lay, m, NULL, NULL);
    if (Dist2D(m, lay->pivot2d) <= CENTER_RADIUS) return GIZMO_CENTER;
    if (g->mode == GIZMO_TRANSLATE) {
        for (int i = 0; i < 3; i++)
            if (lay->plane_visible[i] && PointInQuad(m, (const vec2*)lay->quad[i])) return (GizmoHandle)(GIZMO_PLANE_YZ + i);
    }
    float best = PICK_RADIUS;
    GizmoHandle handle = GIZMO_NONE;
    for (int i = 0; i < 3; i++) {
        if (!lay->axis_visible[i]) continue;
        float d = DistToSegment2D(m, lay->pivot2d, lay->tip[i]);
        if (d < best) {
            best = d;
            handle = (GizmoHandle)(GIZMO_AXIS_X + i);
        }
    }
    return handle;
}

static BOOL BeginDrag(Gizmo* g, const ViewProjection* vp, const GizmoFrame* frame, const GizmoLayout* lay, GizmoHandle h, const vec2 mouse) {
    vec3 ro, rd, fwd;
    ViewRay(vp, mouse, ro, rd);
    ViewForward(vp, fwd);
    memcpy(&g->start_frame, frame, sizeof(GizmoFrame));
    glm_vec2_copy((float*)mouse, g->start_mouse);
    glm_vec2_copy((float*)lay->pivot2d, g->pivot2d);
    g->start_length = lay->length;
    g->plane_rotation = FALSE;
    g->angle_prev_raw = 0.0f;
    g->angle_accum = 0.0f;
    g->start_param = 0.0f;
    if (g->mode == GIZMO_TRANSLATE) {
        if (h >= GIZMO_AXIS_X && h <= GIZMO_AXIS_Z) return GizmoClosestOnLine(frame->pivot, frame->axes[h], ro, rd, &g->start_param);
        if (h >= GIZMO_PLANE_YZ && h <= GIZMO_PLANE_XY) return GizmoRayPlane(frame->pivot, frame->axes[h - GIZMO_PLANE_YZ], ro, rd, g->start_hit);
        if (h == GIZMO_CENTER) return GizmoRayPlane(frame->pivot, fwd, ro, rd, g->start_hit);
        return FALSE;
    }
    if (g->mode == GIZMO_SCALE) {
        if (h >= GIZMO_AXIS_X && h <= GIZMO_AXIS_Z) return GizmoClosestOnLine(frame->pivot, frame->axes[h], ro, rd, &g->start_param);
        return h == GIZMO_CENTER;
    }
    if (h == GIZMO_VIEW_RING) {
        g->start_angle2d = atan2f(mouse[1] - lay->pivot2d[1], mouse[0] - lay->pivot2d[0]);
        return TRUE;
    }
    if (h >= GIZMO_AXIS_X && h <= GIZMO_AXIS_Z) {
        vec3 to_pivot;
        glm_vec3_sub((float*)frame->pivot, (float*)vp->position, to_pivot);
        glm_vec3_normalize(to_pivot);
        float facing = fabsf(glm_vec3_dot((float*)frame->axes[h], to_pivot));
        vec3 hit;
        if (facing > ROTATE_PLANE_MIN_FACING && GizmoRayPlane(frame->pivot, frame->axes[h], ro, rd, hit)) {
            glm_vec3_sub(hit, (float*)frame->pivot, g->start_dir);
            if (glm_vec3_norm(g->start_dir) > 1e-6f) {
                glm_vec3_normalize(g->start_dir);
                g->plane_rotation = TRUE;
                return TRUE;
            }
        }
        int ring = h - GIZMO_AXIS_X, segment = -1;
        float best = 1e30f;
        for (int s = 0; s < GIZMO_RING_SEGMENTS; s++) {
            if (!lay->ring_front[ring][s]) continue;
            float d = DistToSegment2D(mouse, lay->ring[ring][s], lay->ring[ring][s + 1]);
            if (d < best) { best = d; segment = s; }
        }
        if (segment < 0) return FALSE;
        g->tangent2d[0] = lay->ring[ring][segment + 1][0] - lay->ring[ring][segment][0];
        g->tangent2d[1] = lay->ring[ring][segment + 1][1] - lay->ring[ring][segment][1];
        float len = sqrtf(g->tangent2d[0] * g->tangent2d[0] + g->tangent2d[1] * g->tangent2d[1]);
        if (len < 1e-6f) return FALSE;
        g->tangent2d[0] /= len;
        g->tangent2d[1] /= len;
        g->ring_px = lay->ring_px;
        return TRUE;
    }
    return FALSE;
}

static float AccumulateAngle(Gizmo* g, float raw) {
    g->angle_accum += WrapPi(raw - g->angle_prev_raw);
    g->angle_prev_raw = raw;
    return g->angle_accum;
}

static void UpdateDrag(Gizmo* g, const ViewProjection* vp, const GizmoInput* in, GizmoResult* res) {
    vec3 ro, rd, fwd;
    ViewRay(vp, in->mouse, ro, rd);
    ViewForward(vp, fwd);
    const GizmoFrame* f = &g->start_frame;
    GizmoHandle h = g->active;
    if (g->mode == GIZMO_TRANSLATE) {
        if (h >= GIZMO_AXIS_X && h <= GIZMO_AXIS_Z) {
            float t = 0.0f;
            if (GizmoClosestOnLine(f->pivot, f->axes[h], ro, rd, &t)) {
                float delta = t - g->start_param;
                if (in->snap) delta = SnapTo(delta, g->snap_translate);
                glm_vec3_scale((float*)f->axes[h], delta, res->translation);
            }
        } else {
            vec3 normal, hit;
            if (h == GIZMO_CENTER) glm_vec3_copy(fwd, normal);
            else glm_vec3_copy((float*)f->axes[h - GIZMO_PLANE_YZ], normal);
            if (GizmoRayPlane(f->pivot, normal, ro, rd, hit)) {
                vec3 delta;
                glm_vec3_sub(hit, g->start_hit, delta);
                if (in->snap) {
                    vec3 snapped = { 0.0f, 0.0f, 0.0f };
                    for (int i = 0; i < 3; i++) {
                        vec3 part;
                        float c = SnapTo(glm_vec3_dot(delta, (float*)f->axes[i]), g->snap_translate);
                        glm_vec3_scale((float*)f->axes[i], c, part);
                        glm_vec3_add(snapped, part, snapped);
                    }
                    glm_vec3_copy(snapped, delta);
                }
                glm_vec3_copy(delta, res->translation);
            }
        }
        return;
    }
    if (g->mode == GIZMO_SCALE) {
        float factor = 1.0f;
        BOOL have = FALSE;
        if (h >= GIZMO_AXIS_X && h <= GIZMO_AXIS_Z) {
            float t = 0.0f;
            if (GizmoClosestOnLine(f->pivot, f->axes[h], ro, rd, &t)) {
                float reference = fabsf(g->start_param);
                if (reference < 0.5f * g->start_length) reference = 0.5f * g->start_length;
                if (g->start_param < 0.0f) reference = -reference;
                factor = 1.0f + (t - g->start_param) / reference;
                have = TRUE;
            }
        } else {
            float delta = (in->mouse[0] - g->start_mouse[0]) - (in->mouse[1] - g->start_mouse[1]);
            factor = 1.0f + 0.5f * delta / g->size;
            have = TRUE;
        }
        if (have) {
            if (in->snap) factor = SnapTo(factor, g->snap_scale);
            if (fabsf(factor) < MIN_SCALE) factor = factor < 0.0f ? -MIN_SCALE : MIN_SCALE;
            glm_vec3_one(res->scale);
            if (h == GIZMO_CENTER) glm_vec3_broadcast(factor, res->scale);
            else res->scale[h] = factor;
        }
        return;
    }
    float angle = res->angle;
    BOOL have = FALSE;
    if (h == GIZMO_VIEW_RING) {
        float raw = -(atan2f(in->mouse[1] - g->pivot2d[1], in->mouse[0] - g->pivot2d[0]) - g->start_angle2d);
        angle = AccumulateAngle(g, WrapPi(raw));
        glm_vec3_copy((float*)vp->w, res->rotation_axis);
        have = TRUE;
    } else {
        glm_vec3_copy((float*)f->axes[h], res->rotation_axis);
        if (g->plane_rotation) {
            vec3 hit;
            if (GizmoRayPlane(f->pivot, f->axes[h], ro, rd, hit)) {
                vec3 v, c;
                glm_vec3_sub(hit, (float*)f->pivot, v);
                if (glm_vec3_norm(v) > 1e-6f) {
                    glm_vec3_normalize(v);
                    glm_vec3_cross(g->start_dir, v, c);
                    angle = AccumulateAngle(g, atan2f(glm_vec3_dot(c, (float*)f->axes[h]), glm_vec3_dot(g->start_dir, v)));
                    have = TRUE;
                }
            }
        } else {
            float dx = in->mouse[0] - g->start_mouse[0], dy = in->mouse[1] - g->start_mouse[1];
            angle = (dx * g->tangent2d[0] + dy * g->tangent2d[1]) / (g->ring_px > 1.0f ? g->ring_px : 1.0f);
            have = TRUE;
        }
    }
    if (have) {
        if (in->snap) {
            float step = glm_rad(g->snap_rotate);
            angle = SnapTo(angle, step);
        }
        res->angle = angle;
    }
}

static Color WithAlpha(Color c, float a) {
    c.a = (unsigned char)(255.0f * (a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a)));
    return c;
}

static Vector2 V2(const vec2 v) {
    return (Vector2){ v[0], v[1] };
}

static void FillTriangle(Vector2 a, Vector2 b, Vector2 c, Color col) {
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, b, col);
}

static void FillQuad(const vec2 q[4], Color col) {
    FillTriangle(V2(q[0]), V2(q[1]), V2(q[2]), col);
    FillTriangle(V2(q[0]), V2(q[2]), V2(q[3]), col);
}

static void DrawArrowHead(Vector2 from, Vector2 tip, Color col) {
    float dx = tip.x - from.x, dy = tip.y - from.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    dx /= len;
    dy /= len;
    tip.x += dx * 15.0f;
    tip.y += dy * 15.0f;
    Vector2 base = { tip.x - dx * 16.0f, tip.y - dy * 16.0f };
    Vector2 l = { base.x - dy * 6.0f, base.y + dx * 6.0f };
    Vector2 r = { base.x + dy * 6.0f, base.y - dx * 6.0f };
    FillTriangle(tip, l, r, col);
}

static void DrawReadout(const Gizmo* g, const GizmoLayout* lay) {
    if (g->active == GIZMO_NONE) return;
    char text[96];
    const GizmoResult* r = &g->last;
    if (g->mode == GIZMO_TRANSLATE) snprintf(text, sizeof(text), "%+.3f  %+.3f  %+.3f", r->translation[0], r->translation[1], r->translation[2]);
    else if (g->mode == GIZMO_ROTATE) snprintf(text, sizeof(text), "%+.1f deg", glm_deg(r->angle));
    else snprintf(text, sizeof(text), "x%.2f  x%.2f  x%.2f", r->scale[0], r->scale[1], r->scale[2]);
    Font font = FontAsset();
    Vector2 size = MeasureTextEx(font, text, 18, 0);
    Vector2 pos = { lay->pivot2d[0] + 18.0f, lay->pivot2d[1] + g->size + 14.0f };
    DrawRectangleRounded((Rectangle){ pos.x - 6, pos.y - 3, size.x + 12, size.y + 6 }, 0.3f, 6, (Color){ 20, 22, 26, 200 });
    DrawTextEx(font, text, pos, 18, 0, WHITE);
}

static void CameraAngles(const SimpleCamera* c, float* radius, float* theta, float* phi) {
    vec3 offset;
    glm_vec3_sub((float*)c->position, (float*)c->look, offset);
    *radius = glm_vec3_norm(offset);
    if (*radius < 1e-6f) *radius = 1e-6f;
    *phi = acosf(glm_clamp(offset[1] / *radius, -1.0f, 1.0f));
    *theta = atan2f(offset[2], offset[0]);
}

static void SetCameraAngles(SimpleCamera* c, float radius, float theta, float phi) {
    phi = glm_clamp(phi, POLE_EPSILON, GLM_PIf - POLE_EPSILON);
    c->position[0] = c->look[0] + radius * sinf(phi) * cosf(theta);
    c->position[1] = c->look[1] + radius * cosf(phi);
    c->position[2] = c->look[2] + radius * sinf(phi) * sinf(theta);
}

static void TickSnap(void) {
    if (!g_animating) return;
    SimpleCamera camera = RendererCamera()->core;
    float radius, theta, phi;
    CameraAngles(&camera, &radius, &theta, &phi);
    float k = 1.0f - expf(-GetFrameTime() * SNAP_SPEED);
    float dtheta = g_target_keeps_theta ? 0.0f : WrapPi(g_target_theta - theta);
    float dphi = g_target_phi - phi;
    if (fabsf(dtheta) < 0.002f && fabsf(dphi) < 0.002f) {
        if (!g_target_keeps_theta) theta = g_target_theta;
        SetCameraAngles(&camera, radius, theta, g_target_phi);
        g_animating = FALSE;
    } else {
        SetCameraAngles(&camera, radius, theta + dtheta * k, phi + dphi * k);
    }
    RendererCamera()->core = camera;
}

static float NearestAngle(float a, float reference) {
    return reference + Wrap180(a - reference);
}

static void Mat3Axis(mat3 m, int i, vec3 out) {
    glm_vec3_copy(m[i], out);
    glm_vec3_normalize(out);
}

static void WorldAxes(GizmoFrame* f) {
    glm_vec3_copy((vec3){ 1, 0, 0 }, f->axes[0]);
    glm_vec3_copy((vec3){ 0, 1, 0 }, f->axes[1]);
    glm_vec3_copy((vec3){ 0, 0, 1 }, f->axes[2]);
}

static void FrameFromDirection(const vec3 z, GizmoFrame* f) {
    vec3 zz, ref = { 0, 1, 0 }, x, y;
    glm_vec3_normalize_to((float*)z, zz);
    if (fabsf(glm_vec3_dot(zz, ref)) > 0.99f) glm_vec3_copy((vec3){ 1, 0, 0 }, ref);
    glm_vec3_cross(ref, zz, x);
    glm_vec3_normalize(x);
    glm_vec3_cross(zz, x, y);
    glm_vec3_copy(x, f->axes[0]);
    glm_vec3_copy(y, f->axes[1]);
    glm_vec3_copy(zz, f->axes[2]);
}

static BOOL TriangleVertices(size_t index, VertexID ids[3]) {
    if (index >= NumTriangles()) return FALSE;
    Triangle* t = TriangleReference(index);
    ids[0] = t->a;
    ids[1] = t->b;
    ids[2] = t->c;
    for (int i = 0; i < 3; i++) if (ids[i] >= NumVertices()) return FALSE;
    return TRUE;
}

static void WritePoints(const TargetSnapshot* snap, vec3 points[3]) {
    for (int i = 0; i < snap->count; i++) {
        float* v = VertexReference(snap->ids[i]);
        v[0] = points[i][0];
        v[1] = points[i][1];
        v[2] = points[i][2];
    }
    if (snap->type == EDIT_SINGLE_VERTEX && VertexLocked(snap->ids[0])) RigidDeform();
    UpdateVertices();
}

static void GizmoComputeLayout(const Gizmo* g, const ViewProjection* vp, const GizmoFrame* frame, GizmoLayout* out) {
    memset(out, 0, sizeof(GizmoLayout));
    if (!ViewProject(vp, frame->pivot, out->pivot2d, &out->depth)) return;
    out->valid = TRUE;
    out->world_per_px = ViewWorldPerPixel(vp, frame->pivot);
    out->length = g->size * out->world_per_px;
    out->ring_px = g->size * RING_RADIUS;
    out->view_ring_px = g->size * VIEW_RING_RADIUS;
    vec3 to_pivot = { 0 };
    glm_vec3_sub((float*)frame->pivot, (float*)vp->position, to_pivot);
    glm_vec3_normalize(to_pivot);
    for (int i = 0; i < 3; i++) {
        out->sign[i] = 1.0f;
        glm_vec3_scale((float*)frame->axes[i], out->sign[i], out->dir[i]);
        vec3 tip = { 0 };
        glm_vec3_scale(out->dir[i], out->length, tip);
        glm_vec3_add(tip, (float*)frame->pivot, tip);
        out->axis_visible[i] = ViewProject(vp, tip, out->tip[i], NULL) && Dist2D(out->tip[i], out->pivot2d) >= g->size * AXIS_MIN_VISIBLE;
    }
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3, k = (i + 2) % 3;
        float corners[4][2] = { { PLANE_NEAR, PLANE_NEAR }, { PLANE_FAR, PLANE_NEAR }, { PLANE_FAR, PLANE_FAR }, { PLANE_NEAR, PLANE_FAR } };
        BOOL ok = fabsf(glm_vec3_dot((float*)frame->axes[i], to_pivot)) > PLANE_MIN_FACING;
        for (int c = 0; c < 4 && ok; c++) {
            vec3 a, b, p = { 0 };
            glm_vec3_scale(out->dir[j], corners[c][0] * out->length, a);
            glm_vec3_scale(out->dir[k], corners[c][1] * out->length, b);
            glm_vec3_add(a, b, p);
            glm_vec3_add(p, (float*)frame->pivot, p);
            ok = ViewProject(vp, p, out->quad[i][c], NULL);
        }
        out->plane_visible[i] = ok;
    }
    float radius = out->ring_px * out->world_per_px;
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3, k = (i + 2) % 3;
        BOOL point_ok[GIZMO_RING_SEGMENTS + 1];
        BOOL point_front[GIZMO_RING_SEGMENTS + 1];
        for (int s = 0; s <= GIZMO_RING_SEGMENTS; s++) {
            float theta = 2.0f * GLM_PIf * (float)s / (float)GIZMO_RING_SEGMENTS;
            vec3 offset, a, b, p = { 0 };
            glm_vec3_scale((float*)frame->axes[j], cosf(theta) * radius, a);
            glm_vec3_scale((float*)frame->axes[k], sinf(theta) * radius, b);
            glm_vec3_add(a, b, offset);
            glm_vec3_add(offset, (float*)frame->pivot, p);
            point_ok[s] = ViewProject(vp, p, out->ring[i][s], NULL);
            vec3 to_cam = { 0 };
            glm_vec3_sub((float*)vp->position, p, to_cam);
            point_front[s] = glm_vec3_dot(offset, to_cam) > 0.0f;
        }
        for (int s = 0; s < GIZMO_RING_SEGMENTS; s++)
            out->ring_front[i][s] = point_ok[s] && point_ok[s + 1] && point_front[s] && point_front[s + 1];
    }
}

static void NavWidgetLayout(const ViewProjection* vp, float panel_w, NavBall balls[6], vec2 center) {
    center[0] = panel_w - NAV_RADIUS - NAV_BALL - NAV_MARGIN;
    center[1] = NAV_RADIUS + NAV_BALL + NAV_MARGIN;
    for (int i = 0; i < 6; i++) {
        vec3 a = { 0.0f, 0.0f, 0.0f };
        a[i / 2] = (i % 2) ? -1.0f : 1.0f;
        balls[i].axis = i / 2;
        balls[i].negative = (i % 2) == 1;
        balls[i].pos[0] = center[0] + glm_vec3_dot(a, (float*)vp->u) * NAV_RADIUS;
        balls[i].pos[1] = center[1] - glm_vec3_dot(a, (float*)vp->v) * NAV_RADIUS;
        balls[i].toward = glm_vec3_dot(a, (float*)vp->w);
    }
    for (int i = 1; i < 6; i++) {
        NavBall key = balls[i];
        int j = i - 1;
        while (j >= 0 && balls[j].toward > key.toward) { balls[j + 1] = balls[j]; j--; }
        balls[j + 1] = key;
    }
}

static void NavWidgetOrbit(float dx, float dy) {
    SimpleCamera camera = RendererCamera()->core;
    float radius, theta, phi;
    CameraAngles(&camera, &radius, &theta, &phi);
    SetCameraAngles(&camera, radius, theta + dx / 400.0f, phi - dy / 225.0f);
    RendererCamera()->core = camera;
    g_animating = FALSE;
}

static void TargetMat3FromEuler(const vec3 degrees, mat3 out) {
    mat4 r = GLM_MAT4_IDENTITY_INIT;
    glm_rotate_x(r, glm_rad(degrees[0]), r);
    glm_rotate_y(r, glm_rad(degrees[1]), r);
    glm_rotate_z(r, glm_rad(degrees[2]), r);
    glm_mat4_pick3(r, out);
}

static void TargetEulerFromMat3(mat3 m, const vec3 previous, vec3 out) {
    float r02 = m[2][0], r12 = m[2][1], r22 = m[2][2], r01 = m[1][0], r00 = m[0][0];
    float b = asinf(glm_clamp(r02, -1.0f, 1.0f));
    float a, c;
    if (fabsf(cosf(b)) < 1e-4f) {
        c = glm_rad(previous[2]);
        mat3 rzt, rest;
        vec3 zc = { 0.0f, 0.0f, -glm_deg(c) };
        TargetMat3FromEuler(zc, rzt);
        glm_mat3_mul(m, rzt, rest);
        a = atan2f(rest[1][2], rest[1][1]);
    } else {
        a = atan2f(-r12, r22);
        c = atan2f(-r01, r00);
    }
    float s1[3] = { glm_deg(a), glm_deg(b), glm_deg(c) };
    float s2[3] = { glm_deg(a) + 180.0f, 180.0f - glm_deg(b), glm_deg(c) + 180.0f };
    float d1 = 0.0f, d2 = 0.0f;
    for (int i = 0; i < 3; i++) {
        s1[i] = NearestAngle(s1[i], previous[i]);
        s2[i] = NearestAngle(s2[i], previous[i]);
        d1 += fabsf(s1[i] - previous[i]);
        d2 += fabsf(s2[i] - previous[i]);
    }
    for (int i = 0; i < 3; i++) out[i] = d1 <= d2 ? s1[i] : s2[i];
}

static void TargetMat3AxisAngle(const vec3 axis, float angle, mat3 out) {
    vec3 a;
    glm_vec3_normalize_to((float*)axis, a);
    float c = cosf(angle), s = sinf(angle), t = 1.0f - c;
    out[0][0] = t * a[0] * a[0] + c;        out[1][0] = t * a[0] * a[1] - s * a[2]; out[2][0] = t * a[0] * a[2] + s * a[1];
    out[0][1] = t * a[0] * a[1] + s * a[2]; out[1][1] = t * a[1] * a[1] + c;        out[2][1] = t * a[1] * a[2] - s * a[0];
    out[0][2] = t * a[0] * a[2] - s * a[1]; out[1][2] = t * a[1] * a[2] + s * a[0]; out[2][2] = t * a[2] * a[2] + c;
}

void InitializeGizmo(Gizmo* g) {
    memset(g, 0, sizeof(Gizmo));
    g->mode = GIZMO_TRANSLATE;
    g->size = GIZMO_DEFAULT_SIZE;
    g->snap_translate = GIZMO_DEFAULT_SNAP_TRANSLATE;
    g->snap_rotate = GIZMO_DEFAULT_SNAP_ROTATE;
    g->snap_scale = GIZMO_DEFAULT_SNAP_SCALE;
    g->hover = GIZMO_NONE;
    g->active = GIZMO_NONE;
    g->last.handle = GIZMO_NONE;
    glm_vec3_one(g->last.scale);
    glm_vec3_copy((vec3){ 0.0f, 0.0f, 1.0f }, g->last.rotation_axis);
}

void GizmoUpdate(Gizmo* g, const ViewProjection* vp, const GizmoFrame* frame, const GizmoInput* in, GizmoResult* out) {
    GizmoResult res = g->last;
    res.began = FALSE;
    res.ended = FALSE;
    res.cancelled = FALSE;
    if (g->active != GIZMO_NONE) {
        if (in->cancel) {
            res.cancelled = TRUE;
            res.active = FALSE;
            g->active = GIZMO_NONE;
            g->hover = GIZMO_NONE;
        } else {
            UpdateDrag(g, vp, in, &res);
            if (in->down) {
                res.active = TRUE;
            } else {
                res.active = FALSE;
                res.ended = TRUE;
                g->active = GIZMO_NONE;
            }
        }
        g->last = res;
        *out = res;
        return;
    }
    GizmoLayout lay = { 0 };
    GizmoComputeLayout(g, vp, frame, &lay);
    g->hover = GIZMO_NONE;
    memset(&res, 0, sizeof(GizmoResult));
    res.handle = GIZMO_NONE;
    glm_vec3_one(res.scale);
    glm_vec3_copy((vec3){ 0.0f, 0.0f, 1.0f }, res.rotation_axis);
    if (lay.valid && in->hover_enabled) {
        g->hover = Pick(g, &lay, in->mouse);
        if (g->hover != GIZMO_NONE && in->pressed && BeginDrag(g, vp, frame, &lay, g->hover, in->mouse)) {
            g->active = g->hover;
            res.began = TRUE;
            res.active = TRUE;
            res.handle = g->active;
            g->last = res;
        }
    }
    *out = res;
}

void GizmoDraw(const Gizmo* g, const ViewProjection* vp, const GizmoFrame* frame) {
    GizmoLayout lay = { 0 };
    GizmoComputeLayout(g, vp, frame, &lay);
    if (!lay.valid) return;
    Vector2 p = V2(lay.pivot2d);
    BOOL dragging = g->active != GIZMO_NONE;
    if (g->mode == GIZMO_ROTATE) {
        DrawCircleV(p, lay.ring_px, (Color){ 255, 255, 255, 14 });
        for (int i = 0; i < 3; i++) {
            BOOL hot = g->hover == (GizmoHandle)i || g->active == (GizmoHandle)i;
            if (dragging && !hot) continue;
            for (int s = 0; s < GIZMO_RING_SEGMENTS; s++) {
                if (lay.ring_front[i][s]) DrawLineEx(V2(lay.ring[i][s]), V2(lay.ring[i][s + 1]), hot ? 4.0f : 3.0f, hot ? MappedColor(HOT_COLOR) : AxisColor(i));
                else DrawLineEx(V2(lay.ring[i][s]), V2(lay.ring[i][s + 1]), 1.0f, WithAlpha(AxisColor(i), 0.18f));
            }
        }
        BOOL hot = g->hover == GIZMO_VIEW_RING || g->active == GIZMO_VIEW_RING;
        if (!dragging || hot) DrawRing(p, lay.view_ring_px - (hot ? 2.0f : 1.0f), lay.view_ring_px + (hot ? 2.0f : 1.0f), 0, 360, 72, hot ? MappedColor(HOT_COLOR) : (Color){ 200, 200, 200, 190 });
        DrawCircleV(p, 3.0f, WHITE);
        DrawReadout(g, &lay);
        return;
    }
    if (g->mode == GIZMO_TRANSLATE) {
        for (int i = 0; i < 3; i++) {
            if (!lay.plane_visible[i]) continue;
            GizmoHandle h = (GizmoHandle)(GIZMO_PLANE_YZ + i);
            BOOL hot = g->hover == h || g->active == h;
            if (dragging && !hot) continue;
            Color base = hot ? MappedColor(HOT_COLOR) : AxisColor(i);
            FillQuad((const vec2*)lay.quad[i], WithAlpha(base, hot ? 0.6f : 0.35f));
            for (int c = 0; c < 4; c++) DrawLineEx(V2(lay.quad[i][c]), V2(lay.quad[i][(c + 1) % 4]), 1.5f, WithAlpha(base, 0.9f));
        }
    }
    for (int i = 0; i < 3; i++) {
        if (!lay.axis_visible[i]) continue;
        BOOL hot = g->hover == (GizmoHandle)i || g->active == (GizmoHandle)i;
        if (dragging && !hot) continue;
        Color col = hot ? MappedColor(HOT_COLOR) : AxisColor(i);
        Vector2 tip = V2(lay.tip[i]);
        if (g->mode == GIZMO_TRANSLATE) {
            DrawLineEx(p, tip, hot ? 4.0f : 3.0f, col);
            DrawArrowHead(p, tip, col);
        } else {
            DrawLineEx(p, tip, hot ? 4.0f : 3.0f, col);
            DrawRectangleV((Vector2){ tip.x - 6, tip.y - 6 }, (Vector2){ 12, 12 }, col);
        }
    }
    BOOL hot = g->hover == GIZMO_CENTER || g->active == GIZMO_CENTER;
    Color center = hot ? MappedColor(HOT_COLOR) : (Color){ 235, 235, 235, 255 };
    if (g->mode == GIZMO_TRANSLATE) {
        DrawCircleV(p, 6.0f, center);
        DrawCircleLines((int)p.x, (int)p.y, 8.0f, WithAlpha(center, 0.6f));
    } else {
        DrawRectangleV((Vector2){ p.x - 7, p.y - 7 }, (Vector2){ 14, 14 }, center);
    }
    DrawReadout(g, &lay);
}

BOOL GizmoWantsMouse(const Gizmo* g) {
    return g->hover != GIZMO_NONE || g->active != GIZMO_NONE;
}

void GizmoReset(Gizmo* g) {
    g->active = GIZMO_NONE;
    g->hover = GIZMO_NONE;
    g->last.active = FALSE;
}

void NavWidgetUpdate(const ViewProjection* vp, const vec2 mouse, BOOL hover_enabled, BOOL pressed, BOOL down) {
    TickSnap();
    NavBall balls[6] = { 0 };
    vec2 c = { 0 };
    NavWidgetLayout(vp, vp->width, balls, c);
    g_hot = -1;
    g_over = hover_enabled && Dist2D(mouse, c) < NAV_RADIUS + NAV_BALL + 4.0f;
    if (g_over && !g_dragging) {
        for (int i = 0; i < 6; i++)
            if (Dist2D(mouse, balls[i].pos) < NAV_BALL) g_hot = i;
    }
    if (g_dragging && !down) g_dragging = FALSE;
    if (g_over && pressed && !g_dragging) {
        if (g_hot >= 0) NavWidgetSnap(balls[g_hot].axis, balls[g_hot].negative, TRUE);
        else g_dragging = TRUE;
    }
    if (g_dragging) {
        Vector2 d = GetMouseDelta();
        if (d.x != 0.0f || d.y != 0.0f) NavWidgetOrbit(d.x, d.y);
    }
}

BOOL NavWidgetWantsMouse() {
    return g_over || g_dragging;
}

void NavWidgetDraw(const ViewProjection* vp) {
    NavBall balls[6] = { 0 };
    vec2 c = { 0 };
    NavWidgetLayout(vp, vp->width, balls, c);
    Vector2 center = { c[0], c[1] };
    Font font = FontAsset();
    if (g_over || g_dragging) DrawCircleV(center, NAV_RADIUS + NAV_BALL + 4.0f, (Color){ 255, 255, 255, 28 });
    for (int i = 0; i < 6; i++) {
        const NavBall* b = &balls[i];
        Color col = AxisColor(b->axis);
        float shade = 0.8f + 0.2f * (b->toward + 1.0f) * 0.5f;
        Vector2 p = { b->pos[0], b->pos[1] };
        BOOL hot = i == g_hot;
        if (!b->negative) {
            DrawLineEx(center, p, 2.5f, (Color){ col.r, col.g, col.b, (unsigned char)(255 * shade) });
            DrawCircleV(p, NAV_BALL, (Color){ col.r, col.g, col.b, (unsigned char)(255 * shade) });
            if (hot) DrawRing(p, NAV_BALL - 1.0f, NAV_BALL + 1.0f, 0, 360, 24, WHITE);
            Vector2 ts = MeasureTextEx(font, NAV_NAMES[b->axis], 16, 0);
            DrawTextEx(font, NAV_NAMES[b->axis], (Vector2){ p.x - ts.x * 0.5f, p.y - ts.y * 0.5f }, 16, 0, (Color){ 20, 20, 20, 255 });
        } else {
            DrawCircleV(p, NAV_BALL, (Color){ col.r, col.g, col.b, hot ? 190 : 80 });
            DrawRing(p, NAV_BALL - 1.5f, NAV_BALL, 0, 360, 24, col);
            if (hot) {
                char label[3] = { '-', NAV_NAMES[b->axis][0], 0 };
                Vector2 ts = MeasureTextEx(font, label, 16, 0);
                DrawTextEx(font, label, (Vector2){ p.x - ts.x * 0.5f, p.y - ts.y * 0.5f }, 16, 0, (Color){ 20, 20, 20, 255 });
            }
        }
    }
}

void NavWidgetSnap(int axis, BOOL negative, BOOL flip_if_aligned) {
    SimpleCamera camera = RendererCamera()->core;
    float radius, theta, phi;
    CameraAngles(&camera, &radius, &theta, &phi);
    if (g_animating) {
        theta = g_target_theta;
        phi = g_target_phi;
    }
    float tt = theta, tp = GLM_PI_2f;
    BOOL keeps = FALSE;
    if (axis == 0) tt = negative ? GLM_PIf : 0.0f;
    else if (axis == 2) tt = negative ? -GLM_PI_2f : GLM_PI_2f;
    else { tp = negative ? GLM_PIf - POLE_EPSILON : POLE_EPSILON; keeps = TRUE; }

    BOOL aligned = fabsf(phi - tp) < 0.01f && (keeps || fabsf(WrapPi(theta - tt)) < 0.01f);
    if (aligned && flip_if_aligned) {
        NavWidgetSnap(axis, !negative, FALSE);
        return;
    }
    g_target_theta = tt;
    g_target_phi = tp;
    g_target_keeps_theta = keeps;
    if (keeps) g_target_theta = theta;
    g_animating = TRUE;
}

void NavWidgetCancelSnap() {
    g_animating = FALSE;
}

TargetCaps TargetQuery() {
    TargetCaps caps = { 0 };
    EditType type = 0;
    size_t index = 0;
    if (!GetEditTarget(&type, &index)) return caps;
    caps.type = type;
    caps.index = index;
    switch (type) {
        case EDIT_MESH:
            if (index >= NumMeshes() || MeshReference(index)->disabled) return caps;
            caps.can[GIZMO_TRANSLATE] = caps.can[GIZMO_ROTATE] = caps.can[GIZMO_SCALE] = TRUE;
            break;
        case EDIT_LIGHT: {
            if (index >= NumLights()) return caps;
            caps.can[GIZMO_TRANSLATE] = TRUE;
            caps.can[GIZMO_ROTATE] = glm_vec3_norm(LightReference(index)->direction) > 1e-6f;
            break;
        }
        case EDIT_CAMERA:
            if (index >= NumCameras() || index == PrimaryCameraID()) return caps;
            caps.can[GIZMO_TRANSLATE] = caps.can[GIZMO_ROTATE] = TRUE;
            break;
        case EDIT_SINGLE_VERTEX:
            if (index >= NumVertices()) return caps;
            caps.can[GIZMO_TRANSLATE] = TRUE;
            break;
        case EDIT_SINGLE_TRIANGLE: {
            VertexID ids[3];
            if (!TriangleVertices(index, ids)) return caps;
            caps.can[GIZMO_TRANSLATE] = caps.can[GIZMO_ROTATE] = caps.can[GIZMO_SCALE] = TRUE;
            break;
        }
        default:
            return caps;
    }
    caps.valid = TRUE;
    return caps;
}

BOOL TargetFrame(const TargetCaps* caps, GizmoMode mode, TransformSpace space, BOOL pivot_center, GizmoFrame* out) {
    if (!caps->valid || !caps->can[mode]) return FALSE;
    WorldAxes(out);
    BOOL local = space == TRANSFORM_SPACE_LOCAL;
    switch (caps->type) {
        case EDIT_MESH: {
            MeshDescriptor* md = MeshReference(caps->index);
            mat3 r;
            TargetMat3FromEuler(md->rotate, r);
            vec3 offset = { 0, 0, 0 };
            if (pivot_center) {
                for (int i = 0; i < 3; i++) offset[i] = md->center[i] * md->scale[i];
                glm_mat3_mulv(r, offset, offset);
            }
            vec3 pivot;
            glm_vec3_add(md->translate, offset, pivot);
            glm_vec3_copy(pivot, out->pivot);
            if (local || mode == GIZMO_SCALE) for (int i = 0; i < 3; i++) Mat3Axis(r, i, out->axes[i]);
            return TRUE;
        }
        case EDIT_LIGHT: {
            SceneLight* l = LightReference(caps->index);
            glm_vec3_copy(l->position, out->pivot);
            if (local && caps->can[GIZMO_ROTATE]) FrameFromDirection(l->direction, out);
            return TRUE;
        }
        case EDIT_CAMERA: {
            SceneCamera* c = GetCamera(caps->index);
            glm_vec3_copy(c->core.position, out->pivot);
            if (local) {
                vec3 u, v, w;
                CameraUVW(c->core, u, v, w);
                glm_vec3_copy(u, out->axes[0]);
                glm_vec3_copy(v, out->axes[1]);
                glm_vec3_copy(w, out->axes[2]);
            }
            return TRUE;
        }
        case EDIT_SINGLE_VERTEX:
            glm_vec3_copy(VertexReference(caps->index), out->pivot);
            return TRUE;
        case EDIT_SINGLE_TRIANGLE: {
            VertexID ids[3];
            if (!TriangleVertices(caps->index, ids)) return FALSE;
            float *a = VertexReference(ids[0]), *b = VertexReference(ids[1]), *c = VertexReference(ids[2]);
            for (int i = 0; i < 3; i++) out->pivot[i] = (a[i] + b[i] + c[i]) / 3.0f;
            if (local) {
                vec3 e1, e2, n;
                glm_vec3_sub(b, a, e1);
                glm_vec3_sub(c, a, e2);
                glm_vec3_cross(e1, e2, n);
                if (glm_vec3_norm(n) > 1e-12f && glm_vec3_norm(e1) > 1e-12f) {
                    glm_vec3_normalize(n);
                    glm_vec3_normalize(e1);
                    glm_vec3_cross(n, e1, e2);
                    glm_vec3_copy(e1, out->axes[0]);
                    glm_vec3_copy(e2, out->axes[1]);
                    glm_vec3_copy(n, out->axes[2]);
                }
            }
            return TRUE;
        }
        default:
            return FALSE;
    }
}

void TargetBegin(TargetSnapshot* snap, const TargetCaps* caps, const GizmoFrame* frame, BOOL pivot_center) {
    memset(snap, 0, sizeof(TargetSnapshot));
    snap->type = caps->type;
    snap->index = caps->index;
    memcpy(&snap->frame, frame, sizeof(GizmoFrame));
    switch (caps->type) {
        case EDIT_MESH: {
            MeshDescriptor* md = MeshReference(caps->index);
            glm_vec3_copy(md->translate, snap->translate);
            glm_vec3_copy(md->rotate, snap->euler);
            glm_vec3_copy(md->scale, snap->scale);
            TargetMat3FromEuler(md->rotate, snap->rotation);
            if (pivot_center) glm_vec3_copy(md->center, snap->pivot_object);
            break;
        }
        case EDIT_LIGHT:
            glm_vec3_copy(LightReference(caps->index)->position, snap->light_position);
            glm_vec3_copy(LightReference(caps->index)->direction, snap->light_direction);
            break;
        case EDIT_CAMERA:
            snap->camera = GetCamera(caps->index)->core;
            break;
        case EDIT_SINGLE_VERTEX:
            snap->count = 1;
            snap->ids[0] = (VertexID)caps->index;
            glm_vec3_copy(VertexReference(snap->ids[0]), snap->points[0]);
            break;
        case EDIT_SINGLE_TRIANGLE:
            if (TriangleVertices(caps->index, snap->ids)) {
                snap->count = 3;
                for (int i = 0; i < 3; i++) glm_vec3_copy(VertexReference(snap->ids[i]), snap->points[i]);
            }
            break;
        default: break;
    }
}

void TargetApply(const TargetSnapshot* snap, GizmoMode mode, const GizmoResult* r) {
    mat3 q = { 0 };
    TargetMat3AxisAngle(r->rotation_axis, r->angle, q);
    const float* pivot = snap->frame.pivot;
    switch (snap->type) {
        case EDIT_MESH: {
            MeshDescriptor* md = MeshReference(snap->index);
            if (mode == GIZMO_TRANSLATE) {
                glm_vec3_add((float*)snap->translate, (float*)r->translation, md->translate);
            } else {
                mat3 rot = { 0 };
                vec3 scale, euler;
                glm_vec3_copy((float*)snap->scale, scale);
                glm_mat3_copy((vec3*)snap->rotation, rot);
                glm_vec3_copy((float*)snap->euler, euler);
                if (mode == GIZMO_ROTATE && r->angle != 0.0f) {
                    glm_mat3_mul(q, (vec3*)snap->rotation, rot);
                    TargetEulerFromMat3(rot, snap->euler, euler);
                } else if (mode == GIZMO_SCALE) {
                    for (int i = 0; i < 3; i++) scale[i] *= r->scale[i];
                }
                vec3 p = { 0 };
                for (int i = 0; i < 3; i++) p[i] = scale[i] * snap->pivot_object[i];
                glm_mat3_mulv(rot, p, p);
                glm_vec3_sub((float*)pivot, p, md->translate);
                glm_vec3_copy(euler, md->rotate);
                glm_vec3_copy(scale, md->scale);
            }
            UpdateObjectTransform(snap->index);
            break;
        }
        case EDIT_LIGHT: {
            SceneLight* l = LightReference(snap->index);
            if (mode == GIZMO_TRANSLATE) glm_vec3_add((float*)snap->light_position, (float*)r->translation, l->position);
            else if (mode == GIZMO_ROTATE) glm_mat3_mulv(q, (float*)snap->light_direction, l->direction);
            UpdateLights();
            break;
        }
        case EDIT_CAMERA: {
            SceneCamera* c = GetCamera(snap->index);
            c->core = snap->camera;
            if (mode == GIZMO_TRANSLATE) {
                glm_vec3_add((float*)snap->camera.position, (float*)r->translation, c->core.position);
                glm_vec3_add((float*)snap->camera.look, (float*)r->translation, c->core.look);
            } else if (mode == GIZMO_ROTATE) {
                vec3 forward, up;
                glm_vec3_sub((float*)snap->camera.look, (float*)snap->camera.position, forward);
                glm_mat3_mulv(q, forward, forward);
                glm_vec3_add((float*)snap->camera.position, forward, c->core.look);
                glm_mat3_mulv(q, (float*)snap->camera.up, up);
                glm_vec3_copy(up, c->core.up);
            }
            break;
        }
        case EDIT_SINGLE_VERTEX:
        case EDIT_SINGLE_TRIANGLE: {
            vec3 points[3];
            mat3 scale_matrix, a;
            for (int i = 0; i < 3; i++) glm_vec3_copy((float*)snap->frame.axes[i], a[i]);
            glm_mat3_identity(scale_matrix);
            if (mode == GIZMO_SCALE) {
                mat3 d = GLM_MAT3_ZERO_INIT, ad, at;
                for (int i = 0; i < 3; i++) d[i][i] = r->scale[i];
                glm_mat3_mul(a, d, ad);
                glm_mat3_transpose_to(a, at);
                glm_mat3_mul(ad, at, scale_matrix);
            }
            for (int i = 0; i < snap->count; i++) {
                vec3 rel;
                glm_vec3_sub((float*)snap->points[i], (float*)pivot, rel);
                if (mode == GIZMO_ROTATE) glm_mat3_mulv(q, rel, rel);
                else if (mode == GIZMO_SCALE) glm_mat3_mulv(scale_matrix, rel, rel);
                else glm_vec3_add(rel, (float*)r->translation, rel);
                glm_vec3_add((float*)pivot, rel, points[i]);
            }
            WritePoints(snap, points);
            break;
        }
        default: break;
    }
}

void TargetRestore(const TargetSnapshot* snap) {
    switch (snap->type) {
        case EDIT_MESH: {
            MeshDescriptor* md = MeshReference(snap->index);
            glm_vec3_copy((float*)snap->translate, md->translate);
            glm_vec3_copy((float*)snap->euler, md->rotate);
            glm_vec3_copy((float*)snap->scale, md->scale);
            UpdateObjectTransform(snap->index);
            break;
        }
        case EDIT_LIGHT:
            glm_vec3_copy((float*)snap->light_position, LightReference(snap->index)->position);
            glm_vec3_copy((float*)snap->light_direction, LightReference(snap->index)->direction);
            UpdateLights();
            break;
        case EDIT_CAMERA:
            GetCamera(snap->index)->core = snap->camera;
            break;
        case EDIT_SINGLE_VERTEX:
        case EDIT_SINGLE_TRIANGLE:
            WritePoints(snap, (vec3*)snap->points);
            break;
        default: break;
    }
}
