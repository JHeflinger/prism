#include "animation.h"
#include <stdio.h>
#include <stddef.h>

static BoneChannel* FindChannel(Animation* clip, const char* name) {
    for (size_t i = 0; i < clip->channels.size; i++) {
        if (strcmp(clip->channels.data[i].name, name) == 0)
            return &(clip->channels.data[i]);
    }
    return NULL;
}

static void SampleVec3Keys(ARRLIST_Vec3Key* keys, float time, vec3 out) {
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

static void SampleQuatKeys(ARRLIST_QuatKey* keys, float time, versor out) {
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

static void SampleBone(MeshAnimation* animation, Animation* clip, float time, size_t b, mat4 local) {
    BoneChannel* channel = FindChannel(clip, animation->skeleton.bones[b].name);
    if (channel) {
        int full_channel = (channel->positions.size > 1 &&
                            channel->rotations.size > 1 &&
                            channel->scales.size > 1);
        vec3 trans, scl;
        versor rot;
        if (full_channel) {
            SampleVec3Keys(&(channel->positions), time, trans);
            SampleQuatKeys(&(channel->rotations), time, rot);
            SampleVec3Keys(&(channel->scales), time, scl);
            glm_mat4_identity(local);
            glm_translate(local, trans);
            glm_quat_rotate(local, rot, local);
            glm_scale(local, scl);
        } else {
            trans[0] = animation->skeleton.bones[b].localbind[3][0];
            trans[1] = animation->skeleton.bones[b].localbind[3][1];
            trans[2] = animation->skeleton.bones[b].localbind[3][2];
            if (channel->positions.size > 1)
                SampleVec3Keys(&(channel->positions), time, trans);
            scl[0] = 1.0f; scl[1] = 1.0f; scl[2] = 1.0f;
            if (channel->scales.size > 1)
                SampleVec3Keys(&(channel->scales), time, scl);
            mat4 prerot;
            memcpy(prerot, animation->skeleton.bones[b].localbind, sizeof(mat4));
            prerot[3][0] = 0.0f; prerot[3][1] = 0.0f; prerot[3][2] = 0.0f;
            mat4 anim_rot;
            if (channel->rotations.size >= 1) {
                SampleQuatKeys(&(channel->rotations), time, rot);
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
        }
    } else {
        memcpy(local, animation->skeleton.bones[b].localbind, sizeof(mat4));
    }
}

void BlendBone(MeshAnimation* animation,
                      Animation* clip_a, float time_a,
                      Animation* clip_b, float time_b,
                      float weight,
                      mat4* global, mat4* pose,
                      size_t b, mat4 parent_global) {
    mat4 local_a, local_b;
    SampleBone(animation, clip_a, time_a, b, local_a);
    SampleBone(animation, clip_b, time_b, b, local_b);
    vec3 s_a, s_b, s_out;
    s_a[0] = glm_vec3_norm((vec3){local_a[0][0], local_a[0][1], local_a[0][2]});
    s_a[1] = glm_vec3_norm((vec3){local_a[1][0], local_a[1][1], local_a[1][2]});
    s_a[2] = glm_vec3_norm((vec3){local_a[2][0], local_a[2][1], local_a[2][2]});
    s_b[0] = glm_vec3_norm((vec3){local_b[0][0], local_b[0][1], local_b[0][2]});
    s_b[1] = glm_vec3_norm((vec3){local_b[1][0], local_b[1][1], local_b[1][2]});
    s_b[2] = glm_vec3_norm((vec3){local_b[2][0], local_b[2][1], local_b[2][2]});
    glm_vec3_lerp(s_a, s_b, weight, s_out);
    mat4 rot_a, rot_b;
    memcpy(rot_a, local_a, sizeof(mat4));
    memcpy(rot_b, local_b, sizeof(mat4));
    for (int col = 0; col < 3; col++) {
        if (s_a[col] > 1e-6f) { rot_a[col][0] /= s_a[col]; rot_a[col][1] /= s_a[col]; rot_a[col][2] /= s_a[col]; }
        if (s_b[col] > 1e-6f) { rot_b[col][0] /= s_b[col]; rot_b[col][1] /= s_b[col]; rot_b[col][2] /= s_b[col]; }
    }
    versor r_a, r_b, r_out;
    glm_mat4_quat(rot_a, r_a);
    glm_mat4_quat(rot_b, r_b);
    if (glm_quat_dot(r_a, r_b) < 0.0f) glm_vec4_negate(r_b);
    glm_quat_slerp(r_a, r_b, weight, r_out);
    vec3 t_a = {local_a[3][0], local_a[3][1], local_a[3][2]};
    vec3 t_b = {local_b[3][0], local_b[3][1], local_b[3][2]};
    vec3 t_out;
    glm_vec3_lerp(t_a, t_b, weight, t_out);
    mat4 local_out;
    glm_mat4_identity(local_out);
    glm_translate(local_out, t_out);
    glm_quat_rotate(local_out, r_out, local_out);
    glm_scale(local_out, s_out);
    mat4 global_b, pose_b, inverse;
    glm_mat4_mul(parent_global, local_out, global_b);
    memcpy(global[b], global_b, sizeof(mat4));
    memcpy(inverse, animation->skeleton.bones[b].inversebind, sizeof(mat4));
    glm_mat4_mul(global_b, inverse, pose_b);
    memcpy(pose[b], pose_b, sizeof(mat4));
    for (size_t c = 0; c < animation->skeleton.bonecount; c++) {
        if (animation->skeleton.bones[c].parent == b)
            BlendBone(animation, clip_a, time_a, clip_b, time_b,
                      weight, global, pose, c, global_b);
    }
}

static void ProcessBone(MeshAnimation* animation, Animation* clip, mat4* global, mat4* pose, size_t b, mat4 parent_global) {
    mat4 local, global_b, invbind, pose_b;
    SampleBone(animation, clip, animation->time, b, local);
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
                float dt = GetFrameTime();
                Animation* clip = &(animation->animations.data[animation->current]);
                animation->time += dt * clip->tps;
                if (animation->looping) animation->time = fmod(animation->time, clip->duration);
                mat4 global[MAX_BONES];
                if (animation->blending) {
                    Animation* prev = &(animation->animations.data[animation->previous]);
                    animation->ptime += dt * prev->tps;
                    animation->ptime = fmod(animation->ptime, prev->duration);
                    animation->bweight += dt / animation->bduration;
                    if (animation->bweight >= 1.0f) {
                        animation->bweight = 1.0f;
                        animation->blending = FALSE;
                    }
                    for (size_t b = 0; b < animation->skeleton.bonecount; b++) {
                        if (animation->skeleton.bones[b].parent == (size_t)-1)
                            BlendBone(animation,
                                      prev, animation->ptime,
                                      clip, animation->time,
                                      animation->bweight,
                                      global, pose, b, identity);
                    }
                } else {
                    for (size_t b = 0; b < animation->skeleton.bonecount; b++) {
                        if (animation->skeleton.bones[b].parent == (size_t)-1)
                            ProcessBone(animation, clip, global, pose, b, identity);
                    }
                }
            }
        }
    }
    return changes;
}
