#include "animation.h"

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
                mat4 local[MAX_BONES];
                mat4 global[MAX_BONES];
                for (size_t b = 0; b < animation->skeleton.bonecount; b++) {
                    BoneChannel* channel = FindChannel(clip, animation->skeleton.bones[b].name);
                    if (channel) {
                        vec3 pos, scl;
                        versor rot;
                        SampleVec3Keys(&(channel->positions), animation->time, pos);
                        SampleQuatKeys(&(channel->rotations), animation->time, rot);
                        SampleVec3Keys(&(channel->scales), animation->time, scl);
                        glm_mat4_identity(local[b]);
                        glm_translate(local[b], pos);
                        glm_quat_rotate(local[b], rot, local[b]);
                        glm_scale(local[b], scl);
                    } else {
                        memcpy(local[b], animation->skeleton.bones[b].localbind, sizeof(mat4));
                    }
                }
                for (size_t b = 0; b < animation->skeleton.bonecount; b++) {
                    if (animation->skeleton.bones[b].parent == (size_t)-1)
                        glm_mat4_copy(local[b], global[b]);
                    else
                        glm_mat4_mul(global[animation->skeleton.bones[b].parent], local[b], global[b]);
                    mat4 tpose, invbind;
                    memcpy(invbind, animation->skeleton.bones[b].inversebind, sizeof(mat4));
                    glm_mat4_mul(global[b], invbind, tpose);
                    memcpy(pose[b], tpose, sizeof(mat4));
                }
            }
        }
    }
    return changes;
}
