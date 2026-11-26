#ifndef ANIMATION_H
#define ANIMATION_H

#include "renderer/rstructs.h"

#define KEY_CHANNEL_LOCATION 0b1
#define KEY_CHANNEL_ROTATION 0b10
#define KEY_CHANNEL_SCALE    0b100
#define KEY_CHANNEL_ALL      0b111

#define TIMELINE_DEFAULT_FPS 24.0f
#define TIMELINE_DEFAULT_END 120.0f
#define KEY_TIME_EPSILON 1e-3f

BOOL PlayAnimations(Geometry* geometry);

float TimelineDT();

void OverrideTimelineDT(float dt);

AnimationTimeline* Timeline();

void TimelineSetFrame(float frame);

void TimelineStep(float frames);

void TimelineTogglePlay();

void TimelineSetFPS(float fps);

KeyInterpolation TimelineDefaultInterpolation();

void TimelineSetDefaultInterpolation(KeyInterpolation interp);

const char** KeyInterpolationLabels();

size_t RigOfMesh(size_t meshid);

void SkeletonFinalize(Skeleton* skeleton);

size_t SkeletonOrder(const Skeleton* skeleton, size_t* order);

void SkeletonRestGlobals(const Skeleton* skeleton, mat4* out);

BOOL SkeletonIsDescendant(const Skeleton* skeleton, size_t bone, size_t ancestor);

size_t SkeletonFindBone(const Skeleton* skeleton, const char* name);

void AnimationMarkDirty(MeshAnimation* anim);

uint64_t KeyRevision();

void KeysChanged();

void AnimationMarkAllDirty();

void AnimationEvaluateNow(size_t animid);

float AnimationClipTime(MeshAnimation* anim);

float AnimationFrameToTicks(const Animation* clip, float frame);

float AnimationTicksToFrame(const Animation* clip, float ticks);

void PoseIdentity(BonePose* pose);

void PoseToMatrix(const BonePose* pose, mat4 out);

void PoseFromMatrix(mat4 m, BonePose* out);

BOOL BoneIsPosed(const MeshAnimation* anim, size_t bone);

void BoneCurrentPose(MeshAnimation* anim, size_t bone, BonePose* out);

void BoneSetPose(MeshAnimation* anim, size_t bone, const BonePose* pose);

void BoneClearOverride(MeshAnimation* anim, size_t bone);

void BoneGlobal(const MeshAnimation* anim, size_t bone, mat4 out);

void BoneLocal(const MeshAnimation* anim, size_t bone, mat4 out);

void BoneBind(const MeshAnimation* anim, size_t bone, mat4 localbind, mat4 inversebind);

void BoneHeadTail(const MeshAnimation* anim, size_t bone, vec3 head, vec3 tail);

Animation* CurrentClip(MeshAnimation* anim);

size_t ClipCreate(MeshAnimation* anim, const char* name);

size_t ClipDuplicate(MeshAnimation* anim, size_t index);

void ClipDelete(MeshAnimation* anim, size_t index);

void ClipRename(MeshAnimation* anim, size_t index, const char* name);

BOOL ClipMakeEditable(MeshAnimation* anim, size_t index);

void ClipSelect(MeshAnimation* anim, size_t index);

void ClipRecomputeDuration(Animation* clip);

BoneChannel* ClipChannel(Animation* clip, const char* name, BOOL create);

void ClipFreeChannels(Animation* clip);

void KeyInsert(MeshAnimation* anim, size_t bone, float frame, uint32_t channels);

void KeyDelete(Animation* clip, const char* bone, float ticks);

BOOL KeyExists(Animation* clip, const char* bone, float ticks);

size_t KeyTimes(Animation* clip, const char* bone, float* out, size_t max);

void KeyMove(Animation* clip, const char* bone, float from, float to);

void KeySetInterpolation(Animation* clip, const char* bone, float ticks, KeyInterpolation interp);

KeyInterpolation KeyGetInterpolation(Animation* clip, const char* bone, float ticks);

BOOL KeyNeighbour(MeshAnimation* anim, float frame, int dir, float* out);

void ChannelRenameAll(MeshAnimation* anim, const char* oldname, const char* newname);

void ChannelRemoveAll(MeshAnimation* anim, const char* name);

#endif
