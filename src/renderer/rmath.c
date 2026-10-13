#include "rmath.h"

#define VIEW_NEAR_DEPTH 1e-3f

float TriangleArea(vec3 a, vec3 b, vec3 c) {
    vec3 b_a, c_a, tcross;
    glm_vec3_sub(b, a, b_a);
    glm_vec3_sub(c, a, c_a);
    glm_vec3_cross(b_a, c_a, tcross);
    return glm_vec3_norm(tcross) * 0.5f;
}

void Mat3Add(mat3 a, mat3 b, mat3 dest) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            dest[i][j] = a[i][j] + b[i][j];
}

void Mat4Add(mat4 a, mat4 b, mat4 dest) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            dest[i][j] = a[i][j] + b[i][j];
}

void CameraUVW(SimpleCamera camera, vec3 u, vec3 v, vec3 w) {
    glm_vec3_sub(camera.look, camera.position, camera.look);
    glm_vec3_normalize(camera.up);
    glm_vec3_normalize(camera.look);
    glm_vec3_negate_to(camera.look, w);
    glm_vec3_crossn(camera.up, w, u);
    glm_vec3_crossn(w, u, v);
}

void PolarDecompose(mat3 C, mat3 R_out) {
    mat3 Ct, CtC;
    glm_mat3_transpose_to(C, Ct);
    glm_mat3_mul(Ct, C, CtC);
    mat3 V;
    glm_mat3_identity(V);
    mat3 A;
    glm_mat3_copy(CtC, A);
    for (int sweep = 0; sweep < 20; sweep++) {
        float off = A[0][1]*A[0][1] + A[0][2]*A[0][2] + A[1][2]*A[1][2];
        if (off < 1e-24f) break;
        int pairs[3][2] = {{0,1},{0,2},{1,2}};
        for (int p = 0; p < 3; p++) {
            int r = pairs[p][0], q = pairs[p][1];
            if (fabsf(A[q][r]) < 1e-15f) continue;
            float tau = (A[q][q] - A[r][r]) / (2.f * A[q][r]);
            float t = (tau >= 0.f)
                ? 1.f / (tau + sqrtf(1.f + tau*tau))
                : 1.f / (tau - sqrtf(1.f + tau*tau));
            float c = 1.f / sqrtf(1.f + t*t);
            float s = t * c;
            float Arr = A[r][r], Aqq = A[q][q], Arq = A[q][r];
            A[r][r] = Arr - t*Arq;
            A[q][q] = Aqq + t*Arq;
            A[q][r] = 0.f; A[r][q] = 0.f;
            for (int i = 0; i < 3; i++) {
                if (i == r || i == q) continue;
                float Air = A[i < r ? r : i][i < r ? i : r];
                float Aiq = A[i < q ? q : i][i < q ? i : q];
                if (i < r) { A[r][i] = c*Air - s*Aiq; A[i][r] = A[r][i]; }
                else { A[i][r] = c*Air - s*Aiq; A[r][i] = A[i][r]; }
                if (i < q) { A[q][i] = s*Air + c*Aiq; A[i][q] = A[q][i]; }
                else { A[i][q] = s*Air + c*Aiq; A[q][i] = A[i][q]; }
            }
            for (int i = 0; i < 3; i++) {
                float Vir = V[r][i], Viq = V[q][i];
                V[r][i] = c*Vir - s*Viq;
                V[q][i] = s*Vir + c*Viq;
            }
        }
    }
    float sv[3] = { sqrtf(fmaxf(A[0][0], 0.f)),
                    sqrtf(fmaxf(A[1][1], 0.f)),
                    sqrtf(fmaxf(A[2][2], 0.f)) };
    mat3 U;
    for (int i = 0; i < 3; i++) {
        vec3 vi = { V[i][0], V[i][1], V[i][2] };
        vec3 Cvi;
        glm_mat3_mulv(C, vi, Cvi);
        float sigma = sv[i];
        if (sigma > 1e-8f) {
            U[i][0] = Cvi[0] / sigma;
            U[i][1] = Cvi[1] / sigma;
            U[i][2] = Cvi[2] / sigma;
        } else {
            vec3 fallback = {0.f, 0.f, 0.f};
            fallback[i] = 1.f;
            glm_vec3_copy(fallback, U[i]);
        }
    }
    for (int col = 0; col < 3; col++)
        for (int row = 0; row < 3; row++) {
            R_out[col][row] = 0.f;
            for (int k = 0; k < 3; k++)
                R_out[col][row] += U[k][row] * V[k][col];
        }
    if (glm_mat3_det(R_out) < 0.f) {
        int mini = (sv[0] < sv[1]) ? ((sv[0] < sv[2]) ? 0 : 2)
                                    : ((sv[1] < sv[2]) ? 1 : 2);
        U[mini][0] *= -1.f; U[mini][1] *= -1.f; U[mini][2] *= -1.f;
        for (int col = 0; col < 3; col++)
            for (int row = 0; row < 3; row++) {
                R_out[col][row] = 0.f;
                for (int k = 0; k < 3; k++)
                    R_out[col][row] += U[k][row] * V[k][col];
            }
    }
}

AxisAlignedBoundingBox SplatBounds(GaussianSplat* splat) {
    float s[3] = {
        expf(splat->logscale[0]),
        expf(splat->logscale[1]),
        expf(splat->logscale[2])
    };
    float w = splat->rotation[0];
    float x = splat->rotation[1];
    float y = splat->rotation[2];
    float z = splat->rotation[3];
    float len = sqrtf(x*x + y*y + z*z + w*w);
    if (len > 0.0f) {
        float inv = 1.0f / len;
        x *= inv; y *= inv; z *= inv; w *= inv;
    } else {
        x = y = z = 0.0f;
        w = 1.0f;
    }
    float R[3][3] = {
        { 1.0f - 2.0f*(y*y + z*z), 2.0f*(x*y - z*w), 2.0f*(x*z + y*w) },
        { 2.0f*(x*y + z*w), 1.0f - 2.0f*(x*x + z*z), 2.0f*(y*z - x*w) },
        { 2.0f*(x*z - y*w), 2.0f*(y*z + x*w), 1.0f - 2.0f*(x*x + y*y) }
    };
    AxisAlignedBoundingBox box;
    float k = 3.0f;
    for (int i = 0; i < 3; i++) {
        float a = R[i][0] * s[0];
        float b = R[i][1] * s[1];
        float c = R[i][2] * s[2];
        float extent = k * sqrtf(a*a + b*b + c*c);
        box.min[i] = splat->position[i] - extent;
        box.max[i] = splat->position[i] + extent;
    }
    return box;
}

void QuaternionFromBasis(const vec3 c0, const vec3 c1, const vec3 c2, versor q) {
    float m00 = c0[0], m10 = c0[1], m20 = c0[2];
    float m01 = c1[0], m11 = c1[1], m21 = c1[2];
    float m02 = c2[0], m12 = c2[1], m22 = c2[2];
    float tr = m00 + m11 + m22;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        q[0] = 0.25f * s; q[1] = (m21 - m12) / s; q[2] = (m02 - m20) / s; q[3] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
        q[0] = (m21 - m12) / s; q[1] = 0.25f * s; q[2] = (m01 + m10) / s; q[3] = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
        q[0] = (m02 - m20) / s; q[1] = (m01 + m10) / s; q[2] = 0.25f * s; q[3] = (m12 + m21) / s;
    } else {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
        q[0] = (m10 - m01) / s; q[1] = (m02 + m20) / s; q[2] = (m12 + m21) / s; q[3] = 0.25f * s;
    }
}

ViewProjection ViewProjectionFrom(SimpleCamera camera, float render_w, float render_h, float panel_w, float panel_h) {
    ViewProjection vp = { 0 };
    glm_vec3_copy(camera.position, vp.position);
    CameraUVW(camera, vp.u, vp.v, vp.w);
    vp.width = panel_w > 1.0f ? panel_w : 1.0f;
    vp.height = panel_h > 1.0f ? panel_h : 1.0f;
    float diffw = render_w / vp.width;
    float diffh = render_h / vp.height;
    float crop_w = diffw < diffh ? render_w : render_h * (vp.width / vp.height);
    float scale = crop_w > 0.0f ? vp.width / crop_w : 1.0f;
    float half_fov = glm_rad(camera.fov * 0.5f);
    float d = (cosf(half_fov) / sinf(half_fov)) * (render_w * 0.5f);
    vp.focal = d * scale;
    vp.ortho = camera.ortho;
    float orthosize = camera.orthosize > 1e-4f ? camera.orthosize : 1e-4f;
    vp.orthoscale = (render_h / orthosize) * scale;
    return vp;
}

BOOL ViewProject(const ViewProjection* vp, const vec3 world, vec2 out, float* depth) {
    vec3 rel;
    glm_vec3_sub((float*)world, (float*)vp->position, rel);
    float z = -glm_vec3_dot(rel, (float*)vp->w);
    if (z < VIEW_NEAR_DEPTH) return FALSE;
    float x = glm_vec3_dot(rel, (float*)vp->u);
    float y = glm_vec3_dot(rel, (float*)vp->v);
    if (vp->ortho) {
        out[0] = vp->width * 0.5f + vp->orthoscale * x;
        out[1] = vp->height * 0.5f - vp->orthoscale * y;
    } else {
        out[0] = vp->width * 0.5f + vp->focal * x / z;
        out[1] = vp->height * 0.5f - vp->focal * y / z;
    }
    if (depth) *depth = z;
    return TRUE;
}

void ViewRay(const ViewProjection* vp, const vec2 panel, vec3 origin, vec3 direction) {
    float x = panel[0] - vp->width * 0.5f;
    float y = -(panel[1] - vp->height * 0.5f);
    vec3 ux, vy, wd;
    if (vp->ortho) {
        float inv = 1.0f / (vp->orthoscale > 1e-6f ? vp->orthoscale : 1e-6f);
        glm_vec3_scale((float*)vp->u, x * inv, ux);
        glm_vec3_scale((float*)vp->v, y * inv, vy);
        glm_vec3_add(ux, vy, origin);
        glm_vec3_add(origin, (float*)vp->position, origin);
        glm_vec3_negate_to((float*)vp->w, direction);
        return;
    }
    glm_vec3_scale((float*)vp->u, x, ux);
    glm_vec3_scale((float*)vp->v, y, vy);
    glm_vec3_scale((float*)vp->w, -vp->focal, wd);
    glm_vec3_add(ux, vy, direction);
    glm_vec3_add(direction, wd, direction);
    glm_vec3_normalize(direction);
    glm_vec3_copy((float*)vp->position, origin);
}

void ViewForward(const ViewProjection* vp, vec3 out) {
    glm_vec3_negate_to((float*)vp->w, out);
}

float ViewWorldPerPixel(const ViewProjection* vp, const vec3 world) {
    vec3 rel;
    glm_vec3_sub((float*)world, (float*)vp->position, rel);
    float z = -glm_vec3_dot(rel, (float*)vp->w);
    if (vp->ortho) return 1.0f / (vp->orthoscale > 1e-6f ? vp->orthoscale : 1e-6f);
    if (z < VIEW_NEAR_DEPTH) z = VIEW_NEAR_DEPTH;
    return z / (vp->focal > 1e-6f ? vp->focal : 1e-6f);
}

float WrapPi(float a) {
    while (a > GLM_PIf) a -= 2.0f * GLM_PIf;
    while (a < -GLM_PIf) a += 2.0f * GLM_PIf;
    return a;
}

float SnapTo(float value, float step) {
    if (step <= 1e-9f) return value;
    return roundf(value / step) * step;
}

float Dist2D(const vec2 a, const vec2 b) {
    return hypotf(a[0] - b[0], a[1] - b[1]);
}

float DistToSegment2D(const vec2 p, const vec2 a, const vec2 b) {
    float abx = b[0] - a[0], aby = b[1] - a[1];
    float len2 = abx * abx + aby * aby;
    float t = 0.0f;
    if (len2 > 1e-9f) {
        t = ((p[0] - a[0]) * abx + (p[1] - a[1]) * aby) / len2;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }
    float cx = a[0] + abx * t, cy = a[1] + aby * t;
    float dx = p[0] - cx, dy = p[1] - cy;
    return sqrtf(dx * dx + dy * dy);
}

float Cross2D(const vec2 o, const vec2 a, const vec2 b) {
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
}

BOOL PointInQuad(const vec2 p, const vec2 q[4]) {
    BOOL has_pos = FALSE, has_neg = FALSE;
    for (int i = 0; i < 4; i++) {
        float c = Cross2D(q[i], q[(i + 1) % 4], p);
        if (c > 0.0f) has_pos = TRUE;
        if (c < 0.0f) has_neg = TRUE;
    }
    return !(has_pos && has_neg);
}

float Wrap180(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}
