#include "animation.h"
#include <stdio.h>
#include <stddef.h>

BoneChannel* FindChannel(Animation* clip, const char* name) {
    for (size_t i = 0; i < clip->channels.size; i++) {
        if (strcmp(clip->channels.data[i].name, name) == 0)
            return &(clip->channels.data[i]);
    }
    return NULL;
}

void SampleVec3Keys(ARRLIST_Vec3Key* keys, float time, vec3 out) {
    if (keys->size == 0) {
        glm_vec3_zero(out);
        return;
    }
    if (keys->size == 1 || time <= keys->data[0].time) {
        glm_vec3_copy(keys->data[0].value, out);
        return;
    }
    if (time >= keys->data[keys->size - 1].time) {
        glm_vec3_copy(keys->data[keys->size - 1].value, out);
        return;
    }
    size_t i = 0;
    while (i < keys->size - 1 && keys->data[i + 1].time < time) i++;
    float t = (float)((time - keys->data[i].time) / (keys->data[i+1].time - keys->data[i].time));
    glm_vec3_lerp(keys->data[i].value, keys->data[i + 1].value, t, out);
}

void SampleQuatKeys(ARRLIST_QuatKey* keys, float time, versor out) {
    if (keys->size == 0) {
        glm_quat_identity(out);
        return;
    }
    if (keys->size == 1 || time <= keys->data[0].time) {
        memcpy(out, keys->data[0].value, sizeof(versor));
        return;
    }
    if (time >= keys->data[keys->size - 1].time) {
        memcpy(out, keys->data[keys->size - 1].value, sizeof(versor));
        return;
    }
    size_t i = 0;
    while (i < keys->size - 1 && keys->data[i + 1].time < time) i++;
    float t = (float)((time - keys->data[i].time) / (keys->data[i + 1].time - keys->data[i].time));
    versor ti, tip1;
    memcpy(ti, keys->data[i].value, sizeof(versor));
    memcpy(tip1, keys->data[i + 1].value, sizeof(versor));
    if (glm_quat_dot(ti, tip1) < 0.0f)
        glm_vec4_negate(tip1);
    glm_quat_slerp(ti, tip1, t, out);
}

static void ProcessBone(MeshAnimation* animation, Animation* clip, mat4* global, mat4* pose, size_t b, mat4 parent_global) {
    BoneChannel* channel = FindChannel(clip, animation->skeleton.bones[b].name);
    mat4 local, global_b, invbind, pose_b;
    if (channel) {
        vec3 trans = { animation->skeleton.bones[b].localbind[3][0],
                       animation->skeleton.bones[b].localbind[3][1],
                       animation->skeleton.bones[b].localbind[3][2] };
        if (channel->positions.size > 1)
            SampleVec3Keys(&(channel->positions), animation->time, trans);
        vec3 scl = { 1.0f, 1.0f, 1.0f };
        if (channel->scales.size > 1)
            SampleVec3Keys(&(channel->scales), animation->time, scl);
        mat4 prerot;
        memcpy(prerot, animation->skeleton.bones[b].localbind, sizeof(mat4));
        prerot[3][0] = 0.0f; prerot[3][1] = 0.0f; prerot[3][2] = 0.0f;
        mat4 anim_rot;
        if (channel->rotations.size >= 1) {
            versor rot;
            SampleQuatKeys(&(channel->rotations), animation->time, rot);
            glm_mat4_identity(anim_rot);
            glm_quat_rotate(anim_rot, rot, anim_rot);
        } else {
            glm_mat4_identity(anim_rot);
        }
        mat4 prerot_anim;
        glm_mat4_mul(prerot, anim_rot, prerot_anim);
        glm_mat4_identity(local);
        glm_translate(local, trans);
        glm_mat4_mul(local, prerot_anim, local);
        glm_scale(local, scl);
    } else {
        memcpy(local, animation->skeleton.bones[b].localbind, sizeof(mat4));
    }
    glm_mat4_mul(parent_global, local, global_b);
    memcpy(global[b], global_b, sizeof(mat4));
    memcpy(invbind, animation->skeleton.bones[b].inversebind, sizeof(mat4));
    glm_mat4_mul(global_b, invbind, pose_b);
    memcpy(pose[b], pose_b, sizeof(mat4));
    for (size_t c = 0; c < animation->skeleton.bonecount; c++) {
        if (animation->skeleton.bones[c].parent == b)
            ProcessBone(animation, clip, global, pose, c, global_b);
    }
}

BOOL PlayAnimations(Geometry* geometry) {
    BOOL changes = FALSE;
    for (size_t i = 0; i < geometry->animations.size; i++) {
        MeshAnimation* animation = &(geometry->animations.data[i]);
        mat4* pose = &(geometry->poses.data[i * MAX_BONES]);
        mat4 identity = GLM_MAT4_IDENTITY_INIT;
        if (!animation->enable && animation->_enable) {
            animation->_enable = FALSE;
            for (size_t j = 0; j < MAX_BONES; j++) memcpy(pose[j], identity, sizeof(mat4));
            changes = TRUE;
        } else if (animation->enable) {
            animation->_enable = TRUE;
            if (animation->playing) {
                changes = TRUE;
                Animation* clip = &(animation->animations.data[animation->current]);
                animation->time += GetFrameTime() * clip->tps;
                if (animation->looping) animation->time = fmod(animation->time, clip->duration);
                mat4 global[MAX_BONES];
                for (size_t b = 0; b < animation->skeleton.bonecount; b++) {
                    if (animation->skeleton.bones[b].parent == (size_t)-1)
                        ProcessBone(animation, clip, global, pose, b, identity);
                }
            }
        }
    }
    return changes;
}
