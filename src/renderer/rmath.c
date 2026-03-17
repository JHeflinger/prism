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
    // Müller et al. 2016 - iterative polar decomposition
    // More robust than Jacobi SVD for near-degenerate covariance matrices
    mat3 R;
    glm_mat3_identity(R);

    for (int iter = 0; iter < 20; iter++) {
        // Compute R^T
        mat3 Rt;
        glm_mat3_transpose_to(R, Rt);

        // Compute columns of R cross products for the adjugate
        vec3 col0, col1, col2, c01, c12, c20;
        glm_vec3_copy(R[0], col0);
        glm_vec3_copy(R[1], col1);
        glm_vec3_copy(R[2], col2);
        glm_vec3_cross(col1, col2, c01);
        glm_vec3_cross(col2, col0, c12);
        glm_vec3_cross(col0, col1, c20);

        float det = glm_vec3_dot(col0, c01);

        // If det is near zero the matrix is degenerate — return identity
        if (fabsf(det) < 1e-12f) {
            glm_mat3_copy(C, R_out);
            glm_mat3_identity(R_out);
            return;
        }

        float inv_det = 1.0f / det;

        // adjugate(R)^T / det = R^{-T}
        mat3 invT;
        invT[0][0] = c01[0] * inv_det;
        invT[0][1] = c01[1] * inv_det;
        invT[0][2] = c01[2] * inv_det;
        invT[1][0] = c12[0] * inv_det;
        invT[1][1] = c12[1] * inv_det;
        invT[1][2] = c12[2] * inv_det;
        invT[2][0] = c20[0] * inv_det;
        invT[2][1] = c20[1] * inv_det;
        invT[2][2] = c20[2] * inv_det;

        // R = (R + R^{-T}) / 2, pre-multiplied by C
        // First converge R to orthogonal, then apply to C
        mat3 R_new;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                R_new[i][j] = 0.5f * (R[i][j] + invT[i][j]);

        // Check convergence
        float diff = 0.0f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                float d = R_new[i][j] - R[i][j];
                diff += d * d;
            }
        glm_mat3_copy(R_new, R);
        if (diff < 1e-12f) break;
    }

    // R is now the orthogonal polar factor of the identity — 
    // we want the polar factor of C, so compute R = polar(C)
    // by running the same iteration initialized with C instead
    glm_mat3_copy(C, R);
    for (int iter = 0; iter < 40; iter++) {
        vec3 col0, col1, col2, c01, c12, c20;
        glm_vec3_copy(R[0], col0);
        glm_vec3_copy(R[1], col1);
        glm_vec3_copy(R[2], col2);
        glm_vec3_cross(col1, col2, c01);
        glm_vec3_cross(col2, col0, c12);
        glm_vec3_cross(col0, col1, c20);
        float det = glm_vec3_dot(col0, c01);
        if (fabsf(det) < 1e-12f) { glm_mat3_identity(R); break; }
        float inv_det = 1.0f / det;
        mat3 invT;
        invT[0][0]=c01[0]*inv_det; invT[0][1]=c01[1]*inv_det; invT[0][2]=c01[2]*inv_det;
        invT[1][0]=c12[0]*inv_det; invT[1][1]=c12[1]*inv_det; invT[1][2]=c12[2]*inv_det;
        invT[2][0]=c20[0]*inv_det; invT[2][1]=c20[1]*inv_det; invT[2][2]=c20[2]*inv_det;
        mat3 R_new;
        float diff = 0.0f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                R_new[i][j] = 0.5f * (R[i][j] + invT[i][j]);
                float d = R_new[i][j] - R[i][j];
                diff += d * d;
            }
        glm_mat3_copy(R_new, R);
        if (diff < 1e-12f) break;
    }

    // Fix reflection
    if (glm_mat3_det(R) < 0.0f)
        for (int i = 0; i < 3; i++) R[i][2] *= -1.0f;

    glm_mat3_copy(R, R_out);
}
