#include "rmath.h"

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
