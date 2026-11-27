#include "timeline.h"
#include "renderer/animation.h"
#include "renderer/renderer.h"
#include "renderer/rig.h"
#include "ui/panels/edit.h"
#include "ui/shared.h"
#include <ui/extra.h>
#include <data/colors.h>
#include <data/fonts.h>
#include <data/input.h>
#include <util/logger.h>

#define TL_NAME_WIDTH 180.0f
#define TL_RULER_HEIGHT 22.0f
#define TL_ROW_HEIGHT 18.0f
#define TL_FONT 15.0f
#define TL_KEY_RADIUS 5.0f
#define TL_MAX_SELECTED 131072
#define SUMMARY_ROW ((size_t)-2)

typedef enum {
    DRAG_NONE = 0,
    DRAG_SCRUB,
    DRAG_KEYS,
    DRAG_BOX,
    DRAG_PAN,
} TimelineDrag;

typedef struct {
    size_t bone;
    float ticks;
} KeyRef;

typedef struct {
    float ticks;
    float frame;
    uint8_t interp;
} CachedKey;

typedef struct {
    const void* anim;
    size_t clipindex;
    const void* clip;
    uint64_t revision;
    size_t bones;
    float fps;
    BOOL valid;
} CacheKey;

static KeyRef g_sel[TL_MAX_SELECTED];
static size_t g_nsel = 0;
static size_t g_sel_anim = (size_t)-1;
static size_t g_sel_clip = (size_t)-1;
static float g_view_start = 0.0f;
static float g_view_end = 0.0f;
static BOOL g_view_init = FALSE;
static float g_scroll = 0.0f;
static TimelineDrag g_drag = DRAG_NONE;
static float g_drag_origin = 0.0f;
static float g_drag_delta = 0.0f;
static Vector2 g_box_start = { 0 };
static Vector2 g_pan_last = { 0 };
static BOOL g_only_keyed = FALSE;
static char g_clip_name[256] = { 0 };
static size_t g_clip_name_anim = (size_t)-1;
static size_t g_clip_name_clip = (size_t)-1;
static size_t g_rows[MAX_BONES + 1];
static int g_row_depth[MAX_BONES + 1];
static size_t g_nrows = 0;
static CachedKey* g_keys = NULL;
static size_t g_keys_cap = 0;
static size_t g_key_start[MAX_BONES + 1];
static size_t g_key_count[MAX_BONES + 1];
static CacheKey g_cache = { 0 };

static int CompareFloatAscImpl(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static int CompareKeyRef(size_t bone, float ticks, const KeyRef* r) {
    if (bone != r->bone) return bone < r->bone ? -1 : 1;
    if (fabsf(ticks - r->ticks) < KEY_TIME_EPSILON) return 0;
    return ticks < r->ticks ? -1 : 1;
}

static size_t SelectionLowerBound(size_t bone, float ticks) {
    size_t lo = 0, hi = g_nsel;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (CompareKeyRef(bone, ticks, &g_sel[mid]) > 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static BOOL KeySelected(size_t bone, float ticks) {
    if (g_nsel == 0) return FALSE;
    size_t i = SelectionLowerBound(bone, ticks);
    return i < g_nsel && CompareKeyRef(bone, ticks, &g_sel[i]) == 0;
}

static void KeySelect(size_t bone, float ticks, BOOL on) {
    size_t i = SelectionLowerBound(bone, ticks);
    BOOL found = i < g_nsel && CompareKeyRef(bone, ticks, &g_sel[i]) == 0;
    if (on && !found && g_nsel < TL_MAX_SELECTED) {
        memmove(&g_sel[i + 1], &g_sel[i], (g_nsel - i) * sizeof(KeyRef));
        g_sel[i] = (KeyRef){ bone, ticks };
        g_nsel++;
    } else if (!on && found) {
        memmove(&g_sel[i], &g_sel[i + 1], (g_nsel - i - 1) * sizeof(KeyRef));
        g_nsel--;
    }
}

static int CompareKeyRefSort(const void* a, const void* b) {
    const KeyRef* x = (const KeyRef*)a;
    const KeyRef* y = (const KeyRef*)b;
    if (x->bone != y->bone) return x->bone < y->bone ? -1 : 1;
    return x->ticks < y->ticks ? -1 : (x->ticks > y->ticks ? 1 : 0);
}

static void KeyAppend(size_t bone, float ticks) {
    if (g_nsel < TL_MAX_SELECTED) g_sel[g_nsel++] = (KeyRef){ bone, ticks };
}

static void SelectionNormalize() {
    qsort(g_sel, g_nsel, sizeof(KeyRef), CompareKeyRefSort);
    size_t out = 0;
    for (size_t i = 0; i < g_nsel; i++)
        if (out == 0 || CompareKeyRef(g_sel[i].bone, g_sel[i].ticks, &g_sel[out - 1]) != 0) g_sel[out++] = g_sel[i];
    g_nsel = out;
}

typedef struct {
    float ticks;
    uint8_t interp;
    uint8_t priority;
} RawKey;

static int CompareRawKey(const void* a, const void* b) {
    const RawKey* x = (const RawKey*)a;
    const RawKey* y = (const RawKey*)b;
    if (x->ticks != y->ticks) return x->ticks < y->ticks ? -1 : 1;
    return (int)x->priority - (int)y->priority;
}

static void ReserveKeys(size_t n) {
    if (n <= g_keys_cap) return;
    size_t cap = g_keys_cap ? g_keys_cap : 1024;
    while (cap < n) cap *= 2;
    g_keys = g_keys ? EZ_REALLOC(g_keys, cap, sizeof(CachedKey)) : EZ_ALLOC(cap, sizeof(CachedKey));
    g_keys_cap = cap;
}

static void ReleaseKeyCache() {
    if (g_keys) EZ_FREE(g_keys);
    g_keys = NULL;
    g_keys_cap = 0;
    g_cache.valid = FALSE;
}

static void RefreshKeyCache(MeshAnimation* anim) {
    Animation* clip = anim ? CurrentClip(anim) : NULL;
    CacheKey key = { anim, anim ? anim->current : 0, clip, KeyRevision(), anim ? anim->skeleton.bonecount : 0, Timeline()->fps, TRUE };
    if (g_cache.valid && memcmp(&key, &g_cache, sizeof(CacheKey)) == 0) return;
    g_cache = key;
    memset(g_key_start, 0, sizeof(g_key_start));
    memset(g_key_count, 0, sizeof(g_key_count));
    if (!clip) return;
    size_t nb = anim->skeleton.bonecount;
    size_t total = 0, rawmax = 0;
    for (size_t k = 0; k < clip->channels.size; k++) {
        BoneChannel* ch = &(clip->channels.data[k]);
        size_t n = ch->positions.size + ch->rotations.size + ch->scales.size;
        total += n;
        if (n > rawmax) rawmax = n;
    }
    ReserveKeys(total * 2 + 1);
    RawKey* raw = EZ_ALLOC(total + 1, sizeof(RawKey));
    size_t used = 0;
    for (size_t b = 0; b < nb; b++) {
        g_key_start[b] = used;
        BoneChannel* ch = ClipChannel(clip, anim->skeleton.bones[b].name, FALSE);
        if (!ch) continue;
        size_t n = 0;
        for (size_t i = 0; i < ch->rotations.size; i++) raw[n++] = (RawKey){ ch->rotations.data[i].time, (uint8_t)ch->rotations.data[i].interp, 0 };
        for (size_t i = 0; i < ch->positions.size; i++) raw[n++] = (RawKey){ ch->positions.data[i].time, (uint8_t)ch->positions.data[i].interp, 1 };
        for (size_t i = 0; i < ch->scales.size; i++) raw[n++] = (RawKey){ ch->scales.data[i].time, (uint8_t)ch->scales.data[i].interp, 2 };
        qsort(raw, n, sizeof(RawKey), CompareRawKey);
        for (size_t i = 0; i < n; i++) {
            if (g_key_count[b] > 0 && fabsf(raw[i].ticks - g_keys[used - 1].ticks) < KEY_TIME_EPSILON) continue;
            uint8_t interp = clip->mode == ANIMATION_AUTHORED ? raw[i].interp : KEY_INTERP_LINEAR;
            g_keys[used++] = (CachedKey){ raw[i].ticks, AnimationTicksToFrame(clip, raw[i].ticks), interp };
            g_key_count[b]++;
        }
    }
    size_t bonekeys = used;
    float* times = EZ_ALLOC(bonekeys + 1, sizeof(float));
    for (size_t i = 0; i < bonekeys; i++) times[i] = g_keys[i].ticks;
    qsort(times, bonekeys, sizeof(float), CompareFloatAscImpl);
    g_key_start[MAX_BONES] = used;
    for (size_t i = 0; i < bonekeys; i++) {
        if (g_key_count[MAX_BONES] > 0 && fabsf(times[i] - g_keys[used - 1].ticks) < KEY_TIME_EPSILON) continue;
        g_keys[used++] = (CachedKey){ times[i], AnimationTicksToFrame(clip, times[i]), KEY_INTERP_SMOOTH };
        g_key_count[MAX_BONES]++;
    }
    EZ_FREE(times);
    EZ_FREE(raw);
}

static size_t RowSlot(size_t bone) {
    return bone == SUMMARY_ROW ? MAX_BONES : bone;
}

static CachedKey* RowKeys(size_t bone, size_t* count) {
    size_t slot = RowSlot(bone);
    if (!g_cache.valid || (slot != MAX_BONES && slot >= g_cache.bones)) { *count = 0; return NULL; }
    *count = g_key_count[slot];
    return g_key_count[slot] ? &g_keys[g_key_start[slot]] : NULL;
}

static size_t KeyLowerBound(const CachedKey* keys, size_t n, float frame) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (keys[mid].frame < frame) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static const char* RowBoneName(MeshAnimation* anim, size_t bone) {
    return bone == SUMMARY_ROW ? NULL : anim->skeleton.bones[bone].name;
}

static void MakeEditableKeepingSelection(MeshAnimation* anim) {
    Animation* clip = CurrentClip(anim);
    if (!clip || clip->mode == ANIMATION_AUTHORED) return;
    float fps = Timeline()->fps;
    float tps = clip->tps > 0.0f ? clip->tps : fps;
    ClipMakeEditable(anim, anim->current);
    for (size_t i = 0; i < g_nsel; i++) g_sel[i].ticks = g_sel[i].ticks / tps * fps; // same mapping as the conversion
    SelectionNormalize();
}

static void DeleteSelectedKeys(MeshAnimation* anim) {
    Animation* clip = CurrentClip(anim);
    if (!clip || g_nsel == 0) return;
    MakeEditableKeepingSelection(anim);
    clip = CurrentClip(anim);
    for (size_t i = 0; i < g_nsel; i++) {
        if (g_sel[i].bone != SUMMARY_ROW && g_sel[i].bone >= anim->skeleton.bonecount) continue;
        KeyDelete(clip, RowBoneName(anim, g_sel[i].bone), g_sel[i].ticks);
    }
    loginfo("Deleted %zu key%s", g_nsel, g_nsel == 1 ? "" : "s");
    g_nsel = 0;
    anim->dirty = TRUE;
}

static int CompareKeyRefAsc(const void* a, const void* b) {
    float x = ((const KeyRef*)a)->ticks, y = ((const KeyRef*)b)->ticks;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static void MoveSelectedKeys(MeshAnimation* anim, float frames) {
    Animation* clip = CurrentClip(anim);
    if (!clip || g_nsel == 0 || fabsf(frames) < 0.5f) return;
    if (clip->mode != ANIMATION_AUTHORED) {
        MakeEditableKeepingSelection(anim);
        clip = CurrentClip(anim);
    }
    float delta = AnimationFrameToTicks(clip, frames);
    qsort(g_sel, g_nsel, sizeof(KeyRef), CompareKeyRefAsc);
    for (size_t k = 0; k < g_nsel; k++) {
        size_t i = delta > 0.0f ? g_nsel - 1 - k : k;
        if (g_sel[i].bone != SUMMARY_ROW && g_sel[i].bone >= anim->skeleton.bonecount) continue;
        KeyMove(clip, RowBoneName(anim, g_sel[i].bone), g_sel[i].ticks, g_sel[i].ticks + delta);
    }
    for (size_t i = 0; i < g_nsel; i++) g_sel[i].ticks += delta;
    SelectionNormalize();
    anim->dirty = TRUE;
}

static void SetSelectedInterpolation(MeshAnimation* anim, KeyInterpolation interp) {
    Animation* clip = CurrentClip(anim);
    TimelineSetDefaultInterpolation(interp);
    if (!clip || g_nsel == 0) return;
    if (clip->mode != ANIMATION_AUTHORED) return;
    for (size_t i = 0; i < g_nsel; i++) {
        if (g_sel[i].bone != SUMMARY_ROW && g_sel[i].bone >= anim->skeleton.bonecount) continue;
        KeySetInterpolation(clip, RowBoneName(anim, g_sel[i].bone), g_sel[i].ticks, interp);
    }
    anim->dirty = TRUE;
}

static size_t DropdownClip(void* data, size_t index, BOOL cancel) {
    MeshAnimation* anim = RigActive();
    if (!anim || anim->animations.size == 0) return 0;
    if (index != (size_t)-1 && !cancel && index < anim->animations.size) {
        ClipSelect(anim, index);
        g_nsel = 0;
    }
    return anim->current < anim->animations.size ? anim->current : 0;
}

static size_t DropdownInterp(void* data, size_t index, BOOL cancel) {
    MeshAnimation* anim = RigActive();
    if (index != (size_t)-1 && !cancel && index < KEY_INTERP_COUNT) {
        if (anim) SetSelectedInterpolation(anim, (KeyInterpolation)index);
        else TimelineSetDefaultInterpolation((KeyInterpolation)index);
        return index;
    }
    if (anim && g_nsel > 0) {
        Animation* clip = CurrentClip(anim);
        if (clip && (g_sel[0].bone == SUMMARY_ROW || g_sel[0].bone < anim->skeleton.bonecount))
            return KeyGetInterpolation(clip, RowBoneName(anim, g_sel[0].bone), g_sel[0].ticks);
    }
    return TimelineDefaultInterpolation();
}

static float FrameToX(float frame, float x0, float x1) {
    float span = g_view_end - g_view_start;
    if (span < 1e-3f) span = 1e-3f;
    return x0 + (frame - g_view_start) / span * (x1 - x0);
}

static float XToFrame(float x, float x0, float x1) {
    float span = g_view_end - g_view_start;
    if (x1 - x0 < 1.0f) return g_view_start;
    return g_view_start + (x - x0) / (x1 - x0) * span;
}

static void BuildRows(MeshAnimation* anim) {
    g_nrows = 0;
    g_rows[g_nrows] = SUMMARY_ROW;
    g_row_depth[g_nrows++] = 0;
    if (!anim) return;
    Skeleton* sk = &(anim->skeleton);
    Animation* clip = CurrentClip(anim);
    size_t stack[MAX_BONES];
    int depth[MAX_BONES];
    size_t top = 0;
    for (size_t b = sk->bonecount; b-- > 0;) {
        if (sk->bones[b].parent >= sk->bonecount) { stack[top] = b; depth[top] = 0; top++; }
    }
    BOOL visited[MAX_BONES] = { 0 };
    while (top > 0 && g_nrows < MAX_BONES + 1) {
        top--;
        size_t b = stack[top];
        int d = depth[top];
        if (visited[b]) continue;
        visited[b] = TRUE;
        BOOL keyed = clip && g_cache.valid && b < g_cache.bones && g_key_count[b] > 0;
        if (!g_only_keyed || keyed || RigBoneSelected(b)) {
            g_rows[g_nrows] = b;
            g_row_depth[g_nrows++] = d;
        }
        for (size_t c = sk->bonecount; c-- > 0;)
            if (sk->bones[c].parent == b && c != b && top < MAX_BONES) { stack[top] = c; depth[top] = d + 1; top++; }
    }
}

static float NiceStep(float pxperframe) {
    static const float steps[] = { 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000, 10000 };
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
        if (steps[i] * pxperframe >= 48.0f) return steps[i];
    return 20000.0f;
}

static void DrawDiamond(float x, float y, float r, Color fill, Color line) {
    float o = r + 1.0f;
    Vector2 t = { x, y - o }, b = { x, y + o }, l = { x - o, y }, rr = { x + o, y };
    DrawTriangle(t, l, rr, line);
    DrawTriangle(rr, l, b, line);
    t.y = y - r + 0.5f; b.y = y + r - 0.5f; l.x = x - r + 0.5f; rr.x = x + r - 0.5f;
    DrawTriangle(t, l, rr, fill);
    DrawTriangle(rr, l, b, fill);
}

static BOOL g_hovered = FALSE;

static BOOL Button(const char* label, float w) {
    return UIButton(label, w) && g_hovered;
}

static float DrawControls(MeshAnimation* anim, float width) {
    AnimationTimeline* tl = Timeline();
    g_hovered = AnimPanelHovered("Timeline") && !InputsBlocked();
    AnimFlowBegin(5.0f, width);
    AnimFlowNext(AnimButtonWidth("|<"));
    if (Button("|<", 0)) tl->frame = tl->start;
    AnimFlowNext(AnimButtonWidth("<<"));
    if (Button("<<", 0) && anim) { float f; if (KeyNeighbour(anim, tl->frame, -1, &f)) tl->frame = f; }
    AnimFlowNext(AnimButtonWidth("<"));
    if (Button("<", 0)) TimelineStep(-1.0f);
    AnimFlowNext(AnimButtonWidth("Pause"));
    if (AnimToggleButton(tl->playing ? "Pause" : "Play", AnimButtonWidth("Pause"), tl->playing) && g_hovered) TimelineTogglePlay();
    AnimFlowNext(AnimButtonWidth(">"));
    if (Button(">", 0)) TimelineStep(1.0f);
    AnimFlowNext(AnimButtonWidth(">>"));
    if (Button(">>", 0) && anim) { float f; if (KeyNeighbour(anim, tl->frame, 1, &f)) tl->frame = f; }
    AnimFlowNext(AnimButtonWidth(">|"));
    if (Button(">|", 0)) tl->frame = tl->end;
    float lw;
    AnimFlowNext(130);
    lw = AnimLabel("Frame");
    UIDragFloat(&(tl->frame), -100000.0f, 100000.0f, 0.25f, 130 - lw);
    AnimFlowNext(120);
    float start = tl->start, end = tl->end;
    lw = AnimLabel("Start");
    if (UIDragFloat(&start, -100000.0f, 100000.0f, 0.25f, 120 - lw)) tl->start = roundf(fminf(start, tl->end));
    AnimFlowNext(120);
    lw = AnimLabel("End");
    if (UIDragFloat(&end, -100000.0f, 100000.0f, 0.25f, 120 - lw)) tl->end = roundf(fmaxf(end, tl->start));
    AnimFlowNext(100);
    float fps = tl->fps;
    lw = AnimLabel("FPS");
    if (UIDragFloat(&fps, 1.0f, 240.0f, 0.25f, 100 - lw) && fps >= 1.0f) TimelineSetFPS(roundf(fps));
    AnimFlowNext(110);
    lw = AnimLabel("Speed");
    UIDragFloat(&(tl->speed), 0.01f, 16.0f, 0.01f, 110 - lw);
    AnimFlowNext(UITextWidth("Loop") + 25);
    UICheckboxLabeled("Loop", &(tl->looping));
    AnimFlowNext(UITextWidth("Auto Key") + 25);
    UICheckboxLabeled("Auto Key", &(tl->autokey));
    // clip row
    AnimFlowBegin(AnimFlowEnd(), width);
    if (!anim) {
        AnimFlowNext(UITextWidth("Select a rigged object to animate (Rig panel)"));
        UIDrawSubtleText("Select a rigged object to animate (Rig panel)");
        return AnimFlowEnd();
    }
    const char* objname = anim->meshid < NumMeshes() ? *MeshNameReference(anim->meshid) : "?";
    AnimFlowNext(fminf(UITextWidth("%s", objname), 160));
    AnimDrawTextClipped(objname, UIGetCursor().x, UIGetCursor().y, LINE_HEIGHT, 160, MappedColor(UI_TEXT_COLOR));
    if (anim->animations.size > 0) {
        AnimFlowNext(170);
        UIDropdownMenu(170, anim->animations.size, anim->names.data, DropdownClip, NULL);
    } else {
        AnimFlowNext(UITextWidth("(no clips)"));
        UIDrawSubtleText("(no clips)");
    }
    AnimFlowNext(AnimButtonWidth("New Clip"));
    if (Button("New Clip", 0)) { ClipCreate(anim, "Action"); anim->synced = TRUE; g_nsel = 0; }
    Animation* clip = CurrentClip(anim);
    if (clip) {
        AnimFlowNext(AnimButtonWidth("Duplicate"));
        if (Button("Duplicate", 0)) { ClipDuplicate(anim, anim->current); g_nsel = 0; }
        AnimFlowNext(AnimButtonWidth("Delete Clip"));
        if (Button("Delete Clip", 0)) { ClipDelete(anim, anim->current); g_nsel = 0; clip = CurrentClip(anim); }
    }
    if (clip) {
        size_t aidx = RigActiveAnimation();
        if (g_clip_name_anim != aidx || g_clip_name_clip != anim->current) {
            snprintf(g_clip_name, sizeof(g_clip_name), "%s", clip->name);
            g_clip_name_anim = aidx;
            g_clip_name_clip = anim->current;
        }
        AnimFlowNext(200);
        if (UITextInput("Rename", g_clip_name, sizeof(g_clip_name) - 1, 200, FALSE)) {
            ClipRename(anim, anim->current, g_clip_name);
            g_clip_name_clip = (size_t)-1;
        }
        if (clip->mode != ANIMATION_AUTHORED) {
            AnimFlowNext(AnimButtonWidth("Make Editable"));
            if (Button("Make Editable", 0)) { ClipMakeEditable(anim, anim->current); anim->synced = TRUE; g_nsel = 0; }
        }
    }
    AnimFlowNext(UITextWidth("Sync") + 25);
    if (UICheckboxLabeled("Sync", &(anim->synced))) anim->dirty = TRUE;
    AnimFlowNext(UITextWidth("Cycle") + 25);
    if (UICheckboxLabeled("Cycle", &(anim->looping))) anim->dirty = TRUE;
    AnimFlowNext(AnimButtonWidth("Key (K)"));
    if (Button("Key (K)", 0)) RigKeySelected(KEY_CHANNEL_ALL);
    AnimFlowNext(AnimButtonWidth("Delete Keys"));
    if (Button("Delete Keys", 0)) DeleteSelectedKeys(anim);
    AnimFlowNext(110);
    UIDropdownMenu(110, KEY_INTERP_COUNT, (char**)KeyInterpolationLabels(), DropdownInterp, NULL);
    AnimFlowNext(UITextWidth("Keyed only") + 25);
    UICheckboxLabeled("Keyed only", &g_only_keyed);
    return AnimFlowEnd();
}

static void DrawDopesheet(MeshAnimation* anim, float top, float width, float height) {
    AnimationTimeline* tl = Timeline();
    Font font = FontAsset();
    float left = 10.0f;
    float x0 = left + TL_NAME_WIDTH;
    float x1 = width - 12.0f;
    float bottom = height - 6.0f;
    if (bottom - top < TL_RULER_HEIGHT + TL_ROW_HEIGHT || x1 - x0 < 40.0f) return;
    if (!g_view_init || g_view_end <= g_view_start) {
        g_view_start = tl->start - 2.0f;
        g_view_end = tl->end + 2.0f;
        g_view_init = TRUE;
    }
    Animation* clip = anim ? CurrentClip(anim) : NULL;
    size_t aidx = RigActiveAnimation();
    if (aidx != g_sel_anim || (anim && anim->current != g_sel_clip)) {
        g_nsel = 0;
        g_sel_anim = aidx;
        g_sel_clip = anim ? anim->current : (size_t)-1;
    }
    RefreshKeyCache(anim);
    BuildRows(anim);
    float rowstop = top + TL_RULER_HEIGHT;
    size_t visible = (size_t)fmaxf(1.0f, floorf((bottom - rowstop) / TL_ROW_HEIGHT));
    float maxscroll = g_nrows > visible ? (float)(g_nrows - visible) : 0.0f;
    g_scroll = glm_clamp(g_scroll, 0.0f, maxscroll);
    size_t first = (size_t)g_scroll;
    DrawRectangle(left, top, x1 - left, bottom - top, (Color){ 22, 22, 26, 255 });
    DrawRectangle(x0, top, x1 - x0, TL_RULER_HEIGHT, (Color){ 34, 34, 40, 255 });
    float rs = FrameToX(tl->start, x0, x1), re = FrameToX(tl->end, x0, x1);
    float ra = fmaxf(rs, x0), rb = fminf(re, x1);
    if (rb > ra) DrawRectangle(ra, rowstop, rb - ra, bottom - rowstop, (Color){ 32, 34, 42, 255 });
    if (clip) {
        float cs = FrameToX(AnimationTicksToFrame(clip, 0.0f), x0, x1), ce = FrameToX(AnimationTicksToFrame(clip, clip->duration), x0, x1);
        float ca = fmaxf(cs, x0), cb = fminf(ce, x1);
        if (cb > ca) DrawRectangle(ca, top + TL_RULER_HEIGHT - 3, cb - ca, 3, (Color){ 90, 140, 220, 200 });
    }
    float ppf = (x1 - x0) / fmaxf(g_view_end - g_view_start, 1e-3f);
    float step = NiceStep(ppf);
    for (float f = floorf(g_view_start / step) * step; f <= g_view_end; f += step) {
        float x = FrameToX(f, x0, x1);
        if (x < x0 || x > x1) continue;
        DrawLine(x, rowstop, x, bottom, (Color){ 48, 48, 56, 255 });
        DrawLine(x, top + TL_RULER_HEIGHT - 7, x, top + TL_RULER_HEIGHT, (Color){ 150, 150, 160, 255 });
        char label[32];
        snprintf(label, sizeof(label), "%.0f", f);
        DrawTextEx(font, label, (Vector2){ x + 3, top + 3 }, 13, 0, (Color){ 170, 170, 180, 255 });
    }
    Vector2 mouse = AnimPanelMouse();
    for (size_t r = first; r < g_nrows && r < first + visible; r++) {
        float y = rowstop + (r - first) * TL_ROW_HEIGHT;
        size_t bone = g_rows[r];
        BOOL selected = bone != SUMMARY_ROW && RigBoneSelected(bone);
        Color rowbg = (r % 2) ? (Color){ 26, 26, 31, 255 } : (Color){ 29, 29, 35, 255 };
        if (selected) rowbg = (Color){ 30, 52, 46, 255 };
        DrawRectangle(left, y, TL_NAME_WIDTH, TL_ROW_HEIGHT, rowbg);
        DrawRectangle(x0, y, x1 - x0, TL_ROW_HEIGHT, (Color){ rowbg.r, rowbg.g, rowbg.b, 120 });
        const char* label = bone == SUMMARY_ROW ? "Summary" : anim->skeleton.bones[bone].name;
        float indent = 6.0f + g_row_depth[r] * 10.0f;
        Color tc = bone == SUMMARY_ROW ? (Color){ 120, 230, 200, 255 } : (selected ? (Color){ 140, 255, 220, 255 } : MappedColor(UI_TEXT_COLOR));
        AnimDrawTextClipped(label, left + indent, y + 1, TL_FONT, TL_NAME_WIDTH - indent - 4, tc);
        if (bone != SUMMARY_ROW && BoneIsPosed(anim, bone)) DrawCircle(left + TL_NAME_WIDTH - 7, y + TL_ROW_HEIGHT / 2, 3, (Color){ 90, 200, 255, 255 });
        if (!clip) continue;
        size_t n = 0;
        CachedKey* keys = RowKeys(bone, &n);
        if (!keys) continue;
        float margin = g_drag == DRAG_KEYS ? fabsf(roundf(g_drag_delta)) : 0.0f;
        float fmin = XToFrame(x0 - TL_KEY_RADIUS, x0, x1) - margin;
        float fmax = XToFrame(x1 + TL_KEY_RADIUS, x0, x1) + margin;
        float lastx = -FLT_MAX;
        for (size_t k = KeyLowerBound(keys, n, fmin); k < n && keys[k].frame <= fmax; k++) {
            float frame = keys[k].frame;
            BOOL sel = g_nsel > 0 && (KeySelected(bone, keys[k].ticks) || (bone != SUMMARY_ROW && KeySelected(SUMMARY_ROW, keys[k].ticks)));
            if (sel && g_drag == DRAG_KEYS) frame += roundf(g_drag_delta);
            float x = FrameToX(frame, x0, x1);
            if (x < x0 - TL_KEY_RADIUS || x > x1 + TL_KEY_RADIUS) continue;
            if (!sel && fabsf(x - lastx) < 2.0f) continue;
            lastx = x;
            KeyInterpolation interp = (KeyInterpolation)keys[k].interp;
            Color fill = sel ? (Color){ 255, 175, 60, 255 } : (interp == KEY_INTERP_STEP ? (Color){ 150, 150, 160, 255 } : (interp == KEY_INTERP_LINEAR ? (Color){ 200, 200, 210, 255 } : (Color){ 235, 235, 240, 255 }));
            float radius = bone == SUMMARY_ROW ? TL_KEY_RADIUS + 1.0f : TL_KEY_RADIUS;
            DrawDiamond(x, y + TL_ROW_HEIGHT / 2, radius, fill, (Color){ 20, 20, 20, 255 });
        }
    }
    float cx = FrameToX(tl->frame, x0, x1);
    if (cx >= x0 && cx <= x1) {
        DrawLineEx((Vector2){ cx, top }, (Vector2){ cx, bottom }, 2.0f, (Color){ 40, 255, 140, 255 });
        char label[32];
        snprintf(label, sizeof(label), "%.0f", tl->frame);
        float lw = MeasureTextEx(font, label, 13, 0).x + 8;
        DrawRectangle(cx - lw / 2, top + 1, lw, 16, (Color){ 40, 255, 140, 255 });
        DrawTextEx(font, label, (Vector2){ cx - lw / 2 + 4, top + 2 }, 13, 0, (Color){ 20, 20, 20, 255 });
    }
    if (g_drag == DRAG_BOX) {
        Rectangle box = { fminf(g_box_start.x, mouse.x), fminf(g_box_start.y, mouse.y), fabsf(mouse.x - g_box_start.x), fabsf(mouse.y - g_box_start.y) };
        DrawRectangleRec(box, (Color){ 60, 255, 175, 40 });
        DrawRectangleLinesEx(box, 1.0f, (Color){ 60, 255, 175, 200 });
    }
    DrawLine(x0, top, x0, bottom, (Color){ 60, 60, 70, 255 });
    if (maxscroll > 0.0f) {
        float track = bottom - rowstop;
        float bar = track * (float)visible / (float)g_nrows;
        float by = rowstop + (track - bar) * (g_scroll / maxscroll);
        DrawRectangle(x1 + 3, by, 5, bar, (Color){ 90, 90, 100, 255 });
    }

    BOOL hovered = AnimPanelHovered("Timeline") && !InputsBlocked();
    BOOL inside = mouse.x >= left && mouse.x <= x1 && mouse.y >= top && mouse.y <= bottom;
    if (hovered && inside) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
                float pivot = XToFrame(mouse.x, x0, x1);
                float scale = wheel > 0 ? 0.85f : 1.0f / 0.85f;
                g_view_start = pivot - (pivot - g_view_start) * scale;
                g_view_end = pivot + (g_view_end - pivot) * scale;
                if (g_view_end - g_view_start < 4.0f) g_view_end = g_view_start + 4.0f;
            } else if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                float shift = (g_view_end - g_view_start) * 0.1f * (wheel > 0 ? -1.0f : 1.0f);
                g_view_start += shift;
                g_view_end += shift;
            } else {
                g_scroll -= wheel * 3.0f;
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) { g_drag = DRAG_PAN; g_pan_last = mouse; }
        if (InputButtonPressed(IK_MOUSELEFT) && g_drag == DRAG_NONE) {
            BOOL shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (mouse.y < rowstop && mouse.x >= x0) {
                g_drag = DRAG_SCRUB;
            } else if (mouse.x < x0 && mouse.y >= rowstop) {
                size_t r = first + (size_t)((mouse.y - rowstop) / TL_ROW_HEIGHT);
                if (anim && r < g_nrows && g_rows[r] != SUMMARY_ROW) {
                    size_t bone = g_rows[r];
                    if (shift && RigBoneSelected(bone)) RigDeselectBone(bone);
                    else { RigSelectBone(bone, shift); SetEditBone(bone); }
                }
            } else if (mouse.x >= x0 && mouse.y >= rowstop && clip) {
                size_t r = first + (size_t)((mouse.y - rowstop) / TL_ROW_HEIGHT);
                BOOL hit = FALSE;
                if (r < g_nrows) {
                    size_t bone = g_rows[r];
                    size_t n = 0;
                    CachedKey* keys = RowKeys(bone, &n);
                    float best = TL_KEY_RADIUS + 3.0f;
                    float besttime = 0.0f;
                    float fmouse = XToFrame(mouse.x, x0, x1);
                    float reach = best / fmaxf(ppf, 1e-3f) + 1.0f;
                    for (size_t k = keys ? KeyLowerBound(keys, n, fmouse - reach) : 0; keys && k < n && keys[k].frame <= fmouse + reach; k++) {
                        float d = fabsf(FrameToX(keys[k].frame, x0, x1) - mouse.x);
                        if (d < best) { best = d; besttime = keys[k].ticks; hit = TRUE; }
                    }
                    if (hit) {
                        BOOL was = KeySelected(bone, besttime);
                        if (shift) KeySelect(bone, besttime, !was);
                        else if (!was) { g_nsel = 0; KeySelect(bone, besttime, TRUE); }
                        tl->frame = roundf(AnimationTicksToFrame(clip, besttime));
                        if (KeySelected(bone, besttime)) {
                            g_drag = DRAG_KEYS;
                            g_drag_origin = XToFrame(mouse.x, x0, x1);
                            g_drag_delta = 0.0f;
                        }
                    }
                }
                if (!hit) {
                    if (!shift) g_nsel = 0;
                    g_drag = DRAG_BOX;
                    g_box_start = mouse;
                }
            }
        }
        // keyboard
        if (IsKeyPressed(KEY_SPACE)) TimelineTogglePlay();
        if (IsKeyPressed(KEY_LEFT)) TimelineStep(-1.0f);
        if (IsKeyPressed(KEY_RIGHT)) TimelineStep(1.0f);
        if (IsKeyPressed(KEY_HOME)) tl->frame = tl->start;
        if (IsKeyPressed(KEY_END)) tl->frame = tl->end;
        if (anim && IsKeyPressed(KEY_UP)) { float f; if (KeyNeighbour(anim, tl->frame, 1, &f)) tl->frame = f; }
        if (anim && IsKeyPressed(KEY_DOWN)) { float f; if (KeyNeighbour(anim, tl->frame, -1, &f)) tl->frame = f; }
        if (anim && (IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_X))) DeleteSelectedKeys(anim);
        if (anim && IsKeyPressed(KEY_K)) RigKeySelected(KEY_CHANNEL_ALL);
        if (anim && clip && IsKeyPressed(KEY_A) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
            g_nsel = 0;
            for (size_t r = 0; r < g_nrows; r++) {
                size_t bone = g_rows[r];
                if (bone == SUMMARY_ROW) continue;
                size_t n = 0;
                CachedKey* keys = RowKeys(bone, &n);
                for (size_t k = 0; k < n; k++) KeyAppend(bone, keys[k].ticks);
            }
            SelectionNormalize();
        }
    }
    switch (g_drag) {
        case DRAG_SCRUB:
            tl->frame = roundf(XToFrame(mouse.x, x0, x1));
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) g_drag = DRAG_NONE;
            break;
        case DRAG_KEYS:
            g_drag_delta = XToFrame(mouse.x, x0, x1) - g_drag_origin;
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                if (anim) MoveSelectedKeys(anim, roundf(g_drag_delta));
                g_drag = DRAG_NONE;
                g_drag_delta = 0.0f;
            }
            break;
        case DRAG_BOX:
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                if (anim && clip) {
                    float fa = XToFrame(fminf(g_box_start.x, mouse.x), x0, x1), fb = XToFrame(fmaxf(g_box_start.x, mouse.x), x0, x1);
                    float ya = fminf(g_box_start.y, mouse.y), yb = fmaxf(g_box_start.y, mouse.y);
                    for (size_t r = first; r < g_nrows && r < first + visible; r++) {
                        float y = rowstop + (r - first) * TL_ROW_HEIGHT + TL_ROW_HEIGHT / 2;
                        if (y < ya || y > yb) continue;
                        size_t bone = g_rows[r];
                        size_t n = 0;
                        CachedKey* keys = RowKeys(bone, &n);
                        for (size_t k = keys ? KeyLowerBound(keys, n, fa) : 0; keys && k < n && keys[k].frame <= fb; k++) KeyAppend(bone, keys[k].ticks);
                    }
                }
                SelectionNormalize();
                g_drag = DRAG_NONE;
            }
            break;
        case DRAG_PAN: {
            float df = (mouse.x - g_pan_last.x) / fmaxf(ppf, 1e-3f);
            g_view_start -= df;
            g_view_end -= df;
            g_scroll -= (mouse.y - g_pan_last.y) / TL_ROW_HEIGHT;
            g_pan_last = mouse;
            if (!IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) g_drag = DRAG_NONE;
            break;
        }
        default: break;
    }
    if (g_drag == DRAG_SCRUB) {
        if (mouse.x > x1) { g_view_start += 1.0f; g_view_end += 1.0f; }
        if (mouse.x < x0) { g_view_start -= 1.0f; g_view_end -= 1.0f; }
    }
}

static void DrawTimelinePanel(float width, float height) {
    MeshAnimation* anim = RigActive();
    float top = DrawControls(anim, width);
    if (anim && anim->skeleton.bonecount > 0 && !CurrentClip(anim) && anim->animations.size == 0) {
        UISetCursor(10, top);
        UIDrawSubtleText("This rig has no clips yet - press New Clip (or key a bone, which creates one).");
        top += LINE_HEIGHT;
    }
    DrawDopesheet(anim, top, width, height);
}

static void CleanTimelinePanel() {
    ReleaseKeyCache();
}

Panel GenerateTimelinePanel() {
    Panel p = { 0 };
    SetupPanel(&p, "Timeline");
    p.draw = DrawTimelinePanel;
    p.clean = CleanTimelinePanel;
    p.scrollable = FALSE;
    return p;
}
