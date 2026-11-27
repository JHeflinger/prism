#include "rigging.h"
#include "renderer/animation.h"
#include "renderer/renderer.h"
#include "renderer/rig.h"
#include "renderer/rmath.h"
#include "ui/shared.h"
#include "ui/panels/edit.h"
#include "data/defaults.h"
#include <ui/extra.h>
#include <data/colors.h>
#include <data/fonts.h>
#include <data/input.h>
#include <util/logger.h>
#include <core/binds.h>

#define BONE_ROW_HEIGHT 20.0f

static AutoWeightMethod g_method = AUTOWEIGHT_HEAT;
static float g_clean_threshold = 0.05f;
static uint32_t g_smooth_iterations = 4;
static float g_smooth_factor = 0.5f;
static BOOL g_smooth_selected = TRUE;
static BOOL g_hovered = FALSE;

static BOOL Button(const char* label, float w) {
    return UIButton(label, w) && g_hovered;
}

static size_t DropdownAutoWeight(void* data, size_t index, BOOL cancel) {
    if (index != (size_t)-1 && !cancel && index < AUTOWEIGHT_COUNT) g_method = (AutoWeightMethod)index;
    return g_method;
}

static size_t DropdownBrushMode(void* data, size_t index, BOOL cancel) {
    if (index != (size_t)-1 && !cancel && index < BRUSH_COUNT) RigBrush()->mode = (BrushMode)index;
    return RigBrush()->mode;
}

static size_t DropdownEditHandle(void* data, size_t index, BOOL cancel) {
    if (index != (size_t)-1 && !cancel && index < EDIT_HANDLE_COUNT) RigSetEditHandle((EditHandle)index);
    return RigEditHandle();
}

static void SelectBoneFromUI(size_t bone, BOOL extend) {
    if (extend && RigBoneSelected(bone)) RigDeselectBone(bone);
    else RigSelectBone(bone, extend);
    if (RigActiveBone() != (size_t)-1) SetEditBone(RigActiveBone());
}

static void DrawModeRow(float width) {
    const char** labels = RigModeLabels();
    float w = (width - 20.0f - (RIG_MODE_COUNT - 1) * 4.0f) / (float)RIG_MODE_COUNT;
    float y = UIGetCursor().y;
    for (int m = 0; m < RIG_MODE_COUNT; m++) {
        UISetCursor(10.0f + m * (w + 4.0f), y);
        if (AnimToggleButton(labels[m], w, RigViewportActive() && RigGetMode() == (RigMode)m) && g_hovered) RigSetMode((RigMode)m);
    }
    UISetCursor(10, y + LINE_HEIGHT + 4);
}

static void DrawBoneTree(MeshAnimation* anim, float width) {
    Skeleton* sk = &(anim->skeleton);
    size_t stack[MAX_BONES];
    int depth[MAX_BONES];
    BOOL visited[MAX_BONES] = { 0 };
    size_t top = 0;
    for (size_t b = sk->bonecount; b-- > 0;) if (sk->bones[b].parent >= sk->bonecount) { stack[top] = b; depth[top] = 0; top++; }
    Vector2 mouse = AnimPanelMouse();
    BOOL hovered = AnimPanelHovered("Rig") && !InputsBlocked();
    BOOL shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    Font font = FontAsset();
    size_t row = 0;
    while (top > 0) {
        top--;
        size_t b = stack[top];
        int d = depth[top];
        if (visited[b]) continue;
        visited[b] = TRUE;
        Vector2 c = UIGetCursor();
        Rectangle rect = { c.x, c.y, width - 20.0f, BONE_ROW_HEIGHT };
        BOOL selected = RigBoneSelected(b);
        BOOL active = RigActiveBone() == b;
        Color bg = (row % 2) ? (Color){ 30, 30, 36, 255 } : (Color){ 35, 35, 42, 255 };
        if (selected) bg = (Color){ 32, 70, 58, 255 };
        if (active) bg = (Color){ 36, 96, 76, 255 };
        BOOL over = hovered && CheckCollisionPointRec(mouse, rect);
        if (over && !selected) bg = (Color){ 45, 45, 55, 255 };
        DrawRectangleRec(rect, bg);
        float x = c.x + 6.0f + d * 12.0f;
        if (d > 0) DrawLine(x - 7, c.y + BONE_ROW_HEIGHT / 2, x - 2, c.y + BONE_ROW_HEIGHT / 2, (Color){ 110, 110, 120, 255 });
        Bone* bone = &(sk->bones[b]);
        char tags[64] = { 0 };
        if (!(bone->flags & BONE_FLAG_DEFORM)) strcat(tags, " ctrl");
        for (uint32_t i = 0; i < anim->numik; i++) if (anim->ik[i].bone == b) { strcat(tags, " IK"); break; }
        for (uint32_t i = 0; i < anim->numik; i++) if (anim->ik[i].target == b) { strcat(tags, " target"); break; }
        if (bone->flags & BONE_FLAG_HIDDEN) strcat(tags, " hidden");
        float tagw = tags[0] ? MeasureTextEx(font, tags, 14, 0).x + 6 : 0.0f;
        Color tc = active ? (Color){ 150, 255, 225, 255 } : (selected ? (Color){ 150, 240, 210, 255 } : MappedColor(UI_TEXT_COLOR));
        AnimDrawTextClipped(bone->name, x, c.y + 1, 17, rect.width - (x - c.x) - tagw - 14, tc);
        if (tags[0]) DrawTextEx(font, tags, (Vector2){ rect.x + rect.width - tagw - 12, c.y + 3 }, 14, 0, (Color){ 140, 170, 210, 255 });
        if (BoneIsPosed(anim, b)) DrawCircle(rect.x + rect.width - 6, c.y + BONE_ROW_HEIGHT / 2, 3, (Color){ 90, 200, 255, 255 });
        if (over && InputButtonPressed(IK_MOUSELEFT)) SelectBoneFromUI(b, shift);
        UISetCursor(c.x, c.y + BONE_ROW_HEIGHT);
        row++;
        for (size_t ch = sk->bonecount; ch-- > 0;)
            if (sk->bones[ch].parent == b && ch != b && top < MAX_BONES) { stack[top] = ch; depth[top] = d + 1; top++; }
    }
}

static void HalfButtons(float width, const char* a, const char* b, BOOL* pa, BOOL* pb) {
    float half = (width - 30.0f) / 2.0f;
    Vector2 c = UIGetCursor();
    *pa = Button(a, half);
    UISetCursor(c.x + half + 10.0f, c.y);
    *pb = Button(b, half);
}

static void DrawRigPanel(float width, float height) {
    g_hovered = AnimPanelHovered("Rig") && !InputsBlocked();
    float sbox = width - 20 - LEFT_COLUMN_WIDTH;
    size_t meshid = RigActiveMesh();
    MeshAnimation* anim = RigActive();
    if (meshid == (size_t)-1 || meshid >= NumMeshes()) {
        UISetCursor((width - UITextWidth("No Selected Object"))/2.0f, height / 2.0f - 20);
        UIDrawText("No Selected Object");
        return;
    }
    UIDrawText("Object: %s", *MeshNameReference(meshid));
    if (!anim) {
        UIMoveCursor(0, 5);
        if (Button("Create Rig", width - 20) && RigCreateForMesh(meshid)) RigSetMode(RIG_MODE_EDIT);
        return;
    }
    UIDrawSubtleText("%zu bones  |  %zu clips  |  %u IK", anim->skeleton.bonecount, anim->animations.size, anim->numik);
    if (!RigSkinsValid(RigActiveAnimation())) UIDrawSubtleText("warning: skin data does not match the vertices (was the mesh edited?)");
    UIMoveCursor(0, 5);
    DrawModeRow(width);
    if (!RigViewportActive()) UIDrawSubtleText("bones show in the animation select mode (Shift+A)");
    UIColumnHeader("Enable Rig", LEFT_COLUMN_WIDTH);
    UICheckbox(&(anim->enable));
    UIColumnHeader("Follow Timeline", LEFT_COLUMN_WIDTH);
    if (UICheckbox(&(anim->synced))) anim->dirty = TRUE;

    // bones
    UIMoveCursor(0, 8);
    UIDividerLabeled(width, "Bones");
    UIMoveCursor(0, 5);
    DrawBoneTree(anim, width);
    UIMoveCursor(0, 5);
    size_t active = RigActiveBone();
    BOOL a = FALSE, b = FALSE;
    RigMode mode = RigGetMode();
    if (mode == RIG_MODE_EDIT) {
        HalfButtons(width, "Add Bone", "Extrude Child", &a, &b);
        if (a) {
            vec3 head, tail;
            if (active != (size_t)-1) {
                RigRestHeadTail(active, head, tail);
                vec3 off = { 0.0f, 0.0f, 0.0f };
                off[0] = glm_vec3_distance(head, tail) * 0.5f;
                glm_vec3_add(head, off, head);
                glm_vec3_add(tail, off, tail);
            } else {
                MeshDescriptor* md = MeshReference(meshid);
                SETVEC3(head, md->center[0], md->center[1], md->center[2]);
                SETVEC3(tail, md->center[0], md->center[1] + fmaxf(md->extents[1] * 0.3f, 0.05f), md->center[2]);
            }
            size_t made = RigAddBone(active != (size_t)-1 ? anim->skeleton.bones[active].parent : (size_t)-1, head, tail, "Bone");
            if (made != (size_t)-1) SelectBoneFromUI(made, FALSE);
        }
        if (b && active != (size_t)-1) {
            size_t made = RigExtrudeBone(active);
            if (made != (size_t)-1) SelectBoneFromUI(made, FALSE);
        }
        HalfButtons(width, "Delete Bone", "Select All", &a, &b);
        if (a && active != (size_t)-1) RigDeleteBone(active);
        if (b) RigSelectAll();
        HalfButtons(width, "Symmetrize +X > -X", "Symmetrize -X > +X", &a, &b);
        if (a) loginfo("Symmetrized %zu bones", RigSymmetrize(MIRROR_POSITIVE_TO_NEGATIVE));
        if (b) loginfo("Symmetrized %zu bones", RigSymmetrize(MIRROR_NEGATIVE_TO_POSITIVE));
        UIColumnHeader("Gizmo Moves", LEFT_COLUMN_WIDTH);
        UIDropdownMenu(sbox, EDIT_HANDLE_COUNT, (char**)RigEditHandleLabels(), DropdownEditHandle, NULL);
        UIDrawSubtleText("bones are named .L/.R (or Left/Right) to be mirrored");
    } else if (mode == RIG_MODE_POSE) {
        HalfButtons(width, "Key Selected (K)", "Select All", &a, &b);
        if (a) RigKeySelected(KEY_CHANNEL_ALL);
        if (b) RigSelectAll();
        HalfButtons(width, "Clear Rotation", "Clear Location", &a, &b);
        if (a) RigResetPose(RigSelectedCount() > 0, KEY_CHANNEL_ROTATION);
        if (b) RigResetPose(RigSelectedCount() > 0, KEY_CHANNEL_LOCATION);
        HalfButtons(width, "Clear Scale", "Clear All", &a, &b);
        if (a) RigResetPose(RigSelectedCount() > 0, KEY_CHANNEL_SCALE);
        if (b) RigResetPose(RigSelectedCount() > 0, KEY_CHANNEL_ALL);
        HalfButtons(width, "Copy Pose", "Paste Pose", &a, &b);
        if (a) RigCopyPose();
        if (b) RigPastePose(FALSE);
        HalfButtons(width, "Paste Mirrored", "Revert To Keys", &a, &b);
        if (a) RigPastePose(TRUE);
        if (b) {
            for (size_t i = 0; i < anim->skeleton.bonecount; i++)
                if (RigSelectedCount() == 0 || RigBoneSelected(i)) BoneClearOverride(anim, i);
        }
    }

    // weights
    UIMoveCursor(0, 8);
    UIDividerLabeled(width, "Weights");
    UIMoveCursor(0, 5);
    if (RigAutoWeightsRunning()) {
        char status[64];
        snprintf(status, sizeof(status), "%.0f%%", RigAutoWeightsProgress());
        UIProgressBar(width - 20, RigAutoWeightsProgress(), status);
        UIMoveCursor(0, 5);
        if (Button("Cancel", width - 20)) RigAutoWeightsCancel();
    } else {
        UIColumnHeader("Method", LEFT_COLUMN_WIDTH);
        UIDropdownMenu(sbox, AUTOWEIGHT_COUNT, (char**)AutoWeightLabels(), DropdownAutoWeight, NULL);
        HalfButtons(width, "Auto Weights", "Selected Bones", &a, &b);
        if (a) RigAutoWeightsBegin(g_method, FALSE);
        if (b) RigAutoWeightsBegin(g_method, TRUE);
        HalfButtons(width, "Normalize", "Clean", &a, &b);
        if (a) RigNormalizeWeights();
        if (b) RigCleanWeights(g_clean_threshold);
        UIColumnHeader("Clean Below", LEFT_COLUMN_WIDTH);
        UIDragFloat(&g_clean_threshold, 0.0f, 0.5f, 0.005f, sbox);
        UIColumnHeader("Smooth Passes", LEFT_COLUMN_WIDTH);
        UIDragUInt(&g_smooth_iterations, 1, 100, 1, sbox);
        UIColumnHeader("Smooth Factor", LEFT_COLUMN_WIDTH);
        UIDragFloat(&g_smooth_factor, 0.0f, 1.0f, 0.01f, sbox);
        UIColumnHeader("Selected Only", LEFT_COLUMN_WIDTH);
        UICheckbox(&g_smooth_selected);
        if (Button("Smooth Weights", width - 20)) RigSmoothWeights(g_smooth_selected, g_smooth_iterations, g_smooth_factor);
        HalfButtons(width, "Mirror +X > -X", "Mirror -X > +X", &a, &b);
        if (a) RigMirrorWeights(MIRROR_POSITIVE_TO_NEGATIVE);
        if (b) RigMirrorWeights(MIRROR_NEGATIVE_TO_POSITIVE);
    }
    if (mode == RIG_MODE_WEIGHTS) {
        WeightBrush* brush = RigBrush();
        UIMoveCursor(0, 8);
        UIDividerLabeled(width, "Weight Brush");
        UIMoveCursor(0, 5);
        UIColumnHeader("Mode", LEFT_COLUMN_WIDTH);
        UIDropdownMenu(sbox, BRUSH_COUNT, (char**)BrushModeLabels(), DropdownBrushMode, NULL);
        UIColumnHeader("Radius", LEFT_COLUMN_WIDTH);
        UIDragFloat(&(brush->radius), 0.0001f, FLT_MAX, 0.005f, sbox);
        UIColumnHeader("Strength", LEFT_COLUMN_WIDTH);
        UIDragFloat(&(brush->strength), 0.0f, 1.0f, 0.01f, sbox);
        if (brush->mode == BRUSH_REPLACE) {
            UIColumnHeader("Weight", LEFT_COLUMN_WIDTH);
            UIDragFloat(&(brush->weight), 0.0f, 1.0f, 0.01f, sbox);
        }
        UIColumnHeader("Front Faces Only", LEFT_COLUMN_WIDTH);
        UICheckbox(&(brush->frontonly));
        UIDrawSubtleText(active == (size_t)-1 ? "select a bone to paint its weights" : "LMB paint, Ctrl inverts, [ ] radius, Alt+click picks a bone");
    }

    // ik overview
    if (anim->numik > 0) {
        UIMoveCursor(0, 8);
        UIDividerLabeled(width, "IK Constraints");
        UIMoveCursor(0, 5);
        for (uint32_t i = 0; i < anim->numik; i++) {
            IKConstraint* ik = &(anim->ik[i]);
            Vector2 c = UIGetCursor();
            BOOL enabled = ik->enabled != 0;
            if (UICheckbox(&enabled)) { ik->enabled = enabled ? 1 : 0; anim->dirty = TRUE; }
            UISetCursor(c.x + 26, c.y);
            const char* tip = ik->bone < anim->skeleton.bonecount ? anim->skeleton.bones[ik->bone].name : "?";
            const char* tgt = ik->target < anim->skeleton.bonecount ? anim->skeleton.bones[ik->target].name : "(no target)";
            char label[600];
            snprintf(label, sizeof(label), "%s -> %s", tip, tgt);
            AnimDrawTextClipped(label, c.x + 26, c.y, LINE_HEIGHT, width - 20 - 26 - 70, MappedColor(UI_TEXT_COLOR));
            UISetCursor(width - 10 - 60, c.y);
            if (Button("Edit", 60) && ik->bone < anim->skeleton.bonecount) SelectBoneFromUI(ik->bone, FALSE);
        }
    }

    UIMoveCursor(0, 15);
    if (Button("Delete Rig", width - 20)) {
        DeselectEditTarget();
        RigRemove(meshid);
    }
}

static BOOL GizmoSwitchCondition() {
    return RigGetMode() == RIG_MODE_EDIT;
}

static void GizmoSwitchWhole() {
    RigSetEditHandle(EDIT_HANDLE_BONE);
}

static void GizmoSwitchHead() {
    RigSetEditHandle(EDIT_HANDLE_HEAD);
}

static void GizmoSwitchTail() {
    RigSetEditHandle(EDIT_HANDLE_TAIL);
}

void UpdateRigging() {
    EditType type;
    size_t index;
    if (GetEditTarget(&type, &index) && type == EDIT_MESH && index < NumMeshes()) RigSetActiveMesh(index);
    RigUpdate();
    if (GetEditTarget(&type, &index) && type == EDIT_BONE) {
        MeshAnimation* anim = RigActive();
        if (!anim || index >= anim->skeleton.bonecount) DeselectEditTarget();
    }
    if (RigAutoWeightsRunning()) RigAutoWeightsTick(0.012);
    if (!InputsBlocked() && IsKeyPressed(KEY_SPACE) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) TimelineTogglePlay();
}

Panel GenerateRigPanel() {
    Panel p = { 0 };
    SetupPanel(&p, "Rig");
    p.draw = DrawRigPanel;
    p.scrollable = TRUE;
    AddConditionalBind("switch gizmo to whole bone", GizmoSwitchCondition, GizmoSwitchWhole, (BindCommand){ IK_LEFT_COMMAND, BIND_KEY_DOWN }, (BindCommand){ IK_TOGGLE_GIZMOS, BIND_KEY_DOWN }, (BindCommand){ IK_GIZMO_WHOLE, BIND_KEY_PRESSED });
    AddConditionalBind("switch gizmo to bone head", GizmoSwitchCondition, GizmoSwitchHead, (BindCommand){ IK_LEFT_COMMAND, BIND_KEY_DOWN }, (BindCommand){ IK_TOGGLE_GIZMOS, BIND_KEY_DOWN }, (BindCommand){ IK_GIZMO_HEAD, BIND_KEY_PRESSED });
    AddConditionalBind("switch gizmo to bone tail", GizmoSwitchCondition, GizmoSwitchTail, (BindCommand){ IK_LEFT_COMMAND, BIND_KEY_DOWN }, (BindCommand){ IK_TOGGLE_GIZMOS, BIND_KEY_DOWN }, (BindCommand){ IK_GIZMO_TAIL, BIND_KEY_PRESSED });
    return p;
}
