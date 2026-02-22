#include "rmath.h"

float TriangleArea(Vector3 av, Vector3 bv, Vector3 cv) {
    vec3 a = {av.x, av.y, av.z};
    vec3 b = {bv.x, bv.y, bv.z};
    vec3 c = {cv.x, cv.y, cv.z};
    vec3 b_a, c_a, tcross;
    glm_vec3_sub(b, a, b_a);
    glm_vec3_sub(c, a, c_a);
    glm_vec3_cross(b_a, c_a, tcross);
    return glm_vec3_norm(tcross) * 0.5f;
}
