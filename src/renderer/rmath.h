#ifndef RMATH_H
#define RMATH_H

#include "renderer/rstructs.h"

#define SETVEC3(v, x, y, z) {v[0] = x; v[1] = y; v[2] = z;}

float TriangleArea(vec3 a, vec3 b, vec3 c);

void Mat3Add(mat3 a, mat3 b, mat3 dest);

void Mat4Add(mat4 a, mat4 b, mat4 dest);

void CameraUVW(SimpleCamera camera, vec3 u, vec3 v, vec3 w);

void PolarDecompose(mat3 C, mat3 R_out);

#endif
