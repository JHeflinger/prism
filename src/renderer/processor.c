#include "processor.h"
#include <easyhash.h>

typedef struct {
    uint32_t v1;
    uint32_t v2;
} IndexPair;

uint64_t hash_indexpair(IndexPair ip) {
    return ez_hash_uint64_t(((uint64_t)ip.v1 << 32) | ip.v2);
}

DECLARE_HASHMAP(IndexPair, uint32_t, Edge);
IMPL_HASHMAP(IndexPair, uint32_t, Edge, hash_indexpair);

void CleanManifoldMesh(ManifoldMesh* manifold) {
    ARRLIST_ManifoldVertex_clear(&(manifold->vertices));
    ARRLIST_ManifoldEdge_clear(&(manifold->edges));
    ARRLIST_ManifoldFace_clear(&(manifold->faces));
    ARRLIST_ManifoldHalfEdge_clear(&(manifold->halfedges));
}

ManifoldMesh GenerateManifoldMesh(const ARRLIST_Vector3 vertices, const ARRLIST_Vector3 normals, const ARRLIST_Triangle triangles) {
    ManifoldMesh mesh = { 0 };

    // step 1: allocate/initialize
    ARRLIST_ManifoldFace_zero(&mesh.faces, triangles.size);
    ARRLIST_ManifoldHalfEdge_zero(&mesh.halfedges, triangles.size*3);
    ARRLIST_ManifoldVertex_zero(&mesh.vertices, vertices.size);
    for (size_t i = 0; i < vertices.size; i++) mesh.vertices.data[i].index = i;

    // step 2: create faces and halfedges
    for (size_t i = 0; i < triangles.size; i++) {
        uint32_t base = i * 3;
        mesh.faces.data[i].halfedge = base;
        mesh.halfedges.data[base + 0].face = i;
        mesh.halfedges.data[base + 1].face = i;
        mesh.halfedges.data[base + 2].face = i;
        mesh.halfedges.data[base + 0].next = base + 1;
        mesh.halfedges.data[base + 1].next = base + 2;
        mesh.halfedges.data[base + 2].next = base + 0;
        mesh.halfedges.data[base + 0].vertex = triangles.data[i].a;
        mesh.halfedges.data[base + 1].vertex = triangles.data[i].b;
        mesh.halfedges.data[base + 2].vertex = triangles.data[i].c;
        mesh.vertices.data[triangles.data[i].a].halfedge = base + 0;
        mesh.vertices.data[triangles.data[i].b].halfedge = base + 1;
        mesh.vertices.data[triangles.data[i].c].halfedge = base + 2;
    }

    // step 3: calculate edges and twins
    HASHMAP_Edge map = { 0 };
    for (size_t i = 0; i < triangles.size*3; i++) {
        IndexPair ip = { mesh.halfedges.data[i].vertex, mesh.halfedges.data[mesh.halfedges.data[i].next].vertex };
        HASHMAP_Edge_set(&map, ip, i);
    }
    for (size_t i = 0; i < triangles.size*3; i++) {
        IndexPair ip = { mesh.halfedges.data[mesh.halfedges.data[i].next].vertex, mesh.halfedges.data[i].vertex };
        uint32_t twin = (uint32_t)-1;
        if (HASHMAP_Edge_has(&map, ip)) {
            twin = HASHMAP_Edge_get(&map, ip);
            mesh.halfedges.data[twin].twin = i;
            HASHMAP_Edge_remove(&map, (IndexPair){ ip.v2, ip.v1 });
        }
        mesh.halfedges.data[i].twin = twin;
    }
    ARRLIST_ManifoldEdge_zero(&mesh.edges, map.size);
    size_t edgeind = 0;
    for (size_t i = 0; i < triangles.size*3; i++) {
        IndexPair ip = { mesh.halfedges.data[i].vertex, mesh.halfedges.data[mesh.halfedges.data[i].next].vertex };
        if (HASHMAP_Edge_has(&map, ip)) {
            mesh.edges.data[edgeind].halfedge = HASHMAP_Edge_get(&map, ip);
            edgeind++;
        }
    }
    HASHMAP_Edge_clear(&map);

    return mesh;
}

BOOL IsManifoldValid(const ManifoldMesh* manifold) {
    // TODO:
    return TRUE;
}
