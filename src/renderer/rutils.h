#ifndef RUTILS_H
#define RUTILS_H

#include "renderer/rstructs.h"

DECLARE_ARRLIST(size_t);

void RUTIL_BoundingVolumeHierarchy(ARRLIST_NodeBVH* bvh, ARRLIST_TriangleBB* geometry);

#endif