#include "rstructs.h"
#include <easyhash.h>

IMPL_ARRLIST(TriangleID);
IMPL_ARRLIST(Triangle);
IMPL_ARRLIST(SurfaceMaterial);
IMPL_ARRLIST(SceneLight);
IMPL_ARRLIST(ManifoldVertex);
IMPL_ARRLIST(ManifoldEdge);
IMPL_ARRLIST(ManifoldFace);
IMPL_ARRLIST(ManifoldHalfEdge);
IMPL_ARR_ARRLIST(vec4);
IMPL_ARR_ARRLIST(vec3);
IMPL_HASHMAP(VertexID, BOOL, Locks, ez_hash_uint32_t);
