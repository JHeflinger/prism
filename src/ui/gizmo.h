#ifndef GIZMO_H
#define GIZMO_H

#include "renderer/rstructs.h"
#include "ui/panels/edit.h"

#define GIZMO_RING_SEGMENTS 64
#define GIZMO_DEFAULT_SIZE 96.0f
#define GIZMO_DEFAULT_SNAP_TRANSLATE 0.25f
#define GIZMO_DEFAULT_SNAP_ROTATE 15.0f
#define GIZMO_DEFAULT_SNAP_SCALE 0.1f

typedef enum {
    GIZMO_TRANSLATE = 0,
    GIZMO_ROTATE = 1,
    GIZMO_SCALE = 2,
} GizmoMode;

typedef enum {
    GIZMO_NONE = -1,
    GIZMO_AXIS_X = 0,
    GIZMO_AXIS_Y = 1,
    GIZMO_AXIS_Z = 2,
    GIZMO_PLANE_YZ = 3,
    GIZMO_PLANE_XZ = 4,
    GIZMO_PLANE_XY = 5,
    GIZMO_CENTER = 6,
    GIZMO_VIEW_RING = 7
} GizmoHandle;

typedef struct {
    vec3 pivot;
    vec3 axes[3];
} GizmoFrame;

typedef struct {
    vec2 mouse;
    BOOL hover_enabled;
    BOOL pressed;
    BOOL down;
    BOOL cancel;
    BOOL snap;
} GizmoInput;

typedef struct {
    BOOL began;
    BOOL active;
    BOOL ended;
    BOOL cancelled;
    GizmoHandle handle;
    vec3 translation;
    vec3 rotation_axis;
    float angle;
    vec3 scale;
} GizmoResult;

typedef struct {
    GizmoMode mode;
    float size;
    float snap_translate;
    float snap_rotate;
    float snap_scale;
    GizmoHandle hover;
    GizmoHandle active;
    GizmoResult last;
    GizmoFrame start_frame;
    vec2 start_mouse;
    float start_param;
    float start_length;
    vec3 start_hit;
    vec3 start_dir;
    float start_angle2d;
    float angle_prev_raw;
    float angle_accum;
    BOOL plane_rotation;
    vec2 tangent2d;
    float ring_px;
    vec2 pivot2d;
} Gizmo;

typedef struct {
    BOOL valid;
    vec2 pivot2d;
    float depth;
    float world_per_px;
    float length;
    float sign[3];
    vec3 dir[3];
    vec2 tip[3];
    BOOL axis_visible[3];
    vec2 quad[3][4];
    BOOL plane_visible[3];
    vec2 ring[3][GIZMO_RING_SEGMENTS + 1];
    BOOL ring_front[3][GIZMO_RING_SEGMENTS];
    float ring_px;
    float view_ring_px;
} GizmoLayout;

typedef struct {
    int axis;
    BOOL negative;
    vec2 pos;
    float toward;
} NavBall;

typedef enum {
    TRANSFORM_SPACE_WORLD = 0,
    TRANSFORM_SPACE_LOCAL = 1,
} TransformSpace;

typedef struct {
    BOOL valid;
    EditType type;
    size_t index;
    BOOL can[3];
} TargetCaps;

typedef struct {
    EditType type;
    size_t index;
    GizmoFrame frame;
    vec3 translate;
    vec3 euler;
    vec3 scale;
    mat3 rotation;
    vec3 pivot_object;
    vec3 light_position;
    vec3 light_direction;
    SimpleCamera camera;
    int count;
    VertexID ids[3];
    vec3 points[3];
} TargetSnapshot;

void InitializeGizmo(Gizmo* g);

void GizmoUpdate(Gizmo* g, const ViewProjection* vp, const GizmoFrame* frame, const GizmoInput* in, GizmoResult* out);

void GizmoDraw(const Gizmo* g, const ViewProjection* vp, const GizmoFrame* frame);

BOOL GizmoWantsMouse(const Gizmo* g);

void GizmoReset(Gizmo* g);

void NavWidgetUpdate(const ViewProjection* vp, const vec2 mouse, BOOL hover_enabled, BOOL pressed, BOOL down);

BOOL NavWidgetWantsMouse();

void NavWidgetDraw(const ViewProjection* vp);

void NavWidgetSnap(int axis, BOOL negative, BOOL flip_if_aligned);

void NavWidgetCancelSnap();

TargetCaps TargetQuery();

BOOL TargetFrame(const TargetCaps* caps, GizmoMode mode, TransformSpace space, BOOL pivot_center, GizmoFrame* out);

void TargetBegin(TargetSnapshot* snap, const TargetCaps* caps, const GizmoFrame* frame, BOOL pivot_center);

void TargetApply(const TargetSnapshot* snap, GizmoMode mode, const GizmoResult* r);

void TargetRestore(const TargetSnapshot* snap);

#endif
