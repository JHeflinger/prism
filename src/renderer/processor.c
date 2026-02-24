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
    for (size_t i = 0; i < vertices.size; i++) {
        SETVEC3(mesh.vertices.data[i].position, vertices.data[i].x, vertices.data[i].y, vertices.data[i].z);
    }

    // step 2: create faces and halfedges
    for (size_t i = 0; i < triangles.size; i++) {
        uint32_t base = i * 3;
        mesh.faces.data[i].halfedge = base;
        mesh.halfedges.data[i].twin = (uint32_t)-1;
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
        if (HASHMAP_Edge_has(&map, ip)) {
            uint32_t twin = HASHMAP_Edge_get(&map, ip);
            mesh.halfedges.data[twin].twin = i;
            mesh.halfedges.data[i].twin = twin;
            HASHMAP_Edge_remove(&map, (IndexPair){ ip.v2, ip.v1 });
        }
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

    // validate half-edges
    for (size_t i = 0; i < manifold->halfedges.size; i++) {

        // validate border edges
        const uint32_t twin = manifold->halfedges.data[i].twin;
        if (twin == (uint32_t)-1) {
            EZ_ERROR("Border twin detected on halfedge %lu - this mesh cannot be manifold", (long unsigned int)i);
            return FALSE;
        }

        // validate twins
        if (twin != (uint32_t)-1 && manifold->halfedges.data[twin].twin != i) {
            EZ_ERROR("Corrupted twin detected - expected [%lu -> %lu -> %lu] but found [%lu -> %lu -> %lu]",
                (long unsigned int)i, (long unsigned int)twin, (long unsigned int)i,
                (long unsigned int)i, (long unsigned int)twin, (long unsigned int)manifold->halfedges.data[twin].twin);
            return FALSE;
        }

        // validate vertices existing
        if (manifold->halfedges.data[i].vertex >= manifold->vertices.size) {
            EZ_ERROR("Halfedge %lu has a non-existent vertex %lu", (long unsigned int)i, (long unsigned int)manifold->halfedges.data[i].vertex);
            return FALSE;
        }

        // validate faces existing
        if (manifold->halfedges.data[i].face >= manifold->faces.size) {
            EZ_ERROR("Halfedge %lu has a non-existent face %lu", (long unsigned int)i, (long unsigned int)manifold->halfedges.data[i].face);
            return FALSE;
        }

        // validate twins existing
        if (twin != (uint32_t)-1 && twin >= manifold->halfedges.size) {
            EZ_ERROR("Out-of-bounds twin detected on halfedge %lu", (long unsigned int)i);
            return FALSE;
        }

        // validate nexts
        if (manifold->halfedges.data[i].next == i) {
            EZ_ERROR("The next of halfedge %lu is pointing to itself", (long unsigned int)i);
            return FALSE;
        }

        // validate face closures
        int ncount = 0;
        uint32_t curr = i;
        while (manifold->halfedges.data[curr].next != i && ncount < 3) {
            curr = manifold->halfedges.data[curr].next;
            ncount++;
        }
        if (ncount != 2) {
            EZ_ERROR("An unclosed halfedge face closure was detected on halfedge %lu", (long unsigned int)i);
            return FALSE;
        }

        // validate face pointers
        uint32_t fhe = manifold->faces.data[manifold->halfedges.data[i].face].halfedge;
        curr = fhe;
        while (TRUE) {
            if (curr == i) break;
            curr = manifold->halfedges.data[curr].next;
            if (curr == fhe) {
                EZ_ERROR("Halfedge %lu points to a face that does not include it", (long unsigned int)i);
                return FALSE;
            }
        }

        // validate twin-face consistency
        if (twin != (uint32_t)-1 && manifold->halfedges.data[i].face == manifold->halfedges.data[twin].face) {
            EZ_ERROR("Halfedge %lu points to the same face as its twin %lu", (long unsigned int)i, (long unsigned int)twin);
            return FALSE;
        }

        // validate twin-vertex consistency
        if (twin != (uint32_t)-1 && manifold->halfedges.data[i].vertex == manifold->halfedges.data[twin].vertex) {
            EZ_ERROR("Halfedge %lu points to the same vertex as its twin %lu", (long unsigned int)i , (long unsigned int)twin);
            return FALSE;
        }

    }

    // validate vertices
    for (size_t i = 0; i < manifold->vertices.size; i++) {

        // validate vertex fan
        uint32_t start = manifold->vertices.data[i].halfedge;
        uint32_t curr = start;
        uint32_t visited = 0;
        while (true) {
            uint32_t twin = manifold->halfedges.data[curr].twin;
            if (twin == (uint32_t)-1) break;
            curr = manifold->halfedges.data[twin].next;
            visited++;
            if (curr == start) {
                if (visited < 2) {
                    EZ_ERROR("Vertex connectivity is less than 2 on vertex %lu", (long unsigned int)i);
                    return FALSE;
                }
                break;
            }
            if (visited >= manifold->edges.size) {
                EZ_ERROR("Vertex connectivity is larger than possible on vertex %lu", (long unsigned int)i);
                return FALSE;
            }
        }

    }

    // Euler characteristic check
    size_t total = manifold->vertices.size - manifold->edges.size + manifold->faces.size;
    if (total != 2) {
        EZ_ERROR("Euler characteristic check failed - mesh is not closed and manifold");
        return FALSE;
    }

    return TRUE;
}

void EdgeFlip(ManifoldMesh* manifold, uint32_t edge) {
    uint32_t h1 = manifold->edges.data[edge].halfedge;
    uint32_t h2 = manifold->halfedges.data[h1].next;
    uint32_t h3 = manifold->halfedges.data[h2].next;
    uint32_t h4 = manifold->halfedges.data[h1].twin;
    uint32_t h5 = manifold->halfedges.data[h4].next;
    uint32_t h6 = manifold->halfedges.data[h5].next;
    uint32_t v1 = manifold->halfedges.data[h1].vertex;
    uint32_t v2 = manifold->halfedges.data[h4].vertex;
    uint32_t f1 = manifold->halfedges.data[h3].face;
    uint32_t f2 = manifold->halfedges.data[h5].face;
    manifold->halfedges.data[h1].next = h3;
    manifold->halfedges.data[h1].vertex = manifold->halfedges.data[h6].vertex;
    manifold->halfedges.data[h4].next = h6;
    manifold->halfedges.data[h4].vertex = manifold->halfedges.data[h3].vertex;
    manifold->vertices.data[v1].halfedge = h3;
    manifold->vertices.data[v2].halfedge = h2;
    manifold->halfedges.data[h3].next = h5;
    manifold->halfedges.data[h5].next = h1;
    manifold->halfedges.data[h6].next = h2;
    manifold->halfedges.data[h2].next = h4;
    manifold->faces.data[f1].halfedge = h1;
    manifold->faces.data[f2].halfedge = h4;
    manifold->halfedges.data[h1].face = f1;
    manifold->halfedges.data[h3].face = f1;
    manifold->halfedges.data[h5].face = f1;
    manifold->halfedges.data[h2].face = f2;
    manifold->halfedges.data[h4].face = f2;
    manifold->halfedges.data[h6].face = f2;
}

void EdgeSplit(ManifoldMesh* manifold, uint32_t edge) {
    uint32_t h1 = manifold->edges.data[edge].halfedge;
    uint32_t h2 = manifold->halfedges.data[h1].next;
    uint32_t h3 = manifold->halfedges.data[h2].next;
    uint32_t h4 = manifold->halfedges.data[h1].twin;
    uint32_t h5 = manifold->halfedges.data[h4].next;
    uint32_t h6 = manifold->halfedges.data[h5].next;
    uint32_t h7 = manifold->halfedges.size;
    uint32_t h8 = manifold->halfedges.size + 1;
    uint32_t h9 = manifold->halfedges.size + 2;
    uint32_t h10 = manifold->halfedges.size + 3;
    uint32_t h11 = manifold->halfedges.size + 4;
    uint32_t h12 = manifold->halfedges.size + 5;
    uint32_t f1 = manifold->halfedges.data[h3].face;
    uint32_t f2 = manifold->halfedges.data[h5].face;
    uint32_t f3 = manifold->faces.size;
    uint32_t f4 = manifold->faces.size + 1;
    uint32_t v1 = manifold->halfedges.data[h6].vertex;
    uint32_t v2 = manifold->halfedges.data[h5].vertex;
    uint32_t v3 = manifold->halfedges.data[h3].vertex;
    uint32_t v4 = manifold->halfedges.data[h2].vertex;
    uint32_t v5 = manifold->vertices.size;
    uint32_t e1 = manifold->edges.size;
    uint32_t e2 = manifold->edges.size + 1;
    uint32_t e3 = manifold->edges.size + 2;
    ManifoldVertex v2s = manifold->vertices.data[v2];
    ManifoldVertex v4s = manifold->vertices.data[v4];
    ARRLIST_ManifoldVertex_add(&(manifold->vertices), (ManifoldVertex) { h4, {
        ((v2s.position[0] - v4s.position[0])/2.0f) + v2s.position[0],
        ((v2s.position[1] - v4s.position[1])/2.0f) + v2s.position[1],
        ((v2s.position[2] - v4s.position[2])/2.0f) + v2s.position[2]
    }});
    ARRLIST_ManifoldFace_add(&(manifold->faces), (ManifoldFace) { h6 });
    ARRLIST_ManifoldFace_add(&(manifold->faces), (ManifoldFace) { h2 });
    ARRLIST_ManifoldEdge_add(&(manifold->edges), (ManifoldEdge) { h7 });
    ARRLIST_ManifoldEdge_add(&(manifold->edges), (ManifoldEdge) { h9 });
    ARRLIST_ManifoldEdge_add(&(manifold->edges), (ManifoldEdge) { h11 });
    ARRLIST_ManifoldHalfEdge_add(&(manifold->halfedges), (ManifoldHalfEdge) { h8, h11, v4, e1, f3 });
    ARRLIST_ManifoldHalfEdge_add(&(manifold->halfedges), (ManifoldHalfEdge) { h7, h2, v5, e1, f4 });
    ARRLIST_ManifoldHalfEdge_add(&(manifold->halfedges), (ManifoldHalfEdge) { h10, h8, v3, e2, f4 });
    ARRLIST_ManifoldHalfEdge_add(&(manifold->halfedges), (ManifoldHalfEdge) { h9, h3, v5, e2, f2 });
    ARRLIST_ManifoldHalfEdge_add(&(manifold->halfedges), (ManifoldHalfEdge) { h12, h6, v5, e3, f3 });
    ARRLIST_ManifoldHalfEdge_add(&(manifold->halfedges), (ManifoldHalfEdge) { h11, h4, v1, e3, f1 });
    manifold->faces.data[f1].halfedge = h5;
    manifold->faces.data[f2].halfedge = h1;
    manifold->vertices.data[v1].halfedge = h6;
    manifold->vertices.data[v2].halfedge = h1;
    manifold->vertices.data[v3].halfedge = h3;
    manifold->vertices.data[v4].halfedge = h2;
    manifold->halfedges.data[h1].face = f2;
    manifold->halfedges.data[h2].face = f4;
    manifold->halfedges.data[h3].face = f2;
    manifold->halfedges.data[h4].face = f1;
    manifold->halfedges.data[h5].face = f1;
    manifold->halfedges.data[h6].face = f3;
    manifold->halfedges.data[h1].vertex = v2;
    manifold->halfedges.data[h2].vertex = v4;
    manifold->halfedges.data[h3].vertex = v3;
    manifold->halfedges.data[h4].vertex = v5;
    manifold->halfedges.data[h5].vertex = v2;
    manifold->halfedges.data[h6].vertex = v1;
    manifold->halfedges.data[h1].next = h10;
    manifold->halfedges.data[h2].next = h9;
    manifold->halfedges.data[h5].next = h12;
    manifold->halfedges.data[h6].next = h7;
}

void EdgeCollapse(ManifoldMesh* manifold, uint32_t edge) {
    uint32_t h1 = manifold->edges.data[edge].halfedge;
    uint32_t h2 = manifold->halfedges.data[h1].next;
    uint32_t h3 = manifold->halfedges.data[h2].next;
    uint32_t h4 = manifold->halfedges.data[h1].twin;
    uint32_t h5 = manifold->halfedges.data[h4].next;
    uint32_t h6 = manifold->halfedges.data[h5].next;
    uint32_t h7 = manifold->halfedges.data[h6].twin;
    uint32_t h8 = manifold->halfedges.data[h5].twin;
    uint32_t h9 = manifold->halfedges.data[h3].twin;
    uint32_t h10 = manifold->halfedges.data[h2].twin;
    uint32_t e1 = edge;
    uint32_t e2 = manifold->halfedges.data[h3].edge;
    uint32_t e3 = manifold->halfedges.data[h2].edge;
    uint32_t e4 = manifold->halfedges.data[h5].edge;
    uint32_t e5 = manifold->halfedges.data[h6].edge;
    uint32_t f1 = manifold->halfedges.data[h1].face;
    uint32_t f2 = manifold->halfedges.data[h4].face;
    uint32_t v1 = manifold->halfedges.data[h1].vertex;
    uint32_t v2 = manifold->halfedges.data[h3].vertex;
    uint32_t v3 = manifold->halfedges.data[h7].vertex;
    uint32_t v4 = manifold->halfedges.data[h6].vertex;
    uint32_t walkstart = h5;
    uint32_t curr = walkstart;
    while (true) {
        if (manifold->halfedges.data[curr].vertex == v1)
            manifold->halfedges.data[curr].vertex = v3;
        uint32_t twin = manifold->halfedges.data[curr].twin;
        if (twin == (uint32_t)-1) break;
        curr = manifold->halfedges.data[twin].next;
        if (curr == walkstart) break;
    }
    manifold->halfedges.data[h9].twin = h10;
    manifold->halfedges.data[h10].twin = h9;
    manifold->halfedges.data[h8].twin = h7;
    manifold->halfedges.data[h7].twin = h8;
    manifold->vertices.data[v2].halfedge = h10;
    manifold->vertices.data[v4].halfedge = h8;
    manifold->halfedges.data[h9].edge = e3;
    manifold->halfedges.data[h8].edge = e5;
    manifold->edges.data[e3].halfedge = h10;
    manifold->edges.data[e5].halfedge = h7;
    manifold->faces.data[f1].halfedge = (uint32_t)-1;
    manifold->faces.data[f2].halfedge = (uint32_t)-1;
    manifold->halfedges.data[h1].next = (uint32_t)-1;
    manifold->halfedges.data[h2].next = (uint32_t)-1;
    manifold->halfedges.data[h3].next = (uint32_t)-1;
    manifold->halfedges.data[h4].next = (uint32_t)-1;
    manifold->halfedges.data[h5].next = (uint32_t)-1;
    manifold->halfedges.data[h6].next = (uint32_t)-1;
    manifold->vertices.data[v1].halfedge = (uint32_t)-1;
    manifold->edges.data[e1].halfedge = (uint32_t)-1;
    manifold->edges.data[e2].halfedge = (uint32_t)-1;
    manifold->edges.data[e4].halfedge = (uint32_t)-1;
}
