#ifndef RMATH_H
#define RMATH_H

#include "renderer/rstructs.h"

#define SETVEC3(v, x, y, z) {v[0] = x; v[1] = y; v[2] = z;}
#define CLAMP(x, min, max) (x < min ? min : (x > max ? max : x))
#define MIN(x, y) (x < y ? x : y)
#define MAX(x, y) (x > y ? x : y)
#define INLINEV3(v) { v[0], v[1], v[2] }

float TriangleArea(vec3 a, vec3 b, vec3 c);

void Mat3Add(mat3 a, mat3 b, mat3 dest);

void Mat4Add(mat4 a, mat4 b, mat4 dest);

void CameraUVW(SimpleCamera camera, vec3 u, vec3 v, vec3 w);

void PolarDecompose(mat3 C, mat3 R_out);

AxisAlignedBoundingBox SplatBounds(GaussianSplat* splat);

void QuaternionFromBasis(const vec3 c0, const vec3 c1, const vec3 c2, versor q);

ViewProjection ViewProjectionFrom(SimpleCamera camera, float render_w, float render_h, float panel_w, float panel_h);

BOOL ViewProject(const ViewProjection* vp, const vec3 world, vec2 out, float* depth);

void ViewRay(const ViewProjection* vp, const vec2 panel, vec3 origin, vec3 direction);

void ViewForward(const ViewProjection* vp, vec3 out);

float ViewWorldPerPixel(const ViewProjection* vp, const vec3 world);

float WrapPi(float a);

float SnapTo(float value, float step);

float Dist2D(const vec2 a, const vec2 b);

float DistToSegment2D(const vec2 p, const vec2 a, const vec2 b);

float Cross2D(const vec2 o, const vec2 a, const vec2 b);

BOOL PointInQuad(const vec2 p, const vec2 q[4]);

float Wrap180(float a);

#endif
