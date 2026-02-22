#include "processor.h"

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

    return mesh;
}

BOOL IsManifoldValid(const ManifoldMesh* manifold) {
    // TODO:
    return TRUE;
}
