#include "rmath.h"

float TriangleArea(vec3 a, vec3 b, vec3 c) {
    vec3 b_a, c_a, tcross;
    glm_vec3_sub(b, a, b_a);
    glm_vec3_sub(c, a, c_a);
    glm_vec3_cross(b_a, c_a, tcross);
    return glm_vec3_norm(tcross) * 0.5f;
}

void Mat4Add(mat4 a, mat4 b, mat4 dest) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            dest[i][j] = a[i][j] + b[i][j];
}
