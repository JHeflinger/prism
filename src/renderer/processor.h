#ifndef PROCESSOR_H
#define PROCESSOR_H

#include "renderer/rstructs.h"

void CleanManifoldMesh(ManifoldMesh* manifold);

ManifoldMesh GenerateManifoldMesh(const ARRLIST_vec4 vertices, const ARRLIST_vec4 normals, const ARRLIST_Triangle triangles);

BOOL IsManifoldValid(const ManifoldMesh* manifold);

void EdgeFlip(ManifoldMesh* manifold, uint32_t edge);

void EdgeSplit(ManifoldMesh* manifold, uint32_t edge);

void EdgeCollapse(ManifoldMesh* manifold, uint32_t edge);

void DirectedEdgeCollapse(ManifoldMesh* manifold, uint32_t edge, vec3 position);

void SaveManifoldOBJ(const char* path, ManifoldMesh* manifold);

void ReformatFromManifold(Geometry* geometry);

void SerialSubdivide(ManifoldMesh* manifold);

void SerialSimplify(ManifoldMesh* manifold, size_t reduction);

void SerialFilter(ManifoldMesh* manifold, float smoothing);

#endif
