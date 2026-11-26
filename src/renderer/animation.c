#include "animation.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include <util/logger.h>

#define KEY_SEGMENT(keys, time, out) { \
    size_t _lo = 0, _hi = (keys)->size - 1; \
    while (_hi - _lo > 1) { \
        size_t _mid = (_lo + _hi) / 2; \
        if ((keys)->data[_mid].time <= (time)) _lo = _mid; else _hi = _mid; \
    } \
    out = _lo; \
}

#define REMOVE_KEYS_AT(list, type, t) { \
    for (size_t _i = (list).size; _i-- > 0;) \
        if (fabsf((list).data[_i].time - (t)) < KEY_TIME_EPSILON) ARRLIST_##type##_remove(&(list), _i); \
}

#define MOVE_KEYS(list, type, from, to) { \
    BOOL _found = FALSE; \
    type _moved = { 0 }; \
    for (size_t _i = 0; _i < (list).size; _i++) \
        if (fabsf((list).data[_i].time - (from)) < KEY_TIME_EPSILON) { _moved = (list).data[_i]; _found = TRUE; break; } \
    if (_found) { \
        REMOVE_KEYS_AT(list, type, from); \
        REMOVE_KEYS_AT(list, type, to); \
        _moved.time = (to); \
        size_t _at = (list).size; \
        for (size_t _i = 0; _i < (list).size; _i++) if ((list).data[_i].time > (to)) { _at = _i; break; } \
        if (_at == (list).size) ARRLIST_##type##_add(&(list), _moved); \
        else ARRLIST_##type##_insert(&(list), _moved, _at); \
    } \
}

static float g_dt = -1.0f;
static uint64_t g_key_revision = 1;
static KeyInterpolation g_default_interp = KEY_INTERP_SMOOTH;
static const char* g_interp_labels[KEY_INTERP_COUNT] = { "Linear", "Constant", "Smooth" };

__attribute__((noinline)) static void BlobCopy(void* dst, const void* src, size_t size) { // use this to maintain 8 byte alignment
    memcpy(dst, src, size);
}

static void M4Copy(void* dst, const void* src) {
    BlobCopy(dst, src, sizeof(mat4));
}

static void M4Head(const mat4 m, vec3 out) {
    out[0] = m[3][0];
    out[1] = m[3][1];
    out[2] = m[3][2];
}

static void QuatCopy(void* dst, const void* src) {
    BlobCopy(dst, src, sizeof(float) * 4);
}

static void QuatNormalizeSafe(versor q) {
    float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n < 1e-12f) { glm_quat_identity(q); return; }
    for (int i = 0; i < 4; i++) q[i] /= n;
}

static void QuatFromTo(const vec3 a, const vec3 b, versor out) {
    vec3 na, nb, axis;
    glm_vec3_normalize_to((float*)a, na);
    glm_vec3_normalize_to((float*)b, nb);
    float d = glm_vec3_dot(na, nb);
    if (d > 1.0f - 1e-7f) { glm_quat_identity(out); return; }
    if (d < -1.0f + 1e-7f) {
        vec3 ref = { 1, 0, 0 };
        if (fabsf(na[0]) > 0.9f) SETVEC3(ref, 0, 1, 0);
        glm_vec3_cross(na, ref, axis);
        glm_vec3_normalize(axis);
        glm_quatv(out, GLM_PIf, axis);
        return;
    }
    glm_vec3_cross(na, nb, axis);
    out[0] = axis[0];
    out[1] = axis[1];
    out[2] = axis[2];
    out[3] = 1.0f + d;
    QuatNormalizeSafe(out);
}

static void DecomposeTRS(mat4 m, vec3 t, versor r, vec3 s) {
    vec3 c0 = { m[0][0], m[0][1], m[0][2] };
    vec3 c1 = { m[1][0], m[1][1], m[1][2] };
    vec3 c2 = { m[2][0], m[2][1], m[2][2] };
    s[0] = glm_vec3_norm(c0);
    s[1] = glm_vec3_norm(c1);
    s[2] = glm_vec3_norm(c2);
    vec3 cross;
    glm_vec3_cross(c0, c1, cross);
    if (glm_vec3_dot(cross, c2) < 0.0f) s[0] = -s[0]; // keep the rotation proper
    mat4 rot = GLM_MAT4_IDENTITY_INIT;
    for (int c = 0; c < 3; c++) {
        float sc = c == 0 ? s[0] : (c == 1 ? s[1] : s[2]);
        if (fabsf(sc) < 1e-12f) sc = 1e-12f;
        for (int r = 0; r < 3; r++) rot[c][r] = m[c][r] / sc;
    }
    glm_mat4_quat(rot, r);
    QuatNormalizeSafe(r);
    t[0] = m[3][0];
    t[1] = m[3][1];
    t[2] = m[3][2];
}

static void ComposeTRS(const vec3 t, const versor r, const vec3 s, mat4 out) {
    versor q;
    QuatCopy(q, r);
    QuatNormalizeSafe(q);
    glm_mat4_identity(out);
    glm_translate(out, (float*)t);
    glm_quat_rotate(out, q, out);
    glm_scale(out, (float*)s);
}

static void BlendLocals(mat4 a, mat4 b, float w, mat4 out) {
    vec3 ta, tb, sa, sb, t, s;
    versor ra, rb, r;
    DecomposeTRS(a, ta, ra, sa);
    DecomposeTRS(b, tb, rb, sb);
    if (glm_quat_dot(ra, rb) < 0.0f) glm_vec4_negate(rb);
    glm_vec3_lerp(ta, tb, w, t);
    glm_vec3_lerp(sa, sb, w, s);
    glm_quat_slerp(ra, rb, w, r);
    ComposeTRS(t, r, s, out);
}

static float Smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static void SetPosedBit(MeshAnimation* anim, size_t bone, BOOL on) {
    if (bone >= MAX_BONES) return;
    if (on) anim->posed[bone / 32] |= (1u << (bone % 32));
    else anim->posed[bone / 32] &= ~(1u << (bone % 32));
}

static BOOL ValidParent(const Skeleton* sk, size_t b) {
    size_t p = sk->bones[b].parent;
    return p != (size_t)-1 && p < sk->bonecount && p != b;
}

static BoneChannel* FindChannel(Animation* clip, const char* name) {
    for (size_t i = 0; i < clip->channels.size; i++) {
        if (strcmp(clip->channels.data[i].name, name) == 0)
            return &(clip->channels.data[i]);
    }
    return NULL;
}

static float ClampedTangent(float prev, float tprev, float cur, float tcur, float next, float tnext, BOOL hasprev, BOOL hasnext) {
    if (!hasprev || !hasnext) return 0.0f;
    if ((cur - prev) * (next - cur) <= 0.0f) return 0.0f;
    float dt = tnext - tprev;
    if (dt <= 1e-9f) return 0.0f;
    return (next - prev) / dt;
}

static float Hermite(float p0, float m0, float p1, float m1, float t, float dt) {
    float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * dt * m0 + (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * dt * m1;
}

static void SampleVec3Keys(ARRLIST_Vec3Key* keys, float time, vec3 out, const vec3 fallback) {
    if (keys->size == 0) {
        glm_vec3_copy((float*)fallback, out);
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
    size_t i;
    KEY_SEGMENT(keys, time, i);
    Vec3Key* a = &(keys->data[i]);
    Vec3Key* b = &(keys->data[i + 1]);
    float span = b->time - a->time;
    float t = span > 1e-9f ? (time - a->time) / span : 0.0f;
    if (a->interp == KEY_INTERP_STEP) {
        glm_vec3_copy(a->value, out);
    } else if (a->interp == KEY_INTERP_SMOOTH) {
        BOOL hp = i > 0, hn = i + 2 < keys->size;
        for (int c = 0; c < 3; c++) {
            float m0 = ClampedTangent(hp ? keys->data[i - 1].value[c] : 0, hp ? keys->data[i - 1].time : 0, a->value[c], a->time, b->value[c], b->time, hp, TRUE);
            float m1 = ClampedTangent(a->value[c], a->time, b->value[c], b->time, hn ? keys->data[i + 2].value[c] : 0, hn ? keys->data[i + 2].time : 0, TRUE, hn);
            out[c] = Hermite(a->value[c], m0, b->value[c], m1, t, span);
        }
    } else {
        glm_vec3_lerp(a->value, b->value, t, out);
    }
}

static void SampleQuatKeys(ARRLIST_QuatKey* keys, float time, versor out) {
    if (keys->size == 0) {
        glm_quat_identity(out);
        return;
    }
    if (keys->size == 1 || time <= keys->data[0].time) {
        QuatCopy(out, keys->data[0].value);
        return;
    }
    if (time >= keys->data[keys->size - 1].time) {
        QuatCopy(out, keys->data[keys->size - 1].value);
        return;
    }
    size_t i;
    KEY_SEGMENT(keys, time, i);
    QuatKey* ka = &(keys->data[i]);
    QuatKey* kb = &(keys->data[i + 1]);
    float span = kb->time - ka->time;
    float t = span > 1e-9f ? (time - ka->time) / span : 0.0f;
    versor a, b;
    QuatCopy(a, ka->value);
    QuatCopy(b, kb->value);
    if (glm_quat_dot(a, b) < 0.0f) glm_vec4_negate(b);
    if (ka->interp == KEY_INTERP_STEP) {
        QuatCopy(out, a);
    } else if (ka->interp == KEY_INTERP_SMOOTH) {
        BOOL hp = i > 0, hn = i + 2 < keys->size;
        versor p, n;
        if (hp) { QuatCopy(p, keys->data[i - 1].value); if (glm_quat_dot(p, a) < 0.0f) glm_vec4_negate(p); }
        if (hn) { QuatCopy(n, keys->data[i + 2].value); if (glm_quat_dot(n, b) < 0.0f) glm_vec4_negate(n); }
        for (int c = 0; c < 4; c++) {
            float m0 = ClampedTangent(hp ? p[c] : 0, hp ? keys->data[i - 1].time : 0, a[c], ka->time, b[c], kb->time, hp, TRUE);
            float m1 = ClampedTangent(a[c], ka->time, b[c], kb->time, hn ? n[c] : 0, hn ? keys->data[i + 2].time : 0, TRUE, hn);
            out[c] = Hermite(a[c], m0, b[c], m1, t, span);
        }
        if (!hp && !hn) glm_quat_slerp(a, b, Smoothstep(t), out);
        QuatNormalizeSafe(out);
    } else {
        glm_quat_slerp(a, b, t, out);
    }
}

static void SampleImported(const Bone* bone, BoneChannel* channel, float time, mat4 local) {
    vec3 trans, scl;
    versor rot;
    mat4 localbind;
    M4Copy(localbind, bone->localbind);
    int full_channel = (channel->positions.size > 1 &&
                        channel->rotations.size > 1 &&
                        channel->scales.size > 1);
    vec3 zero = { 0, 0, 0 };
    if (full_channel) {
        SampleVec3Keys(&(channel->positions), time, trans, zero);
        SampleQuatKeys(&(channel->rotations), time, rot);
        SampleVec3Keys(&(channel->scales), time, scl, zero);
        glm_mat4_identity(local);
        glm_translate(local, trans);
        glm_quat_rotate(local, rot, local);
        glm_scale(local, scl);
        return;
    }
    trans[0] = localbind[3][0];
    trans[1] = localbind[3][1];
    trans[2] = localbind[3][2];
    if (channel->positions.size > 1)
        SampleVec3Keys(&(channel->positions), time, trans, zero);
    scl[0] = 1.0f; scl[1] = 1.0f; scl[2] = 1.0f;
    if (channel->scales.size > 1)
        SampleVec3Keys(&(channel->scales), time, scl, zero);
    mat4 prerot;
    M4Copy(prerot, localbind);
    prerot[3][0] = 0.0f; prerot[3][1] = 0.0f; prerot[3][2] = 0.0f;
    mat4 anim_rot;
    glm_mat4_identity(anim_rot);
    if (channel->rotations.size >= 1) {
        SampleQuatKeys(&(channel->rotations), time, rot);
        glm_quat_rotate(anim_rot, rot, anim_rot);
    }
    mat4 prerot_anim;
    glm_mat4_mul(prerot, anim_rot, prerot_anim);
    glm_mat4_identity(local);
    glm_translate(local, trans);
    glm_mat4_mul(local, prerot_anim, local);
    glm_scale(local, scl);
}

static void SampleAuthoredPose(BoneChannel* channel, float time, BonePose* pose) {
    PoseIdentity(pose);
    vec3 zero = { 0, 0, 0 }, one = { 1, 1, 1 };
    versor r;
    SampleVec3Keys(&(channel->positions), time, pose->location, zero);
    SampleQuatKeys(&(channel->rotations), time, r);
    QuatCopy(pose->rotation, r);
    SampleVec3Keys(&(channel->scales), time, pose->scale, one);
}

static void SampleLocal(const Bone* bone, Animation* clip, float time, mat4 local) {
    BoneChannel* channel = clip ? FindChannel(clip, bone->name) : NULL;
    if (!channel) {
        M4Copy(local, bone->localbind);
        return;
    }
    if (clip->mode == ANIMATION_AUTHORED) {
        BonePose pose;
        mat4 delta, bind;
        SampleAuthoredPose(channel, time, &pose);
        PoseToMatrix(&pose, delta);
        M4Copy(bind, bone->localbind);
        glm_mat4_mul(bind, delta, local);
    } else {
        SampleImported(bone, channel, time, local);
    }
}

static BOOL ChannelHasKeys(BoneChannel* ch) {
    return ch && (ch->positions.size + ch->rotations.size + ch->scales.size) > 0;
}

static void ForwardKinematics(const Skeleton* sk, const size_t* order, size_t n, mat4* local, mat4* global) {
    for (size_t i = 0; i < n; i++) {
        size_t b = order[i];
        if (ValidParent(sk, b)) glm_mat4_mul(global[sk->bones[b].parent], local[b], global[b]);
        else M4Copy(global[b], local[b]);
    }
}

static void RotateAroundAxis(vec3 point, const vec3 origin, const vec3 axis, float angle) {
    vec3 rel;
    glm_vec3_sub(point, (float*)origin, rel);
    glm_vec3_rotate(rel, angle, (float*)axis);
    glm_vec3_add((float*)origin, rel, point);
}

static float SignedAngle(const vec3 a, const vec3 b, const vec3 axis) {
    vec3 c;
    glm_vec3_cross((float*)a, (float*)b, c);
    return atan2f(glm_vec3_dot(c, (float*)axis), glm_vec3_dot((float*)a, (float*)b));
}

static void SolveIK(const Skeleton* sk, const IKConstraint* ik, const size_t* order, size_t n, mat4* local, mat4* global) {
    size_t count = sk->bonecount;
    if (ik->bone >= count || ik->target >= count || ik->target == ik->bone) return;
    size_t chain[MAX_BONES];
    size_t m = 0;
    size_t cur = ik->bone;
    size_t limit = ik->chain == 0 ? count : ik->chain;
    while (cur != (size_t)-1 && m < limit) {
        chain[m++] = cur;
        cur = ValidParent(sk, cur) ? sk->bones[cur].parent : (size_t)-1;
    }
    for (size_t i = 0; i < m / 2; i++) { size_t t = chain[i]; chain[i] = chain[m - 1 - i]; chain[m - 1 - i] = t; }
    vec3 P[MAX_BONES + 1];
    float len[MAX_BONES];
    for (size_t i = 0; i < m; i++) M4Head(global[chain[i]], P[i]);
    vec3 tail;
    glm_vec3_copy((float*)sk->bones[ik->bone].tail, tail);
    glm_mat4_mulv3(global[ik->bone], tail, 1.0f, P[m]);
    float total = 0.0f;
    for (size_t i = 0; i < m; i++) {
        len[i] = glm_vec3_distance(P[i], P[i + 1]);
        total += len[i];
    }
    if (total < 1e-9f) return;
    vec3 target, root;
    M4Head(global[ik->target], target);
    glm_vec3_copy(P[0], root);
    float reach = glm_vec3_distance(root, target);
    if (reach >= total) {
        vec3 dir;
        glm_vec3_sub(target, root, dir);
        glm_vec3_normalize(dir);
        for (size_t i = 0; i < m; i++) {
            vec3 step;
            glm_vec3_scale(dir, len[i], step);
            glm_vec3_add(P[i], step, P[i + 1]);
        }
    } else {
        uint32_t iterations = ik->iterations == 0 ? 16 : ik->iterations;
        float tolerance = total * 1e-5f;
        for (uint32_t it = 0; it < iterations; it++) {
            glm_vec3_copy(target, P[m]);
            for (size_t i = m; i-- > 0;) {
                vec3 d;
                glm_vec3_sub(P[i], P[i + 1], d);
                if (glm_vec3_norm(d) < 1e-12f) SETVEC3(d, 0, 1e-6f, 0);
                glm_vec3_normalize(d);
                glm_vec3_scale(d, len[i], d);
                glm_vec3_add(P[i + 1], d, P[i]);
            }
            glm_vec3_copy(root, P[0]);
            for (size_t i = 0; i < m; i++) {
                vec3 d;
                glm_vec3_sub(P[i + 1], P[i], d);
                if (glm_vec3_norm(d) < 1e-12f) SETVEC3(d, 0, 1e-6f, 0);
                glm_vec3_normalize(d);
                glm_vec3_scale(d, len[i], d);
                glm_vec3_add(P[i], d, P[i + 1]);
            }
            if (glm_vec3_distance(P[m], target) < tolerance) break;
        }
    }
    if (m >= 2) {
        BOOL haspole = ik->pole < count && ik->pole != ik->bone;
        float extra = glm_rad(ik->poleangle);
        if (haspole) {
            vec3 pole;
            M4Head(global[ik->pole], pole);
            for (size_t i = 1; i < m; i++) {
                vec3 axis, a, b;
                glm_vec3_sub(P[i + 1], P[i - 1], axis);
                if (glm_vec3_norm(axis) < 1e-9f) continue;
                glm_vec3_normalize(axis);
                glm_vec3_sub(P[i], P[i - 1], a);
                glm_vec3_sub(pole, P[i - 1], b);
                vec3 pa, pb;
                glm_vec3_scale(axis, glm_vec3_dot(a, axis), pa);
                glm_vec3_sub(a, pa, a);
                glm_vec3_scale(axis, glm_vec3_dot(b, axis), pb);
                glm_vec3_sub(b, pb, b);
                if (glm_vec3_norm(a) < 1e-9f || glm_vec3_norm(b) < 1e-9f) continue;
                float angle = SignedAngle(a, b, axis);
                RotateAroundAxis(P[i], P[i - 1], axis, angle);
            }
        }
        if (fabsf(extra) > 1e-6f) {
            vec3 axis;
            glm_vec3_sub(P[m], P[0], axis);
            if (glm_vec3_norm(axis) > 1e-9f) {
                glm_vec3_normalize(axis);
                for (size_t i = 1; i < m; i++) RotateAroundAxis(P[i], P[0], axis, extra);
            }
        }
    }
    float influence = glm_clamp(ik->influence, 0.0f, 1.0f);
    for (size_t i = 0; i < m; i++) {
        size_t b = chain[i];
        mat4 parent, fk, solved, inv, iklocal;
        if (ValidParent(sk, b)) M4Copy(parent, global[sk->bones[b].parent]);
        else glm_mat4_identity(parent);
        glm_mat4_mul(parent, local[b], fk);
        vec3 head, btail, curdir, wanted;
        M4Head(fk, head);
        glm_vec3_copy((float*)sk->bones[b].tail, btail);
        glm_mat4_mulv3(fk, btail, 0.0f, curdir);
        glm_vec3_sub(P[i + 1], head, wanted);
        if (glm_vec3_norm(curdir) < 1e-12f || glm_vec3_norm(wanted) < 1e-12f) {
            M4Copy(global[b], fk);
            continue;
        }
        versor q;
        QuatFromTo(curdir, wanted, q);
        mat4 rq, linear;
        glm_quat_mat4(q, rq);
        M4Copy(linear, fk);
        linear[3][0] = 0.0f; linear[3][1] = 0.0f; linear[3][2] = 0.0f;
        glm_mat4_mul(rq, linear, solved);
        solved[3][0] = head[0]; solved[3][1] = head[1]; solved[3][2] = head[2];
        if (i == m - 1 && ik->matchrotation) {
            mat4 tg;
            vec3 tt, ts, st, ss;
            versor tr, sr;
            M4Copy(tg, global[ik->target]);
            DecomposeTRS(tg, tt, tr, ts);
            DecomposeTRS(solved, st, sr, ss);
            ComposeTRS(st, tr, ss, solved);
        }
        glm_mat4_inv(parent, inv);
        glm_mat4_mul(inv, solved, iklocal);
        if (influence < 1.0f) BlendLocals(local[b], iklocal, influence, local[b]);
        else M4Copy(local[b], iklocal);
        glm_mat4_mul(parent, local[b], global[b]);
    }
    ForwardKinematics(sk, order, n, local, global);
}

static void EvaluateRig(MeshAnimation* anim, mat4* pose) {
    const Skeleton* sk = &(anim->skeleton);
    size_t count = sk->bonecount > MAX_BONES ? MAX_BONES : sk->bonecount;
    static mat4 local[MAX_BONES];
    static mat4 global[MAX_BONES];
    size_t order[MAX_BONES];
    size_t n = SkeletonOrder(sk, order);
    Animation* clip = CurrentClip(anim);
    Animation* prev = (anim->blending && !anim->synced && anim->previous < anim->animations.size) ? &(anim->animations.data[anim->previous]) : NULL;
    for (size_t b = 0; b < count; b++) {
        const Bone* bone = &(sk->bones[b]);
        if (anim->rest) {
            M4Copy(local[b], bone->localbind);
            continue;
        }
        SampleLocal(bone, clip, anim->time, local[b]);
        if (prev) {
            mat4 other;
            SampleLocal(bone, prev, anim->ptime, other);
            BlendLocals(other, local[b], anim->bweight, local[b]);
        }
        if (BoneIsPosed(anim, b)) {
            mat4 delta, bind;
            PoseToMatrix(&(anim->overrides[b]), delta);
            M4Copy(bind, bone->localbind);
            glm_mat4_mul(bind, delta, local[b]);
        }
    }
    ForwardKinematics(sk, order, n, local, global);
    for (size_t b = 0; b < count; b++) M4Copy(anim->locals[b], local[b]);
    if (!anim->rest) {
        for (uint32_t c = 0; c < anim->numik && c < MAX_IK_CONSTRAINTS; c++) {
            IKConstraint ik = anim->ik[c];
            if (!ik.enabled || ik.influence <= 0.0f) continue;
            SolveIK(sk, &ik, order, n, local, global);
        }
    }
    for (size_t b = 0; b < count; b++) {
        M4Copy(anim->globals[b], global[b]);
        mat4 inv, p;
        M4Copy(inv, sk->bones[b].inversebind);
        if (anim->rest) {
            glm_mat4_identity(p);
        } else {
            glm_mat4_mul(global[b], inv, p);
        }
        M4Copy(pose[b], p);
    }
    mat4 identity = GLM_MAT4_IDENTITY_INIT;
    for (size_t b = count; b < MAX_BONES; b++) M4Copy(pose[b], identity);
}

static void DropKeyedOverrides(MeshAnimation* anim) {
    Animation* clip = CurrentClip(anim);
    if (!clip) return;
    for (size_t b = 0; b < anim->skeleton.bonecount; b++) {
        if (!BoneIsPosed(anim, b)) continue;
        if (ChannelHasKeys(FindChannel(clip, anim->skeleton.bones[b].name))) SetPosedBit(anim, b, FALSE);
    }
}

static char* CopyString(const char* s) {
    char* out = EZ_ALLOC(strlen(s) + 1, sizeof(char));
    strcpy(out, s);
    return out;
}

static void UniqueClipName(MeshAnimation* anim, const char* base, char out[256]) {
    snprintf(out, 256, "%s", base && base[0] ? base : "Action");
    for (int k = 1; k < 10000; k++) {
        BOOL taken = FALSE;
        for (size_t c = 0; c < anim->animations.size && !taken; c++)
            taken = strcmp(anim->animations.data[c].name, out) == 0;
        if (!taken) return;
        snprintf(out, 256, "%.240s.%03d", base && base[0] ? base : "Action", k);
    }
}

static void InsertVec3Key(ARRLIST_Vec3Key* keys, Vec3Key key) {
    for (size_t i = 0; i < keys->size; i++) {
        if (fabsf(keys->data[i].time - key.time) < KEY_TIME_EPSILON) {
            key.interp = keys->data[i].interp;
            keys->data[i] = key;
            return;
        }
        if (keys->data[i].time > key.time) {
            ARRLIST_Vec3Key_insert(keys, key, i);
            return;
        }
    }
    ARRLIST_Vec3Key_add(keys, key);
}

static void InsertQuatKey(ARRLIST_QuatKey* keys, QuatKey key) {
    for (size_t i = 0; i < keys->size; i++) {
        if (fabsf(keys->data[i].time - key.time) < KEY_TIME_EPSILON) {
            key.interp = keys->data[i].interp;
            keys->data[i] = key;
            return;
        }
        if (keys->data[i].time > key.time) {
            ARRLIST_QuatKey_insert(keys, key, i);
            return;
        }
    }
    ARRLIST_QuatKey_add(keys, key);
}

static void AddUniqueTime(float* times, size_t* count, float t) {
    for (size_t i = 0; i < *count; i++) if (fabsf(times[i] - t) < KEY_TIME_EPSILON) return;
    times[(*count)++] = t;
}

static int CompareFloat(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

void PoseIdentity(BonePose* pose) {
    memset(pose, 0, sizeof(BonePose));
    pose->rotation[3] = 1.0f;
    SETVEC3(pose->scale, 1.0f, 1.0f, 1.0f);
}

void PoseToMatrix(const BonePose* pose, mat4 out) {
    versor r;
    QuatCopy(r, pose->rotation);
    ComposeTRS(pose->location, r, pose->scale, out);
}

void PoseFromMatrix(mat4 m, BonePose* out) {
    versor r;
    DecomposeTRS(m, out->location, r, out->scale);
    QuatCopy(out->rotation, r);
}

BOOL BoneIsPosed(const MeshAnimation* anim, size_t bone) {
    if (bone >= MAX_BONES) return FALSE;
    return (anim->posed[bone / 32] >> (bone % 32)) & 1u;
}

void BoneBind(const MeshAnimation* anim, size_t bone, mat4 localbind, mat4 inversebind) {
    if (localbind) M4Copy(localbind, anim->skeleton.bones[bone].localbind);
    if (inversebind) M4Copy(inversebind, anim->skeleton.bones[bone].inversebind);
}

void BoneGlobal(const MeshAnimation* anim, size_t bone, mat4 out) {
    M4Copy(out, anim->globals[bone]);
}

void BoneLocal(const MeshAnimation* anim, size_t bone, mat4 out) {
    M4Copy(out, anim->locals[bone]);
}

void BoneHeadTail(const MeshAnimation* anim, size_t bone, vec3 head, vec3 tail) {
    mat4 g;
    M4Copy(g, anim->globals[bone]);
    vec3 t;
    glm_vec3_copy((float*)anim->skeleton.bones[bone].tail, t);
    M4Head(g, head);
    glm_mat4_mulv3(g, t, 1.0f, tail);
}

void BoneCurrentPose(MeshAnimation* anim, size_t bone, BonePose* out) {
    if (BoneIsPosed(anim, bone)) {
        BlobCopy(out, &(anim->overrides[bone]), sizeof(BonePose));
        return;
    }
    mat4 local, bind, inv, delta;
    M4Copy(local, anim->locals[bone]);
    M4Copy(bind, anim->skeleton.bones[bone].localbind);
    glm_mat4_inv(bind, inv);
    glm_mat4_mul(inv, local, delta);
    PoseFromMatrix(delta, out);
}

void BoneSetPose(MeshAnimation* anim, size_t bone, const BonePose* pose) {
    if (bone >= anim->skeleton.bonecount) return;
    BlobCopy(&(anim->overrides[bone]), pose, sizeof(BonePose));
    SetPosedBit(anim, bone, TRUE);
    anim->dirty = TRUE;
}

void BoneClearOverride(MeshAnimation* anim, size_t bone) {
    if (bone >= MAX_BONES) return;
    SetPosedBit(anim, bone, FALSE);
    anim->dirty = TRUE;
}

size_t SkeletonFindBone(const Skeleton* skeleton, const char* name) {
    for (size_t i = 0; i < skeleton->bonecount; i++)
        if (strcmp(skeleton->bones[i].name, name) == 0) return i;
    return (size_t)-1;
}

size_t SkeletonOrder(const Skeleton* skeleton, size_t* order) {
    size_t n = skeleton->bonecount;
    int depth[MAX_BONES];
    for (size_t b = 0; b < n; b++) depth[b] = -1;
    int maxdepth = 0;
    for (size_t b = 0; b < n; b++) {
        size_t chain[MAX_BONES];
        size_t len = 0;
        size_t cur = b;
        while (cur != (size_t)-1 && depth[cur] < 0 && len < n) {
            chain[len++] = cur;
            cur = ValidParent(skeleton, cur) ? skeleton->bones[cur].parent : (size_t)-1;
        }
        int base = (cur == (size_t)-1 || len >= n) ? -1 : depth[cur];
        for (size_t i = len; i > 0; i--) {
            base++;
            depth[chain[i - 1]] = base;
            if (base > maxdepth) maxdepth = base;
        }
    }
    size_t count = 0;
    for (int d = 0; d <= maxdepth; d++)
        for (size_t b = 0; b < n; b++)
            if (depth[b] == d) order[count++] = b;
    return count;
}

BOOL SkeletonIsDescendant(const Skeleton* skeleton, size_t bone, size_t ancestor) {
    size_t cur = bone;
    size_t steps = 0;
    while (cur != (size_t)-1 && cur < skeleton->bonecount && steps++ <= skeleton->bonecount) {
        if (cur == ancestor) return TRUE;
        cur = ValidParent(skeleton, cur) ? skeleton->bones[cur].parent : (size_t)-1;
    }
    return FALSE;
}

void SkeletonRestGlobals(const Skeleton* skeleton, mat4* out) {
    size_t order[MAX_BONES];
    size_t n = SkeletonOrder(skeleton, order);
    for (size_t i = 0; i < n; i++) {
        size_t b = order[i];
        mat4 local, parent, g;
        M4Copy(local, skeleton->bones[b].localbind);
        if (ValidParent(skeleton, b)) {
            M4Copy(parent, out[skeleton->bones[b].parent]);
            glm_mat4_mul(parent, local, g);
            M4Copy(out[b], g);
        } else {
            M4Copy(out[b], local);
        }
    }
}

void SkeletonFinalize(Skeleton* skeleton) {
    size_t n = skeleton->bonecount;
    if (n == 0) return;
    mat4 rest[MAX_BONES];
    SkeletonRestGlobals(skeleton, rest);
    vec3 mn = { FLT_MAX, FLT_MAX, FLT_MAX }, mx = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (size_t b = 0; b < n; b++) {
        vec3 h;
        M4Head(rest[b], h);
        glm_vec3_minv(h, mn, mn);
        glm_vec3_maxv(h, mx, mx);
    }
    float extent = glm_vec3_distance(mn, mx);
    float fallback = extent > 1e-6f ? extent * 0.05f : 0.1f;
    for (size_t b = 0; b < n; b++) {
        Bone* bone = &(skeleton->bones[b]);
        if (bone->flags & BONE_FLAG_INITIALIZED) continue;
        bone->flags |= BONE_FLAG_INITIALIZED | BONE_FLAG_DEFORM;
        vec3 sum = { 0, 0, 0 };
        size_t kids = 0;
        for (size_t c = 0; c < n; c++) {
            if (c == b || skeleton->bones[c].parent != b) continue;
            vec3 t = { skeleton->bones[c].localbind[3][0], skeleton->bones[c].localbind[3][1], skeleton->bones[c].localbind[3][2] };
            glm_vec3_add(sum, t, sum);
            kids++;
        }
        mat4 g;
        M4Copy(g, rest[b]);
        float scale = glm_vec3_norm(g[0]);
        if (scale < 1e-12f) scale = 1.0f;
        if (kids > 0) glm_vec3_scale(sum, 1.0f / (float)kids, sum);
        if (kids > 0 && glm_vec3_norm(sum) * scale > fallback * 0.05f) {
            glm_vec3_copy(sum, bone->tail);
            continue;
        }
        vec3 dir = { 0, 1, 0 };
        if (ValidParent(skeleton, b)) {
            vec3 ph, h, d;
            mat4 pg, inv;
            M4Copy(pg, rest[bone->parent]);
            M4Head(pg, ph);
            M4Head(g, h);
            glm_vec3_sub(h, ph, d);
            if (glm_vec3_norm(d) > 1e-9f) {
                glm_mat4_inv(g, inv);
                glm_mat4_mulv3(inv, d, 0.0f, dir);
            }
        }
        glm_vec3_normalize(dir);
        glm_vec3_scale(dir, fallback / scale, bone->tail);
    }
}

size_t RigOfMesh(size_t meshid) {
    if (meshid >= NumMeshes()) return (size_t)-1;
    MeshDescriptor* md = MeshReference(meshid);
    if (md->pose == (uint32_t)-1) return (size_t)-1;
    size_t a = md->pose / MAX_BONES;
    if (a >= NumAnimations()) return (size_t)-1;
    return a;
}

uint64_t KeyRevision() {
    return g_key_revision;
}

void KeysChanged() {
    g_key_revision++;
}

void AnimationMarkDirty(MeshAnimation* anim) {
    anim->dirty = TRUE;
}

void AnimationMarkAllDirty() {
    KeysChanged();
    for (size_t i = 0; i < NumAnimations(); i++) AnimationReference(i)->dirty = TRUE;
}

AnimationTimeline* Timeline() {
    AnimationTimeline* t = &(RendererGeometry()->timeline);
    if (!(t->fps > 0.0f)) {
        t->fps = TIMELINE_DEFAULT_FPS;
        t->start = 0.0f;
        t->end = TIMELINE_DEFAULT_END;
        t->frame = 0.0f;
        t->playing = FALSE;
        t->looping = TRUE;
        t->autokey = FALSE;
        t->speed = 1.0f;
    }
    if (!(t->speed > 0.0f)) t->speed = 1.0f;
    if (t->end < t->start) t->end = t->start;
    return t;
}

void TimelineSetFrame(float frame) {
    Timeline()->frame = frame;
}

void TimelineStep(float frames) {
    AnimationTimeline* t = Timeline();
    t->frame = roundf(t->frame) + frames;
    if (t->frame > t->end) t->frame = t->looping ? t->start : t->end;
    if (t->frame < t->start) t->frame = t->looping ? t->end : t->start;
}

void TimelineTogglePlay() {
    AnimationTimeline* t = Timeline();
    t->playing = !t->playing;
    if (t->playing && t->frame >= t->end && !t->looping) t->frame = t->start;
}

void TimelineSetFPS(float fps) {
    KeysChanged();
    if (!(fps > 0.0f)) return;
    AnimationTimeline* t = Timeline();
    t->fps = fps;
    for (size_t i = 0; i < NumAnimations(); i++) {
        MeshAnimation* a = AnimationReference(i);
        for (size_t c = 0; c < a->animations.size; c++)
            if (a->animations.data[c].mode == ANIMATION_AUTHORED) a->animations.data[c].tps = fps;
        a->dirty = TRUE;
    }
}

KeyInterpolation TimelineDefaultInterpolation() {
    return g_default_interp;
}

void TimelineSetDefaultInterpolation(KeyInterpolation interp) {
    if (interp < KEY_INTERP_COUNT) g_default_interp = interp;
}

const char** KeyInterpolationLabels() {
    return g_interp_labels;
}

float AnimationFrameToTicks(const Animation* clip, float frame) {
    if (!clip) return frame;
    float fps = Timeline()->fps;
    if (clip->mode == ANIMATION_AUTHORED) return frame;
    return frame / fps * clip->tps;
}

float AnimationTicksToFrame(const Animation* clip, float ticks) {
    if (!clip) return ticks;
    float fps = Timeline()->fps;
    if (clip->mode == ANIMATION_AUTHORED || !(clip->tps > 0.0f)) return ticks;
    return ticks / clip->tps * fps;
}

Animation* CurrentClip(MeshAnimation* anim) {
    if (anim->current >= anim->animations.size) return NULL;
    return &(anim->animations.data[anim->current]);
}

float AnimationClipTime(MeshAnimation* anim) {
    return anim->time;
}

float TimelineDT() {
    if (g_dt < 0) return GetFrameTime();
    return g_dt;
}

void OverrideTimelineDT(float dt) {
    g_dt = dt;
}

void AnimationEvaluateNow(size_t animid) {
    if (animid >= NumAnimations()) return;
    MeshAnimation* anim = AnimationReference(animid);
    Geometry* g = RendererGeometry();
    if ((animid + 1) * MAX_BONES > g->poses.size) return;
    EvaluateRig(anim, &(g->poses.data[animid * MAX_BONES]));
    anim->dirty = FALSE;
    anim->evaltime = anim->time;
    anim->evalclip = anim->current;
    UpdatePoses();
}

BOOL PlayAnimations(Geometry* geometry) {
    BOOL changes = FALSE;
    AnimationTimeline* tl = Timeline();
    float dt = TimelineDT();
    if (tl->playing) {
        tl->frame += dt * tl->fps * tl->speed;
        if (tl->frame > tl->end) {
            if (tl->looping && tl->end > tl->start) {
                tl->frame = tl->start + fmodf(tl->frame - tl->start, tl->end - tl->start);
            } else {
                tl->frame = tl->end;
                tl->playing = FALSE;
            }
        }
    }
    for (size_t i = 0; i < geometry->animations.size; i++) {
        MeshAnimation* animation = &(geometry->animations.data[i]);
        if ((i + 1) * MAX_BONES > geometry->poses.size) break;
        mat4* pose = &(geometry->poses.data[i * MAX_BONES]);
        if (!animation->enable) {
            if (animation->_enable) {
                animation->_enable = FALSE;
                mat4 identity = GLM_MAT4_IDENTITY_INIT;
                for (size_t j = 0; j < MAX_BONES; j++) M4Copy(pose[j], identity);
                changes = TRUE;
            }
            continue;
        }
        if (!animation->_enable) {
            animation->_enable = TRUE;
            animation->dirty = TRUE;
        }
        Animation* clip = CurrentClip(animation);
        BOOL ticking = FALSE;
        if (animation->synced) {
            animation->blending = FALSE;
            float t = clip ? AnimationFrameToTicks(clip, tl->frame) : 0.0f;
            if (clip && animation->looping && clip->duration > 0.0f) {
                t = fmodf(t, clip->duration);
                if (t < 0.0f) t += clip->duration;
            }
            animation->time = t;
        } else if (animation->playing && clip) {
            ticking = TRUE;
            animation->time += dt * clip->tps;
            if (animation->looping && clip->duration > 0.0f) animation->time = fmodf(animation->time, clip->duration);
            if (animation->blending) {
                Animation* prev = animation->previous < animation->animations.size ? &(animation->animations.data[animation->previous]) : NULL;
                if (prev && prev->duration > 0.0f) {
                    animation->ptime += dt * prev->tps;
                    animation->ptime = fmodf(animation->ptime, prev->duration);
                }
                animation->bweight += animation->bduration > 0.0f ? dt / animation->bduration : 1.0f;
                if (animation->bweight >= 1.0f || !prev) {
                    animation->bweight = 1.0f;
                    animation->blending = FALSE;
                }
            }
        }
        BOOL moved = animation->time != animation->evaltime || animation->current != animation->evalclip;
        if (!animation->dirty && !moved && !ticking) continue;
        if (moved) DropKeyedOverrides(animation);
        EvaluateRig(animation, pose);
        animation->dirty = FALSE;
        animation->evaltime = animation->time;
        animation->evalclip = animation->current;
        changes = TRUE;
    }
    return changes;
}

void ClipFreeChannels(Animation* clip) {
    for (size_t k = 0; k < clip->channels.size; k++) {
        ARRLIST_Vec3Key_clear(&(clip->channels.data[k].positions));
        ARRLIST_QuatKey_clear(&(clip->channels.data[k].rotations));
        ARRLIST_Vec3Key_clear(&(clip->channels.data[k].scales));
    }
    ARRLIST_BoneChannel_clear(&(clip->channels));
}

void ClipRecomputeDuration(Animation* clip) {
    float d = 0.0f;
    for (size_t k = 0; k < clip->channels.size; k++) {
        BoneChannel* ch = &(clip->channels.data[k]);
        if (ch->positions.size) d = fmaxf(d, ch->positions.data[ch->positions.size - 1].time);
        if (ch->rotations.size) d = fmaxf(d, ch->rotations.data[ch->rotations.size - 1].time);
        if (ch->scales.size) d = fmaxf(d, ch->scales.data[ch->scales.size - 1].time);
    }
    clip->duration = d;
}

size_t ClipCreate(MeshAnimation* anim, const char* name) {
    KeysChanged();
    Animation clip = { 0 };
    UniqueClipName(anim, name, clip.name);
    clip.tps = Timeline()->fps;
    clip.duration = 0.0f;
    clip.mode = ANIMATION_AUTHORED;
    ARRLIST_Animation_add(&(anim->animations), clip);
    ARRLIST_DynamicString_add(&(anim->names), CopyString(clip.name));
    anim->current = anim->animations.size - 1;
    anim->blending = FALSE;
    anim->dirty = TRUE;
    return anim->current;
}

size_t ClipDuplicate(MeshAnimation* anim, size_t index) {
    KeysChanged();
    if (index >= anim->animations.size) return (size_t)-1;
    Animation src = anim->animations.data[index];
    Animation clip = src;
    clip.channels = (ARRLIST_BoneChannel){ 0 };
    UniqueClipName(anim, src.name, clip.name);
    for (size_t k = 0; k < src.channels.size; k++) {
        BoneChannel ch = { 0 };
        BoneChannel* s = &(src.channels.data[k]);
        memcpy(ch.name, s->name, sizeof(ch.name));
        for (size_t i = 0; i < s->positions.size; i++) ARRLIST_Vec3Key_add(&ch.positions, s->positions.data[i]);
        for (size_t i = 0; i < s->rotations.size; i++) ARRLIST_QuatKey_add(&ch.rotations, s->rotations.data[i]);
        for (size_t i = 0; i < s->scales.size; i++) ARRLIST_Vec3Key_add(&ch.scales, s->scales.data[i]);
        ARRLIST_BoneChannel_add(&clip.channels, ch);
    }
    ARRLIST_Animation_add(&(anim->animations), clip);
    ARRLIST_DynamicString_add(&(anim->names), CopyString(clip.name));
    anim->current = anim->animations.size - 1;
    anim->blending = FALSE;
    anim->dirty = TRUE;
    return anim->current;
}

void ClipDelete(MeshAnimation* anim, size_t index) {
    KeysChanged();
    if (index >= anim->animations.size) return;
    ClipFreeChannels(&(anim->animations.data[index]));
    ARRLIST_Animation_remove(&(anim->animations), index);
    if (index < anim->names.size) {
        EZ_FREE(anim->names.data[index]);
        ARRLIST_DynamicString_remove(&(anim->names), index);
    }
    if (anim->current > index || anim->current >= anim->animations.size)
        anim->current = anim->current > 0 ? anim->current - 1 : 0;
    anim->previous = anim->current;
    anim->blending = FALSE;
    anim->dirty = TRUE;
}

void ClipRename(MeshAnimation* anim, size_t index, const char* name) {
    KeysChanged();
    if (index >= anim->animations.size || !name || !name[0]) return;
    Animation* clip = &(anim->animations.data[index]);
    if (strcmp(clip->name, name) == 0) return;
    char unique[256];
    UniqueClipName(anim, name, unique);
    memcpy(clip->name, unique, sizeof(unique));
    if (index < anim->names.size) {
        EZ_FREE(anim->names.data[index]);
        anim->names.data[index] = CopyString(unique);
    }
}

void ClipSelect(MeshAnimation* anim, size_t index) {
    if (index >= anim->animations.size) return;
    if (anim->synced) {
        anim->current = index;
        anim->blending = FALSE;
        anim->dirty = TRUE;
    } else {
        SwitchAnimation(anim, index);
    }
}

BOOL ClipMakeEditable(MeshAnimation* anim, size_t index) {
    KeysChanged();
    if (index >= anim->animations.size) return FALSE;
    Animation* clip = &(anim->animations.data[index]);
    if (clip->mode == ANIMATION_AUTHORED) return TRUE;
    float fps = Timeline()->fps;
    float tps = clip->tps > 0.0f ? clip->tps : fps;
    for (size_t k = 0; k < clip->channels.size; k++) {
        BoneChannel* ch = &(clip->channels.data[k]);
        size_t b = SkeletonFindBone(&(anim->skeleton), ch->name);
        size_t cap = ch->positions.size + ch->rotations.size + ch->scales.size + 1;
        float* times = EZ_ALLOC(cap, sizeof(float));
        size_t nt = 0;
        for (size_t i = 0; i < ch->positions.size; i++) AddUniqueTime(times, &nt, ch->positions.data[i].time);
        for (size_t i = 0; i < ch->rotations.size; i++) AddUniqueTime(times, &nt, ch->rotations.data[i].time);
        for (size_t i = 0; i < ch->scales.size; i++) AddUniqueTime(times, &nt, ch->scales.data[i].time);
        qsort(times, nt, sizeof(float), CompareFloat);
        ARRLIST_Vec3Key positions = { 0 }, scales = { 0 };
        ARRLIST_QuatKey rotations = { 0 };
        if (b != (size_t)-1) {
            const Bone* bone = &(anim->skeleton.bones[b]);
            mat4 bind, inv;
            M4Copy(bind, bone->localbind);
            glm_mat4_inv(bind, inv);
            for (size_t i = 0; i < nt; i++) {
                mat4 local, delta;
                BonePose pose;
                SampleImported(bone, ch, times[i], local);
                glm_mat4_mul(inv, local, delta);
                PoseFromMatrix(delta, &pose);
                float frame = times[i] / tps * fps;
                Vec3Key vk = { 0 };
                QuatKey qk = { 0 };
                vk.time = frame; vk.interp = KEY_INTERP_LINEAR;
                qk.time = frame; qk.interp = KEY_INTERP_LINEAR;
                glm_vec3_copy(pose.location, vk.value);
                ARRLIST_Vec3Key_add(&positions, vk);
                QuatCopy(qk.value, pose.rotation);
                ARRLIST_QuatKey_add(&rotations, qk);
                glm_vec3_copy(pose.scale, vk.value);
                ARRLIST_Vec3Key_add(&scales, vk);
            }
        }
        EZ_FREE(times);
        ARRLIST_Vec3Key_clear(&(ch->positions));
        ARRLIST_QuatKey_clear(&(ch->rotations));
        ARRLIST_Vec3Key_clear(&(ch->scales));
        ch->positions = positions;
        ch->rotations = rotations;
        ch->scales = scales;
    }
    clip->mode = ANIMATION_AUTHORED;
    clip->tps = fps;
    ClipRecomputeDuration(clip);
    anim->dirty = TRUE;
    loginfo("Converted clip \"%s\" into an editable (authored) clip", clip->name);
    return TRUE;
}

BoneChannel* ClipChannel(Animation* clip, const char* name, BOOL create) {
    BoneChannel* ch = FindChannel(clip, name);
    if (ch || !create) return ch;
    BoneChannel fresh = { 0 };
    strncpy(fresh.name, name, sizeof(fresh.name) - 1);
    ARRLIST_BoneChannel_add(&(clip->channels), fresh);
    return &(clip->channels.data[clip->channels.size - 1]);
}

void ChannelRenameAll(MeshAnimation* anim, const char* oldname, const char* newname) {
    KeysChanged();
    for (size_t c = 0; c < anim->animations.size; c++) {
        BoneChannel* ch = FindChannel(&(anim->animations.data[c]), oldname);
        if (ch) {
            memset(ch->name, 0, sizeof(ch->name));
            strncpy(ch->name, newname, sizeof(ch->name) - 1);
        }
    }
}

void ChannelRemoveAll(MeshAnimation* anim, const char* name) {
    KeysChanged();
    for (size_t c = 0; c < anim->animations.size; c++) {
        Animation* clip = &(anim->animations.data[c]);
        for (size_t k = 0; k < clip->channels.size; k++) {
            if (strcmp(clip->channels.data[k].name, name) != 0) continue;
            ARRLIST_Vec3Key_clear(&(clip->channels.data[k].positions));
            ARRLIST_QuatKey_clear(&(clip->channels.data[k].rotations));
            ARRLIST_Vec3Key_clear(&(clip->channels.data[k].scales));
            ARRLIST_BoneChannel_remove(&(clip->channels), k);
            break;
        }
        ClipRecomputeDuration(clip);
    }
}

void KeyInsert(MeshAnimation* anim, size_t bone, float frame, uint32_t channels) {
    KeysChanged();
    if (bone >= anim->skeleton.bonecount || channels == 0) return;
    if (anim->dirty) {
        for (size_t i = 0; i < NumAnimations(); i++)
            if (AnimationReference(i) == anim) { AnimationEvaluateNow(i); break; }
    }
    Animation* clip = CurrentClip(anim);
    if (!clip) {
        ClipCreate(anim, "Action");
        clip = CurrentClip(anim);
        anim->synced = TRUE;
    }
    if (clip->mode != ANIMATION_AUTHORED) {
        ClipMakeEditable(anim, anim->current);
        clip = CurrentClip(anim);
    }
    BonePose pose;
    BoneCurrentPose(anim, bone, &pose);
    float ticks = roundf(AnimationFrameToTicks(clip, frame) * 1000.0f) / 1000.0f;
    BoneChannel* ch = ClipChannel(clip, anim->skeleton.bones[bone].name, TRUE);
    Vec3Key vk = { 0 };
    QuatKey qk = { 0 };
    vk.time = ticks; vk.interp = g_default_interp;
    qk.time = ticks; qk.interp = g_default_interp;
    if (channels & KEY_CHANNEL_LOCATION) {
        glm_vec3_copy(pose.location, vk.value);
        InsertVec3Key(&(ch->positions), vk);
    }
    if (channels & KEY_CHANNEL_ROTATION) {
        versor q;
        QuatCopy(q, pose.rotation);
        size_t before = (size_t)-1;
        for (size_t i = 0; i < ch->rotations.size; i++)
            if (ch->rotations.data[i].time < ticks - KEY_TIME_EPSILON) before = i;
        if (before != (size_t)-1) {
            versor prev;
            QuatCopy(prev, ch->rotations.data[before].value);
            if (glm_quat_dot(prev, q) < 0.0f) glm_vec4_negate(q);
        }
        QuatCopy(qk.value, q);
        InsertQuatKey(&(ch->rotations), qk);
    }
    if (channels & KEY_CHANNEL_SCALE) {
        glm_vec3_copy(pose.scale, vk.value);
        InsertVec3Key(&(ch->scales), vk);
    }
    if (channels == KEY_CHANNEL_ALL) {
        SetPosedBit(anim, bone, FALSE);
    } else {
        BlobCopy(&(anim->overrides[bone]), &pose, sizeof(BonePose));
        SetPosedBit(anim, bone, TRUE);
    }
    ClipRecomputeDuration(clip);
    anim->dirty = TRUE;
}

void KeyDelete(Animation* clip, const char* bone, float ticks) {
    KeysChanged();
    if (!clip) return;
    for (size_t k = 0; k < clip->channels.size; k++) {
        BoneChannel* ch = &(clip->channels.data[k]);
        if (bone && strcmp(ch->name, bone) != 0) continue;
        REMOVE_KEYS_AT(ch->positions, Vec3Key, ticks);
        REMOVE_KEYS_AT(ch->rotations, QuatKey, ticks);
        REMOVE_KEYS_AT(ch->scales, Vec3Key, ticks);
    }
    ClipRecomputeDuration(clip);
}

BOOL KeyExists(Animation* clip, const char* bone, float ticks) {
    if (!clip) return FALSE;
    for (size_t k = 0; k < clip->channels.size; k++) {
        BoneChannel* ch = &(clip->channels.data[k]);
        if (bone && strcmp(ch->name, bone) != 0) continue;
        for (size_t i = 0; i < ch->positions.size; i++) if (fabsf(ch->positions.data[i].time - ticks) < KEY_TIME_EPSILON) return TRUE;
        for (size_t i = 0; i < ch->rotations.size; i++) if (fabsf(ch->rotations.data[i].time - ticks) < KEY_TIME_EPSILON) return TRUE;
        for (size_t i = 0; i < ch->scales.size; i++) if (fabsf(ch->scales.data[i].time - ticks) < KEY_TIME_EPSILON) return TRUE;
    }
    return FALSE;
}

size_t KeyTimes(Animation* clip, const char* bone, float* out, size_t max) {
    if (!clip || max == 0) return 0;
    size_t total = 0;
    for (size_t k = 0; k < clip->channels.size; k++) {
        BoneChannel* ch = &(clip->channels.data[k]);
        if (bone && strcmp(ch->name, bone) != 0) continue;
        total += ch->positions.size + ch->rotations.size + ch->scales.size;
    }
    if (total == 0) return 0;
    float* all = EZ_ALLOC(total, sizeof(float));
    size_t n = 0;
    for (size_t k = 0; k < clip->channels.size; k++) {
        BoneChannel* ch = &(clip->channels.data[k]);
        if (bone && strcmp(ch->name, bone) != 0) continue;
        for (size_t i = 0; i < ch->positions.size; i++) all[n++] = ch->positions.data[i].time;
        for (size_t i = 0; i < ch->rotations.size; i++) all[n++] = ch->rotations.data[i].time;
        for (size_t i = 0; i < ch->scales.size; i++) all[n++] = ch->scales.data[i].time;
    }
    qsort(all, n, sizeof(float), CompareFloat);
    size_t count = 0;
    for (size_t i = 0; i < n && count < max; i++)
        if (count == 0 || fabsf(all[i] - out[count - 1]) >= KEY_TIME_EPSILON) out[count++] = all[i];
    EZ_FREE(all);
    return count;
}

void KeyMove(Animation* clip, const char* bone, float from, float to) {
    KeysChanged();
    if (!clip || fabsf(from - to) < KEY_TIME_EPSILON) return;
    for (size_t k = 0; k < clip->channels.size; k++) {
        BoneChannel* ch = &(clip->channels.data[k]);
        if (bone && strcmp(ch->name, bone) != 0) continue;
        MOVE_KEYS(ch->positions, Vec3Key, from, to);
        MOVE_KEYS(ch->rotations, QuatKey, from, to);
        MOVE_KEYS(ch->scales, Vec3Key, from, to);
    }
    ClipRecomputeDuration(clip);
}

void KeySetInterpolation(Animation* clip, const char* bone, float ticks, KeyInterpolation interp) {
    KeysChanged();
    if (!clip || interp >= KEY_INTERP_COUNT) return;
    for (size_t k = 0; k < clip->channels.size; k++) {
        BoneChannel* ch = &(clip->channels.data[k]);
        if (bone && strcmp(ch->name, bone) != 0) continue;
        for (size_t i = 0; i < ch->positions.size; i++) if (fabsf(ch->positions.data[i].time - ticks) < KEY_TIME_EPSILON) ch->positions.data[i].interp = interp;
        for (size_t i = 0; i < ch->rotations.size; i++) if (fabsf(ch->rotations.data[i].time - ticks) < KEY_TIME_EPSILON) ch->rotations.data[i].interp = interp;
        for (size_t i = 0; i < ch->scales.size; i++) if (fabsf(ch->scales.data[i].time - ticks) < KEY_TIME_EPSILON) ch->scales.data[i].interp = interp;
    }
}

KeyInterpolation KeyGetInterpolation(Animation* clip, const char* bone, float ticks) {
    if (!clip) return g_default_interp;
    for (size_t k = 0; k < clip->channels.size; k++) {
        BoneChannel* ch = &(clip->channels.data[k]);
        if (bone && strcmp(ch->name, bone) != 0) continue;
        for (size_t i = 0; i < ch->rotations.size; i++) if (fabsf(ch->rotations.data[i].time - ticks) < KEY_TIME_EPSILON) return (KeyInterpolation)ch->rotations.data[i].interp;
        for (size_t i = 0; i < ch->positions.size; i++) if (fabsf(ch->positions.data[i].time - ticks) < KEY_TIME_EPSILON) return (KeyInterpolation)ch->positions.data[i].interp;
        for (size_t i = 0; i < ch->scales.size; i++) if (fabsf(ch->scales.data[i].time - ticks) < KEY_TIME_EPSILON) return (KeyInterpolation)ch->scales.data[i].interp;
    }
    return g_default_interp;
}

BOOL KeyNeighbour(MeshAnimation* anim, float frame, int dir, float* out) {
    Animation* clip = CurrentClip(anim);
    if (!clip) return FALSE;
    static float times[8192];
    size_t n = KeyTimes(clip, NULL, times, 8192);
    BOOL found = FALSE;
    float best = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float f = AnimationTicksToFrame(clip, times[i]);
        if (dir > 0 && f > frame + KEY_TIME_EPSILON && (!found || f < best)) { best = f; found = TRUE; }
        if (dir < 0 && f < frame - KEY_TIME_EPSILON && (!found || f > best)) { best = f; found = TRUE; }
    }
    if (found) *out = best;
    return found;
}
