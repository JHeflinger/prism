#ifndef RMATH_H
#define RMATH_H

#include "renderer/rstructs.h"

#define SETVEC3(v, x, y, z) {v[0] = x; v[1] = y; v[2] = z;}
#define SETVEC(v1, v2) {v1[0] = v2[0]; v1[1] = v2[1]; v1[2] = v2[2];}

float TriangleArea(vec3 a, vec3 b, vec3 c);

void Mat4Add(mat4 a, mat4 b, mat4 dest);

void CameraUVW(SimpleCamera camera, vec3 u, vec3 v, vec3 w);

#endif
