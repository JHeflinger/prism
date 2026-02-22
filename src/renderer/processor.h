#ifndef PROCESSOR_H
#define PROCESSOR_H

#include "renderer/rstructs.h"

ManifoldMesh GenerateManifoldMesh(const ARRLIST_Vector3 vertices, const ARRLIST_Vector3 normals, const ARRLIST_Triangle triangles);

BOOL IsManifoldValid(const ManifoldMesh* manifold);

#endif
