#include "rutils.h"
#include "core/log.h"

#define BVH_LIMIT 0.01f

IMPL_ARRLIST(size_t);

void SplitBVH(ARRLIST_NodeBVH* bvh, size_t index, ARRLIST_TriangleBB* geometry, ARRLIST_size_t* children) {
    #define CBVH bvh->data[index]
    #define BVHMIN bvh->data[index].min
    #define BVHMAX bvh->data[index].max

    // set up split boxes
    float xyz_dist[3] = {
        BVHMAX[0] - BVHMIN[0],
        BVHMAX[1] - BVHMIN[1],
        BVHMAX[2] - BVHMIN[2],
    };
    int dist_ind = xyz_dist[0] > xyz_dist[1] ?
        (xyz_dist[0] > xyz_dist[2] ? 0 : 2) :
        (xyz_dist[1] > xyz_dist[2] ? 1 : 2);
    if (xyz_dist[dist_ind] < BVH_LIMIT) {
        for (size_t i = 0; i < children->size; i++) {
            NodeBVH child = {
                { geometry->data[children->data[i]].min[0],
                  geometry->data[children->data[i]].min[1],
                  geometry->data[children->data[i]].min[2] },
                { geometry->data[children->data[i]].max[0],
                  geometry->data[children->data[i]].max[1],
                  geometry->data[children->data[i]].max[2] },
                { BVH_LEAF, 0, 0 }
            };
            ARRLIST_NodeBVH_add(bvh, child);
            bvh->data[index].branches[1] = bvh->size - 1;

            if (i + 1 >= children->size) {
                bvh->data[index].branches[0] = BVH_LEFT_ONLY;
            } else {
                bvh->data[index].branches[0] = BVH_BOTH;
                NodeBVH next = {
                    { BVHMIN[0], BVHMIN[1], BVHMIN[2] },
                    { BVHMAX[0], BVHMAX[1], BVHMAX[2] },
                    { BVH_LEAF, 0, 0 }
                };
                ARRLIST_NodeBVH_add(bvh, next);
                bvh->data[index].branches[2] = bvh->size - 1;
                index = bvh->size - 1;
            }
        }
        return;
    }
    float mid_dist = xyz_dist[dist_ind] / 2.0f;
    NodeBVH left = {
        { BVHMIN[0], BVHMIN[1], BVHMIN[2] },
        { BVHMAX[0], BVHMAX[1], BVHMAX[2] },
        { BVH_LEAF, 0, 0 }
    };
    NodeBVH right = {
        { BVHMIN[0], BVHMIN[1], BVHMIN[2] },
        { BVHMAX[0], BVHMAX[1], BVHMAX[2] },
        { BVH_LEAF, 0, 0 }
    };
    left.max[dist_ind] -= mid_dist;
    right.min[dist_ind] += mid_dist;

    // set up subchildren
    ARRLIST_size_t left_children = { 0 };
    ARRLIST_size_t right_children = { 0 };
    for (size_t i = 0; i < children->size; i++) {
        if (geometry->data[children->data[i]].centroid[dist_ind] < left.max[dist_ind])
            ARRLIST_size_t_add(&left_children, children->data[i]);
        else ARRLIST_size_t_add(&right_children, children->data[i]);
    }
    if (left_children.size > 0 && right_children.size > 0) {
        CBVH.branches[0] = BVH_BOTH;
    } else if (left_children.size > 0) {
        CBVH.branches[0] = BVH_LEFT_ONLY;
    } else if (right_children.size > 0) {
        CBVH.branches[0] = BVH_RIGHT_ONLY;
    } else {
        CBVH.branches[0] = BVH_LEAF;
    }
    if (left_children.size > 1) {
        ARRLIST_NodeBVH_add(bvh, left);
        CBVH.branches[1] = bvh->size - 1;
        SplitBVH(bvh, bvh->size - 1, geometry, &left_children);
    } else if (left_children.size == 1) {
        left.branches[0] = BVH_LEAF;
        left.branches[1] = left_children.data[0];
        ARRLIST_NodeBVH_add(bvh, left);
        CBVH.branches[1] = bvh->size - 1;
    }
    if (right_children.size > 1) {
        ARRLIST_NodeBVH_add(bvh, right);
        CBVH.branches[2] = bvh->size - 1;
        SplitBVH(bvh, bvh->size - 1, geometry, &right_children);
    } else if (right_children.size == 1) {
        right.branches[0] = BVH_LEAF;
        right.branches[1] = right_children.data[0];
        ARRLIST_NodeBVH_add(bvh, right);
        CBVH.branches[2] = bvh->size - 1;
    }
    ARRLIST_size_t_clear(&left_children);
    ARRLIST_size_t_clear(&right_children);

    // resize
    if (bvh->data[index].branches[0] == 3 || bvh->data[index].branches[0] == 1) {
        glm_vec3_minv(
            bvh->data[index].min,
            bvh->data[bvh->data[index].branches[1]].min,
            bvh->data[index].min);
        glm_vec3_maxv(
            bvh->data[index].max,
            bvh->data[bvh->data[index].branches[1]].max,
            bvh->data[index].max);
    }
    if (bvh->data[index].branches[0] == 3 || bvh->data[index].branches[0] == 2) {
        glm_vec3_minv(
            bvh->data[index].min,
            bvh->data[bvh->data[index].branches[2]].min,
            bvh->data[index].min);
        glm_vec3_maxv(
            bvh->data[index].max,
            bvh->data[bvh->data[index].branches[2]].max,
            bvh->data[index].max);
    }
    
    #undef CBVH
    #undef BVHMIN
    #undef BVHMAX
}

void test(ARRLIST_NodeBVH* bvh) {
    LOG_INFO("%d", (int)bvh->size);
    uint32_t stack[200];
    int stack_ptr = 0;
    stack[stack_ptr++] = 0;
    while (stack_ptr > 0) {
        if (stack[stack_ptr - 1] >= bvh->size) { LOG_FATAL("out of bounds!"); }
        NodeBVH node = bvh->data[stack[--stack_ptr]];
        if (stack_ptr >= 200 - 1) { LOG_FATAL("out of stack space!"); }
        if (node.branches[0] == 0) {
            // triangle intersection check, move on
        } else if (node.branches[0] == 1) {
            // traverse left tree
            stack[stack_ptr++] = node.branches[1];
        } else if (node.branches[0] == 2) {
            // traverse right tree
            stack[stack_ptr++] = node.branches[2];
        } else {
            // traverse both tree sides
            stack[stack_ptr++] = node.branches[1];
            stack[stack_ptr++] = node.branches[2];
        }
    }
}

void RUTIL_BoundingVolumeHierarchy(ARRLIST_NodeBVH* bvh, ARRLIST_TriangleBB* geometry) {
    // clear old bvh
    ARRLIST_NodeBVH_clear(bvh);

    // create root
    NodeBVH root = {
        { FLT_MAX, FLT_MAX, FLT_MAX },
        { -FLT_MAX, -FLT_MAX, -FLT_MAX },
        { BVH_LEAF, 0, 0 }
    };

    // resize root
    for (size_t i = 0; i < geometry->size; i++) {
        glm_vec3_minv(root.min, geometry->data[i].min, root.min);
        glm_vec3_maxv(root.max, geometry->data[i].max, root.max);
    }

    // add to bvh
    ARRLIST_NodeBVH_add(bvh, root);

    // set up indices
    ARRLIST_size_t indices = { 0 };
    for (size_t i = 0; i < geometry->size; i++) ARRLIST_size_t_add(&indices, i);

    // split bvh
    SplitBVH(bvh, 0, geometry, &indices);

    // clean indices
    ARRLIST_size_t_clear(&indices);

    test(bvh);
    LOG_INFO("tested");
    //exit(1);
}