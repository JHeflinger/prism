#include "processor.h"
#include "renderer/renderer.h"
#include <easyhash.h>
#include <easybasics.h>

typedef struct {
    uint32_t v1;
    uint32_t v2;
} IndexPair;

typedef struct {
    mat4 Q;
} QuadricError;

typedef struct {
    uint32_t edge;
    vec3 position;
} CollapseTarget;

uint64_t hash_indexpair(IndexPair ip) {
    return ez_hash_uint64_t(((uint64_t)ip.v1 << 32) | ip.v2);
}

DECLARE_HASHMAP(IndexPair, uint32_t, Edge);
IMPL_HASHMAP(IndexPair, uint32_t, Edge, hash_indexpair);
DECLARE_ARRLIST(QuadricError);
IMPL_ARRLIST(QuadricError);
DECLARE_PQUEUE(CollapseTarget);
IMPL_PQUEUE(CollapseTarget);
DECLARE_ARRLIST(PQPAIR_CollapseTarget);
IMPL_ARRLIST(PQPAIR_CollapseTarget);

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
            uint32_t he = HASHMAP_Edge_get(&map, ip);
            mesh.edges.data[edgeind].halfedge = he;
            mesh.halfedges.data[he].edge = edgeind;
            mesh.halfedges.data[mesh.halfedges.data[he].twin].edge = edgeind;
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

        // validate nexts existing
        if (manifold->halfedges.data[i].next >= manifold->halfedges.size) {
            EZ_ERROR("Halfedge %lu is pointing to an invalid next %lu", (long unsigned int)i, (long unsigned int)manifold->halfedges.data[i].next);
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
        while (TRUE) {
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

        // validate vertex mirroring
        if (manifold->halfedges.data[manifold->vertices.data[i].halfedge].vertex != i) {
            EZ_ERROR("Vertex %lu's halfedge does not point back to the vertex", (long unsigned int)i);
            return FALSE;
        }
    }

    // validate edges
    for (size_t i = 0; i < manifold->edges.size; i++) {
        // validate edge mirroring
        if (manifold->halfedges.data[manifold->edges.data[i].halfedge].edge != i) {
            EZ_ERROR("Edge %lu's halfedge does not point back to the edge", (long unsigned int)i);
            return FALSE;
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
    uint32_t v1 = manifold->halfedges.data[h4].vertex;
    uint32_t v2 = manifold->halfedges.data[h1].vertex;
    uint32_t f1 = manifold->halfedges.data[h1].face;
    uint32_t f2 = manifold->halfedges.data[h4].face;
    manifold->halfedges.data[h1].vertex = manifold->halfedges.data[h3].vertex;
    manifold->halfedges.data[h4].vertex = manifold->halfedges.data[h6].vertex;
    manifold->vertices.data[v1].halfedge = h2;
    manifold->vertices.data[v2].halfedge = h5;
    manifold->faces.data[f1].halfedge = h2;
    manifold->faces.data[f2].halfedge = h5;
    manifold->halfedges.data[h1].next = h6;
    manifold->halfedges.data[h2].next = h1;
    manifold->halfedges.data[h3].next = h5;
    manifold->halfedges.data[h4].next = h3;
    manifold->halfedges.data[h5].next = h4;
    manifold->halfedges.data[h6].next = h2;
    manifold->halfedges.data[h1].face = f1;
    manifold->halfedges.data[h2].face = f1;
    manifold->halfedges.data[h3].face = f2;
    manifold->halfedges.data[h4].face = f2;
    manifold->halfedges.data[h5].face = f2;
    manifold->halfedges.data[h6].face = f1;
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
        ((v2s.position[0] - v4s.position[0])/2.0f) + v4s.position[0],
        ((v2s.position[1] - v4s.position[1])/2.0f) + v4s.position[1],
        ((v2s.position[2] - v4s.position[2])/2.0f) + v4s.position[2]
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
    uint32_t he = manifold->edges.data[edge].halfedge;
    uint32_t v1 = manifold->halfedges.data[he].vertex;
    uint32_t v2 = manifold->halfedges.data[manifold->halfedges.data[he].twin].vertex;
    vec3 midpoint;
    glm_vec3_add(manifold->vertices.data[v1].position, manifold->vertices.data[v2].position, midpoint);
    glm_vec3_scale(midpoint, 0.5f, midpoint);
    DirectedEdgeCollapse(manifold, edge, midpoint);
}

void DirectedEdgeCollapse(ManifoldMesh* manifold, uint32_t edge, vec3 position) {
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
    uint32_t v3 = manifold->halfedges.data[h4].vertex;
    uint32_t v4 = manifold->halfedges.data[h6].vertex;
    uint32_t walkstart = manifold->vertices.data[v1].halfedge;
    uint32_t curr = walkstart;
    do {
        if (manifold->halfedges.data[curr].vertex == v1)
            manifold->halfedges.data[curr].vertex = v3;
        uint32_t twin = manifold->halfedges.data[curr].twin;
        curr = manifold->halfedges.data[twin].next;
    } while (curr != walkstart);
    SETVEC(manifold->vertices.data[v3].position, position);
    manifold->halfedges.data[h9].twin = h10;
    manifold->halfedges.data[h10].twin = h9;
    manifold->halfedges.data[h8].twin = h7;
    manifold->halfedges.data[h7].twin = h8;
    manifold->vertices.data[v2].halfedge = h10;
    manifold->vertices.data[v3].halfedge = h7;
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

void SaveManifoldOBJ(const char* path, ManifoldMesh* manifold) {
    FILE* file = fopen(path, "w");
    if (file == NULL) {
        EZ_ERROR("Unable to open file");
        return;
    }
    uint32_t* vertex_remapping = EZ_ALLOC(manifold->vertices.size, sizeof(uint32_t));
    size_t new_v_ind = 1;
    for (size_t i = 0; i < manifold->vertices.size; i++) {
        if (manifold->vertices.data[i].halfedge == (uint32_t)-1) {
            vertex_remapping[i] = (uint32_t)-1;
        } else {
            vertex_remapping[i] = new_v_ind++;
            ManifoldVertex v = manifold->vertices.data[i];
            fprintf(file, "v %.6f %.6f %.6f\n", v.position[0], v.position[1], v.position[2]);
        }
    }
    for (size_t i = 0; i < manifold->faces.size; i++) {
        uint32_t he = manifold->faces.data[i].halfedge;
        if (he != (uint32_t)-1) {
            uint32_t v1, v2, v3;
            v1 = vertex_remapping[manifold->halfedges.data[he].vertex];
            v2 = vertex_remapping[manifold->halfedges.data[manifold->halfedges.data[he].next].vertex];
            v3 = vertex_remapping[manifold->halfedges.data[manifold->halfedges.data[manifold->halfedges.data[he].next].next].vertex];
            fprintf(file, "f %u %u %u\n", (unsigned int)v1, (unsigned int)v2, (unsigned int)v3);
        }
    }
    EZ_FREE(vertex_remapping);
    fclose(file);
}

void ReformatFromManifold(Geometry* geometry) {
    ARRLIST_Vector3_wipe(&(geometry->vertices));
    ARRLIST_Vector3_wipe(&(geometry->normals));
    ARRLIST_Triangle_wipe(&(geometry->triangles));
    ARRLIST_TriangleID_wipe(&(geometry->tids));
    ARRLIST_TriangleID_wipe(&(geometry->emissives));
    uint32_t* vertex_remapping = EZ_ALLOC(geometry->manifold.vertices.size, sizeof(uint32_t));
    size_t new_v_ind = 0;
    for (size_t i = 0; i < geometry->manifold.vertices.size; i++) {
        if (geometry->manifold.vertices.data[i].halfedge == (uint32_t)-1) {
            vertex_remapping[i] = (uint32_t)-1;
        } else {
            vertex_remapping[i] = new_v_ind++;
            ManifoldVertex v = geometry->manifold.vertices.data[i];
            SubmitVertex((Vector3){ v.position[0], v.position[1], v.position[2] });
        }
    }
    for (size_t i = 0; i < geometry->manifold.faces.size; i++) {
        uint32_t he = geometry->manifold.faces.data[i].halfedge;
        if (he != (uint32_t)-1) {
            uint32_t v1, v2, v3;
            v1 = vertex_remapping[geometry->manifold.halfedges.data[he].vertex];
            v2 = vertex_remapping[geometry->manifold.halfedges.data[geometry->manifold.halfedges.data[he].next].vertex];
            v3 = vertex_remapping[geometry->manifold.halfedges.data[geometry->manifold.halfedges.data[geometry->manifold.halfedges.data[he].next].next].vertex];
            SubmitTriangle((Triangle){ v1, v2, v3, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1, 0});
        }
    }
    EZ_FREE(vertex_remapping);
    UpdateTriangles();
    UpdateVertices();
    UpdateNormals();
}

void SerialSubdivide(ManifoldMesh* manifold) {
    size_t original_edges_size = manifold->edges.size;
    size_t original_vertices_size = manifold->vertices.size;
    constexpr float one_eighth = (1.0f/8.0f);
    constexpr float three_eighths = (3.0f/8.0f);
    constexpr float one_fourth = 0.25f;
    constexpr float three_sixteenths = (3.0f/16.0f);
    constexpr float five_eights = (5.0f/8.0f);

    // subdivide triangles
    for (size_t i = 0; i < original_edges_size; i++) {
        EdgeSplit(manifold, i);
    }
    for (size_t i = original_edges_size; i < manifold->edges.size; i++) {
        if ((i - original_edges_size)%3 == 0) continue;
        uint32_t he = manifold->edges.data[i].halfedge;
        uint32_t v1 = manifold->halfedges.data[he].vertex;
        uint32_t v2 = manifold->halfedges.data[manifold->halfedges.data[he].twin].vertex;
        if (v1 < original_vertices_size || v2 < original_vertices_size) {
            EdgeFlip(manifold, i);
        }
    }

    // set new vertex positions
    for (size_t i = original_vertices_size; i < manifold->vertices.size; i++) {
        uint32_t start = manifold->vertices.data[i].halfedge;
        uint32_t curr = start;
        BOOL extend = FALSE;
        BOOL override = FALSE;
        vec3 new_pos = { 0 };
        while (TRUE) {
            uint32_t twin = manifold->halfedges.data[curr].twin;
            curr = manifold->halfedges.data[twin].next;
            if (override) break;
            if (curr == start) {
                if (!extend) {
                    break;
                } else {
                    override = TRUE;
                }
            }
            uint32_t vertex = manifold->halfedges.data[twin].vertex;
            if (vertex < original_vertices_size) {
                extend = TRUE;
                glm_vec3_muladds(manifold->vertices.data[vertex].position, three_eighths, new_pos);
            } else if (extend) {
                extend = FALSE;
                uint32_t medium = twin;
                medium = manifold->halfedges.data[medium].next;
                medium = manifold->halfedges.data[medium].next;
                medium = manifold->halfedges.data[medium].twin;
                medium = manifold->halfedges.data[medium].next;
                medium = manifold->halfedges.data[medium].next;
                medium = manifold->halfedges.data[medium].vertex;
                EZ_ASSERT(medium < original_vertices_size, "Extended vertex was detected to not be an old vertex");
                glm_vec3_muladds(manifold->vertices.data[medium].position, one_eighth, new_pos);
            }
        }
        glm_vec3_copy(new_pos, manifold->vertices.data[i].position);
    }

    // set old vertex positions
    for (size_t i = 0; i < original_vertices_size; i++) {
        size_t degree = 0;
        uint32_t start = manifold->vertices.data[i].halfedge;
        uint32_t curr = start;
        vec3 new_pos = { 0 };
        do {
            degree++;
            uint32_t medium = manifold->halfedges.data[curr].next;
            medium = manifold->halfedges.data[medium].twin;
            medium = manifold->halfedges.data[medium].next;
            medium = manifold->halfedges.data[medium].twin;
            medium = manifold->halfedges.data[medium].next;
            medium = manifold->halfedges.data[medium].twin;
            medium = manifold->halfedges.data[medium].vertex;
            EZ_ASSERT(medium < original_vertices_size, "Extended vertex was detected to not be an old vertex");
            glm_vec3_add(manifold->vertices.data[medium].position, new_pos, new_pos);
            uint32_t twin = manifold->halfedges.data[curr].twin;
            curr = manifold->halfedges.data[twin].next;
        } while (curr != start);
        float u = three_sixteenths;
        if (degree != 3) {
            float overn = 1.0f / ((float)degree);
            u = (five_eights - powf(three_eighths + one_fourth * cos(2.0f*M_PI*overn), 2.0f)) * overn;
        }
        glm_vec3_scale(new_pos, u, new_pos);
        glm_vec3_muladds(manifold->vertices.data[i].position, 1.0f - (((float)degree)*u), new_pos);
        glm_vec3_copy(new_pos, manifold->vertices.data[i].position);
    }
}

void SerialSimplify(ManifoldMesh* manifold, size_t reduction) {
    ARRLIST_QuadricError qs = { 0 };
    ARRLIST_PQPAIR_CollapseTarget errors = { 0 };
    ARRLIST_QuadricError_zero(&qs, manifold->vertices.size);
    ARRLIST_PQPAIR_CollapseTarget_zero(&errors, manifold->edges.size);
    PQUEUE_CollapseTarget pq = { 0 };

    // initial quadrics
    for (size_t i = 0; i < manifold->faces.size; i++) {
        uint32_t he1 = manifold->faces.data[i].halfedge;
        uint32_t he2 = manifold->halfedges.data[he1].next;
        uint32_t he3 = manifold->halfedges.data[he2].next;
        uint32_t v1 = manifold->halfedges.data[he1].vertex;
        uint32_t v2 = manifold->halfedges.data[he2].vertex;
        uint32_t v3 = manifold->halfedges.data[he3].vertex;
        vec3 e1, e2, normal;
        glm_vec3_sub(manifold->vertices.data[v2].position, manifold->vertices.data[v1].position, e1);
        glm_vec3_sub(manifold->vertices.data[v3].position, manifold->vertices.data[v1].position, e2);
        glm_vec3_cross(e1, e2, normal);
        glm_vec3_normalize(normal);
        float a = normal[0];
        float b = normal[1];
        float c = normal[2];
        float d = -glm_vec3_dot(normal, manifold->vertices.data[v1].position);
        vec4 p = { a, b, c, d };
        mat4 Kp;
        glm_mat4_zero(Kp);
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) Kp[i][j] = p[i] * p[j];
        Mat4Add(qs.data[v1].Q, Kp, qs.data[v1].Q);
        Mat4Add(qs.data[v2].Q, Kp, qs.data[v2].Q);
        Mat4Add(qs.data[v3].Q, Kp, qs.data[v3].Q);
    }

    // build edge costs
    inline float calculate_edge_cost(uint32_t i, PQPAIR_CollapseTarget* output) {
        uint32_t halfedge = manifold->edges.data[i].halfedge;
        uint32_t v1 = manifold->halfedges.data[halfedge].vertex;
        uint32_t v2 = manifold->halfedges.data[manifold->halfedges.data[halfedge].twin].vertex;
        mat4 Q;
        Mat4Add(qs.data[v1].Q, qs.data[v2].Q, Q);
        vec3 optimal_position;
        mat3 A;
        vec3 b;
        vec3 _b;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++)
                A[i][j] = Q[i][j];
            b[i] = Q[i][3];
        }
        glm_vec3_negate_to(b, _b);
        if (fabs(glm_mat3_det(A)) < 1e-6f) {
            glm_vec3_add(manifold->vertices.data[v1].position, manifold->vertices.data[v2].position, optimal_position);
            glm_vec3_scale(optimal_position, 0.5f, optimal_position);
        } else {
            mat3 _A;
            glm_mat3_inv(A, _A);
            glm_mat3_mulv(_A, _b, optimal_position);
        }
        vec4 v = { optimal_position[0], optimal_position[1], optimal_position[2], 1.0f };
        vec4 tmp;
        glm_mat4_mulv(Q, v, tmp);
        output->value.edge = i;
        SETVEC(output->value.position, optimal_position);
        return glm_vec4_dot(v, tmp);
    }
    for (size_t i = 0; i < manifold->edges.size; i++) {
        float initial_cost = calculate_edge_cost(i, &(errors.data[i]));
        errors.data[i].cost = initial_cost;
    }
    PQUEUE_CollapseTarget_build(&pq, errors.data, errors.size);

    // collapse targets
    inline BOOL check_valid_collapse(uint32_t edge) {
        uint32_t he = manifold->edges.data[edge].halfedge;
        if (he == (uint32_t)-1) return FALSE;
        uint32_t v1 = manifold->halfedges.data[he].vertex;
        uint32_t v2 = manifold->halfedges.data[manifold->halfedges.data[he].twin].vertex;
        uint32_t start = manifold->vertices.data[v1].halfedge;
        uint32_t curr = start;
        uint32_t numtouches = 0;
        do {
            uint32_t twin = manifold->halfedges.data[curr].twin;
            uint32_t vert = manifold->halfedges.data[twin].vertex;
            uint32_t degree = 0;
            uint32_t start2 = manifold->vertices.data[vert].halfedge;
            uint32_t curr2 = start2;
            BOOL touches = FALSE;
            do {
                degree++;
                uint32_t twin2 = manifold->halfedges.data[curr2].twin;
                uint32_t vert2 = manifold->halfedges.data[twin2].vertex;
                if (vert2 == v2) touches = TRUE;
                curr2 = manifold->halfedges.data[twin2].next;
            } while (curr2 != start2);
            if (touches && degree == 3) {
                return FALSE;
            }
            if (touches) numtouches++;
            curr = manifold->halfedges.data[twin].next;
            if (curr == start) break;
        } while (curr != start);
        return numtouches <= 2;
    }
    for (size_t i = 0; i < reduction; i++) {
        if (pq.size == 0) EZ_ERROR("Unable to simplify a mesh into a negative number of edges");
        CollapseTarget target = PQUEUE_CollapseTarget_pop(&pq);
        uint32_t edge = target.edge;
        if (!check_valid_collapse(edge)) {
            i--;
            continue;
        }
        uint32_t halfedge = manifold->edges.data[edge].halfedge;
        uint32_t v1 = manifold->halfedges.data[halfedge].vertex;
        uint32_t v2 = manifold->halfedges.data[manifold->halfedges.data[halfedge].twin].vertex;
        DirectedEdgeCollapse(manifold, edge, target.position);
        Mat4Add(qs.data[v1].Q, qs.data[v2].Q, qs.data[v2].Q);
        uint32_t curr = manifold->vertices.data[v2].halfedge;
        do {
            uint32_t edge = manifold->halfedges.data[curr].edge;
            float newcost = calculate_edge_cost(edge, &(pq.list[edge].pair));
            PQUEUE_CollapseTarget_update(&pq, edge, newcost);
            uint32_t twin = manifold->halfedges.data[curr].twin;
            curr = manifold->halfedges.data[twin].next;
        } while (curr != manifold->vertices.data[v2].halfedge);
    }

    PQUEUE_CollapseTarget_clear(&pq);
    ARRLIST_PQPAIR_CollapseTarget_clear(&errors);
    ARRLIST_QuadricError_clear(&qs);
}
