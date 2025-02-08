#include "rutils.h"
#include "core/log.h"

#define BVH_LIMIT 0.01f

IMPL_ARRLIST(size_t);

void ResizeBVH(ARRLIST_NodeBVH* bvh, size_t index) {
    if (bvh->data[index].branch_config == BVH_BOTH) {
        ResizeBVH(bvh, bvh->data[index].left);
        ResizeBVH(bvh, bvh->data[index].right);
        glm_vec3_minv(
            bvh->data[index].min,
            bvh->data[bvh->data[index].left].min,
            bvh->data[index].min);
        glm_vec3_maxv(
            bvh->data[index].max,
            bvh->data[bvh->data[index].left].max,
            bvh->data[index].max);
        glm_vec3_minv(
            bvh->data[index].min,
            bvh->data[bvh->data[index].right].min,
            bvh->data[index].min);
        glm_vec3_maxv(
            bvh->data[index].max,
            bvh->data[bvh->data[index].right].max,
            bvh->data[index].max);
    } else if (bvh->data[index].branch_config == BVH_LEFT_ONLY) {
        ResizeBVH(bvh, bvh->data[index].left);
        glm_vec3_minv(
            bvh->data[index].min,
            bvh->data[bvh->data[index].left].min,
            bvh->data[index].min);
        glm_vec3_maxv(
            bvh->data[index].max,
            bvh->data[bvh->data[index].left].max,
            bvh->data[index].max);
    } else if (bvh->data[index].branch_config == BVH_RIGHT_ONLY) {
        ResizeBVH(bvh, bvh->data[index].right);
        glm_vec3_minv(
            bvh->data[index].min,
            bvh->data[bvh->data[index].right].min,
            bvh->data[index].min);
        glm_vec3_maxv(
            bvh->data[index].max,
            bvh->data[bvh->data[index].right].max,
            bvh->data[index].max);
    } else {
        return;
    }
}

void SplitBVH(ARRLIST_NodeBVH* bvh, size_t index, ARRLIST_TriangleBB* geometry, ARRLIST_size_t* children) {
    #define CBVH bvh->data[index]
    #define BVHMIN bvh->data[index].min
    #define BVHMAX bvh->data[index].max
    #define COPYVEC(v1, v2) { v1[0] = v2[0]; v1[1] = v2[1]; v1[2] = v2[2]; }

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
        size_t stream_index = index;
        for (size_t i = 0; i < children->size; i++) {
            NodeBVH child = {
                { geometry->data[children->data[i]].min[0],
                  geometry->data[children->data[i]].min[1],
                  geometry->data[children->data[i]].min[2] },
                { geometry->data[children->data[i]].max[0],
                  geometry->data[children->data[i]].max[1],
                  geometry->data[children->data[i]].max[2] },
                BVH_LEAF, children->data[i], 0
            };
            ARRLIST_NodeBVH_add(bvh, child);
            bvh->data[stream_index].left = bvh->size - 1;

            if (i + 1 >= children->size) {
                bvh->data[stream_index].branch_config = BVH_LEFT_ONLY;
            } else {
                bvh->data[stream_index].branch_config = BVH_BOTH;
                NodeBVH next = {
                    { BVHMIN[0], BVHMIN[1], BVHMIN[2] },
                    { BVHMAX[0], BVHMAX[1], BVHMAX[2] },
                    BVH_LEAF, 0, 0
                };
                ARRLIST_NodeBVH_add(bvh, next);
                bvh->data[stream_index].right = bvh->size - 1;
                stream_index = bvh->size - 1;
            }
        }
        ResizeBVH(bvh, index);
        return;
    }
    float mid_dist = xyz_dist[dist_ind] / 2.0f;
    NodeBVH left = {
        { BVHMIN[0], BVHMIN[1], BVHMIN[2] },
        { BVHMAX[0], BVHMAX[1], BVHMAX[2] },
        BVH_LEAF, 0, 0
    };
    NodeBVH right = {
        { BVHMIN[0], BVHMIN[1], BVHMIN[2] },
        { BVHMAX[0], BVHMAX[1], BVHMAX[2] },
        BVH_LEAF, 0, 0
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
        CBVH.branch_config = BVH_BOTH;
    } else if (left_children.size > 0) {
        CBVH.branch_config = BVH_LEFT_ONLY;
    } else if (right_children.size > 0) {
        CBVH.branch_config = BVH_RIGHT_ONLY;
    } else {
        LOG_FATAL("This should never happen");
        CBVH.branch_config = BVH_LEAF;
    }
    if (left_children.size > 1) {
        ARRLIST_NodeBVH_add(bvh, left);
        CBVH.left = bvh->size - 1;
        SplitBVH(bvh, CBVH.left, geometry, &left_children);
    } else if (left_children.size == 1) {
        left.branch_config = BVH_LEAF;
        left.left = left_children.data[0];
        COPYVEC(left.min, geometry->data[left.left].min);
        COPYVEC(left.max, geometry->data[left.left].max);
        ARRLIST_NodeBVH_add(bvh, left);
        CBVH.left = bvh->size - 1;
    }
    if (right_children.size > 1) {
        ARRLIST_NodeBVH_add(bvh, right);
        CBVH.right = bvh->size - 1;
        SplitBVH(bvh, CBVH.right, geometry, &right_children);
    } else if (right_children.size == 1) {
        right.branch_config = BVH_LEAF;
        right.left = right_children.data[0];
        COPYVEC(right.min, geometry->data[right.left].min);
        COPYVEC(right.max, geometry->data[right.left].max);
        ARRLIST_NodeBVH_add(bvh, right);
        CBVH.right = bvh->size - 1;
    }
    ARRLIST_size_t_clear(&left_children);
    ARRLIST_size_t_clear(&right_children);

    // resize
    if (bvh->data[index].branch_config == BVH_BOTH || bvh->data[index].branch_config == BVH_LEFT_ONLY) {
        glm_vec3_minv(
            bvh->data[index].min,
            bvh->data[bvh->data[index].left].min,
            bvh->data[index].min);
        glm_vec3_maxv(
            bvh->data[index].max,
            bvh->data[bvh->data[index].left].max,
            bvh->data[index].max);
    }
    if (bvh->data[index].branch_config == BVH_BOTH || bvh->data[index].branch_config == BVH_RIGHT_ONLY) {
        glm_vec3_minv(
            bvh->data[index].min,
            bvh->data[bvh->data[index].right].min,
            bvh->data[index].min);
        glm_vec3_maxv(
            bvh->data[index].max,
            bvh->data[bvh->data[index].right].max,
            bvh->data[index].max);
    }
    
    
    #undef CBVH
    #undef BVHMIN
    #undef BVHMAX
    #undef COPYVEC
}

size_t CountBVH(ARRLIST_NodeBVH* bvh, size_t index) {
    if (bvh->data[index].branch_config == BVH_LEAF) return 1;
    if (bvh->data[index].branch_config == BVH_LEFT_ONLY) return CountBVH(bvh, bvh->data[index].left);
    if (bvh->data[index].branch_config == BVH_RIGHT_ONLY) return CountBVH(bvh, bvh->data[index].right);
    if (bvh->data[index].branch_config == BVH_BOTH) return CountBVH(bvh, bvh->data[index].left) + CountBVH(bvh, bvh->data[index].right);
    return 0;
}

void RUTIL_BoundingVolumeHierarchy(ARRLIST_NodeBVH* bvh, ARRLIST_TriangleBB* geometry) {
    // clear old bvh
    ARRLIST_NodeBVH_clear(bvh);

    // create root
    NodeBVH root = {
        { FLT_MAX, FLT_MAX, FLT_MAX },
        { -FLT_MAX, -FLT_MAX, -FLT_MAX },
        BVH_LEAF, 0, 0
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
}