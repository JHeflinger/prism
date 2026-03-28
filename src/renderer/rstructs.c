#include "rstructs.h"
#include <easyhash.h>

uint64_t hash_edge(Edge edge) {
    return ez_hash_uint64_t(((uint64_t)edge.a << 32) | edge.b);
}

IMPL_ARRLIST(Edge);
IMPL_ARRLIST(TriangleID);
IMPL_ARRLIST(Triangle);
IMPL_ARRLIST(SurfaceMaterial);
IMPL_ARRLIST(SceneLight);
IMPL_ARRLIST(ManifoldVertex);
IMPL_ARRLIST(ManifoldEdge);
IMPL_ARRLIST(ManifoldFace);
IMPL_ARRLIST(ManifoldHalfEdge);
IMPL_ARRLIST(FluidSource);
IMPL_ARRLIST(FluidForce);
IMPL_ARR_ARRLIST(vec4);
IMPL_ARR_ARRLIST(vec3);
IMPL_HASHMAP(VertexID, BOOL, Locks, ez_hash_uint32_t);
IMPL_HASHMAP(Edge, EdgeMeta, EdgeGlue, hash_edge);
IMPL_ARRLIST(MeshDescriptor);
