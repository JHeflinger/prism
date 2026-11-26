#ifndef RIG_H
#define RIG_H

#include "renderer/rstructs.h"

typedef enum {
    RIG_MODE_EDIT = 0,
    RIG_MODE_POSE,
    RIG_MODE_WEIGHTS,
    RIG_MODE_COUNT
} RigMode;

typedef enum {
    BRUSH_ADD = 0,
    BRUSH_SUBTRACT,
    BRUSH_REPLACE,
    BRUSH_SMOOTH,
    BRUSH_COUNT
} BrushMode;

typedef enum {
    AUTOWEIGHT_HEAT = 0,
    AUTOWEIGHT_ENVELOPE,
    AUTOWEIGHT_COUNT
} AutoWeightMethod;

typedef enum {
    EDIT_HANDLE_BONE = 0,
    EDIT_HANDLE_HEAD,
    EDIT_HANDLE_TAIL,
    EDIT_HANDLE_COUNT
} EditHandle;

typedef enum {
    MIRROR_POSITIVE_TO_NEGATIVE = 0,
    MIRROR_NEGATIVE_TO_POSITIVE,
} MirrorDirection;

typedef struct {
    BrushMode mode;
    float radius;
    float strength;
    float weight;
    BOOL frontonly;
} WeightBrush;

typedef struct {
    BOOL valid;
    BOOL pose;
    size_t bone;
    EditHandle handle;
    BOOL posed;
    BonePose override;
    BonePose current;
    mat4 global;
    mat4 parent;
    vec3 head;
    vec3 tail;
    float roll;
    size_t parentbone;
    vec3 parenthead;
    vec3 parenttail;
    float parentroll;
} RigBoneSnapshot;

// ---- active rig / selection ----------------------------------------------------------------------

// keeps the active rig in sync with the scene, call once per frame
void RigUpdate();

size_t RigActiveMesh();

size_t RigActiveAnimation();

MeshAnimation* RigActive();

void RigSetActiveMesh(size_t meshid);

// keeps the active rig pointing at the same object when another object is deleted
void RigMeshDeleted(size_t meshid);

RigMode RigGetMode();

// also turns the animation select mode on
void RigSetMode(RigMode mode);

// bones are shown and the rig modes apply (the overlay is in the animation select mode)
BOOL RigViewportActive();

const char** RigModeLabels();

EditHandle RigEditHandle();

void RigSetEditHandle(EditHandle handle);

const char** RigEditHandleLabels();

size_t RigActiveBone();

void RigSelectBone(size_t bone, BOOL extend);

void RigDeselectBone(size_t bone);

BOOL RigBoneSelected(size_t bone);

void RigClearSelection();

size_t RigSelectedCount();

void RigSelectAll();

// ---- rig + bone authoring (mesh space) -----------------------------------------------------------

BOOL RigCreateForMesh(size_t meshid);

BOOL RigRemove(size_t meshid);

// skins of the mesh line up with its vertices
BOOL RigSkinsValid(size_t animid);

void RigMeshTransform(mat4 out);

void RigBoneFrame(const vec3 head, const vec3 tail, float roll, mat4 out);

void RigRestHeadTail(size_t bone, vec3 head, vec3 tail);

float RigBoneRoll(size_t bone);

void RigSetBoneRest(size_t bone, const vec3 head, const vec3 tail, float roll);

size_t RigAddBone(size_t parent, const vec3 head, const vec3 tail, const char* name);

// adds a child continuing from the tail of a bone
size_t RigExtrudeBone(size_t from);

BOOL RigDeleteBone(size_t bone);

BOOL RigSetParent(size_t bone, size_t parent);

void RigRenameBone(size_t bone, const char* name);

void RigSetBoneFlag(size_t bone, uint32_t flag, BOOL on);

// creates/updates the opposite side of every .L/.R style bone, returns the number of bones touched
size_t RigSymmetrize(MirrorDirection direction);

void RigUniqueBoneName(const char* base, char out[256]);

// ---- weights -------------------------------------------------------------------------------------

const char** AutoWeightLabels();

const char** BrushModeLabels();

BOOL RigAutoWeightsBegin(AutoWeightMethod method, BOOL selectedonly);

BOOL RigAutoWeightsRunning();

float RigAutoWeightsProgress();

void RigAutoWeightsTick(double budget);

void RigAutoWeightsCancel();

void RigNormalizeWeights();

void RigCleanWeights(float threshold);

void RigSmoothWeights(BOOL selectedonly, uint32_t iterations, float factor);

void RigMirrorWeights(MirrorDirection direction);

// weight of a bone in a vertex skin (0 when the bone is not an influence)
float SkinWeight(const VertexSkin* skin, uint32_t bone);

WeightBrush* RigBrush();

// casts the brush against the posed mesh; when apply is set the active bone's weights are painted
BOOL RigPaint(const ViewProjection* vp, const vec2 mouse, BOOL apply, BOOL invert, float dt);

BOOL RigBrushHit(vec3 world, vec3 normal);

// what the overlay shader needs to paint the active bone's weights (FALSE outside of weight paint mode)
BOOL RigWeightOverlay(uint32_t* bone, uint32_t* tstart, uint32_t* tend, uint32_t* vstart, uint32_t* skinstart);

// ---- inverse kinematics --------------------------------------------------------------------------

size_t RigAddIK(size_t bone, size_t target);

void RigRemoveIK(size_t index);

// first constraint whose tip is the bone, or (size_t)-1
size_t RigFindIK(size_t bone);

// creates a target bone at the tip of the chain (parented to the chain's root parent) and an IK constraint
size_t RigCreateIKWithTarget(size_t bone, uint32_t chain);

// ---- viewport ------------------------------------------------------------------------------------

// head/tail of a bone in world space as shown in the viewport (rest in edit mode, posed otherwise)
BOOL RigBoneWorld(size_t bone, vec3 head, vec3 tail);

// world space axes of a bone (normalized), in the same state as RigBoneWorld
BOOL RigBoneWorldAxes(size_t bone, vec3 axes[3]);

size_t RigPickBone(const ViewProjection* vp, const vec2 mouse, float maxpx);

// same as RigBoneWorld/RigBoneWorldAxes for any rig (other rigs are always shown posed)
BOOL RigBoneWorldOf(size_t animid, size_t bone, vec3 head, vec3 tail);

BOOL RigBoneWorldAxesOf(size_t animid, size_t bone, vec3 axes[3]);

// picks a bone of any visible rig, returns the bone and writes its rig's animation index
size_t RigPickAnyBone(const ViewProjection* vp, const vec2 mouse, float maxpx, size_t* animid);

// frees the cached welded graph, skinning buffers and any running weights job
void RigReleaseCaches();

// ---- transform gizmo glue ------------------------------------------------------------------------

void RigSnapshotTake(size_t bone, RigBoneSnapshot* snap);

void RigSnapshotRestore(const RigBoneSnapshot* snap);

void RigApplyTranslate(const RigBoneSnapshot* snap, const vec3 world_delta);

void RigApplyRotate(const RigBoneSnapshot* snap, const vec3 world_axis, float angle);

void RigApplyScale(const RigBoneSnapshot* snap, const vec3 scale);

// called when a drag finishes (auto keying)
void RigSnapshotCommit(const RigBoneSnapshot* snap);

// ---- pose tools ----------------------------------------------------------------------------------

void RigResetPose(BOOL selectedonly, uint32_t channels);

void RigKeySelected(uint32_t channels);

void RigCopyPose();

void RigPastePose(BOOL mirrored);

#endif
