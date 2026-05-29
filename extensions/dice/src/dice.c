#include "dice.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include <math.h>

static void IcosaFaceCentroid(int face_index, vec3 out) {
    const float phi = (1.0f + sqrtf(5.0f)) / 2.0f;
    const float a = 0.5f;
    const float b = phi * 0.5f;
    vec3 verts[12] = {
        { -a,  b,  0 }, {  a,  b,  0 }, { -a, -b,  0 }, {  a, -b,  0 },
        {  0, -a,  b }, {  0,  a,  b }, {  0, -a, -b }, {  0,  a, -b },
        {  b,  0, -a }, {  b,  0,  a }, { -b,  0, -a }, { -b,  0,  a },
    };
    uint32_t faces[20][3] = {
        { 0, 11,  5 }, { 0,  5,  1 }, { 0,  1,  7 }, { 0,  7, 10 }, { 0, 10, 11 },
        { 1,  5,  9 }, { 5, 11,  4 }, { 11, 10,  2 }, { 10,  7,  6 }, { 7,  1,  8 },
        { 3,  9,  4 }, { 3,  4,  2 }, {  3,  2,  6 }, {  3,  6,  8 }, { 3,  8,  9 },
        { 4,  9,  5 }, { 2,  4, 11 }, {  6,  2, 10 }, {  8,  6,  7 }, { 9,  8,  1 },
    };
    uint32_t* f = faces[face_index];
    out[0] = (verts[f[0]][0] + verts[f[1]][0] + verts[f[2]][0]) / 3.0f;
    out[1] = (verts[f[0]][1] + verts[f[1]][1] + verts[f[2]][1]) / 3.0f;
    out[2] = (verts[f[0]][2] + verts[f[1]][2] + verts[f[2]][2]) / 3.0f;
}

static void IcosaFaceNormal(int face_index, vec3 out) {
    const float phi = (1.0f + sqrtf(5.0f)) / 2.0f;
    const float a = 0.5f;
    const float b = phi * 0.5f;
    vec3 verts[12] = {
        { -a,  b,  0 }, {  a,  b,  0 }, { -a, -b,  0 }, {  a, -b,  0 },
        {  0, -a,  b }, {  0,  a,  b }, {  0, -a, -b }, {  0,  a, -b },
        {  b,  0, -a }, {  b,  0,  a }, { -b,  0, -a }, { -b,  0,  a },
    };
    uint32_t faces[20][3] = {
        { 0, 11,  5 }, { 0,  5,  1 }, { 0,  1,  7 }, { 0,  7, 10 }, { 0, 10, 11 },
        { 1,  5,  9 }, { 5, 11,  4 }, { 11, 10,  2 }, { 10,  7,  6 }, { 7,  1,  8 },
        { 3,  9,  4 }, { 3,  4,  2 }, {  3,  2,  6 }, {  3,  6,  8 }, { 3,  8,  9 },
        { 4,  9,  5 }, { 2,  4, 11 }, {  6,  2, 10 }, {  8,  6,  7 }, { 9,  8,  1 },
    };
    uint32_t* f = faces[face_index];
    vec3 ab, ac;
    glm_vec3_sub(verts[f[1]], verts[f[0]], ab);
    glm_vec3_sub(verts[f[2]], verts[f[0]], ac);
    glm_vec3_cross(ab, ac, out);
    glm_vec3_normalize(out);
}

static void D20FaceTransform(int face_index, mat4 out) {
    vec3 face_normal;
    IcosaFaceNormal(face_index, face_normal);
    const float phi = (1.0f + sqrtf(5.0f)) / 2.0f;
    const float a = 0.5f;
    const float b = phi * 0.5f;
    vec3 verts[12] = {
        { -a,  b,  0 }, {  a,  b,  0 }, { -a, -b,  0 }, {  a, -b,  0 },
        {  0, -a,  b }, {  0,  a,  b }, {  0, -a, -b }, {  0,  a, -b },
        {  b,  0, -a }, {  b,  0,  a }, { -b,  0, -a }, { -b,  0,  a },
    };
    uint32_t faces[20][3] = {
        { 0, 11,  5 }, { 0,  5,  1 }, { 0,  1,  7 }, { 0,  7, 10 }, { 0, 10, 11 },
        { 1,  5,  9 }, { 5, 11,  4 }, { 11, 10,  2 }, { 10,  7,  6 }, { 7,  1,  8 },
        { 3,  9,  4 }, { 3,  4,  2 }, {  3,  2,  6 }, {  3,  6,  8 }, { 3,  8,  9 },
        { 4,  9,  5 }, { 2,  4, 11 }, {  6,  2, 10 }, {  8,  6,  7 }, { 9,  8,  1 },
    };
    uint32_t* f = faces[face_index];
    vec3 edge, dot_n, face_tangent;
    glm_vec3_sub(verts[f[1]], verts[f[0]], edge);
    glm_vec3_scale(face_normal, glm_vec3_dot(edge, face_normal), dot_n);
    glm_vec3_sub(edge, dot_n, face_tangent);
    glm_vec3_normalize(face_tangent);
    vec3 face_bitangent;
    glm_vec3_cross(face_normal, face_tangent, face_bitangent);
    mat4 R = GLM_MAT4_IDENTITY_INIT;
    R[0][0] = face_tangent[0];
    R[0][1] = face_tangent[1];
    R[0][2] = face_tangent[2];
    R[1][0] = face_bitangent[0];
    R[1][1] = face_bitangent[1];
    R[1][2] = face_bitangent[2];
    R[2][0] = face_normal[0];
    R[2][1] = face_normal[1];
    R[2][2] = face_normal[2];
    vec3 centroid;
    IcosaFaceCentroid(face_index, centroid);
    mat4 T = GLM_MAT4_IDENTITY_INIT;
    glm_translate(T, centroid);
    glm_mat4_mul(T, R, out);
}

static void ImportDesign(const char* path, size_t face) {
    Texture2D tex = LoadTexture(path);
    Image image = LoadImageFromTexture(tex);
    Color* colors = LoadImageColors(image);
    const float depth = 0.5f;
    const float standard = 1.0f;
    const float sidelen = standard / tex.height;
    const size_t vcount = ((tex.height + 1) * (tex.height + 2)) / 2;
    inline Color pixel(int x, int y) { return colors[y * image.width + x]; }
    vec3* welded = EZ_ALLOC(vcount, sizeof(vec3));
    size_t* stacks = EZ_ALLOC(vcount, sizeof(size_t));
    vec3 startpoint, leftstep, rightstep;
    vec3 verts[3] = {{0, standard / sqrt(3.0f), 0}, {-standard / 2.0f, -standard / (2.0f * sqrt(3.0f)), 0}, {standard / 2.0f, -standard / (2.0f * sqrt(3.0f)), 0}};
    glm_vec3_copy(verts[0], startpoint);
    glm_vec3_sub(verts[1], verts[0], leftstep);
    glm_vec3_sub(verts[2], verts[1], rightstep);
    glm_vec3_scale(leftstep, sidelen, leftstep);
    glm_vec3_scale(rightstep, sidelen, rightstep);
    VertexID vstart = (VertexID)NumVertices();
    TriangleID tstart = (TriangleID)NumTriangles();
    for (size_t row = 0, mc = 1; row < (size_t)tex.height; row++, mc += 2) {
        vec3 rowpoint;
        glm_vec3_scale(leftstep, row, rowpoint);
        glm_vec3_add(rowpoint, startpoint, rowpoint);
        for (size_t col = 0; col < mc; col++) {
            #define VERTA welded[((row*(row+1))/2) + (roundcol/2)]
            #define VERTB welded[rounded ? ((((row+1)*(row+2))/2) + (roundcol/2)) : (((row*(row+1))/2) + (roundcol/2) + 1)]
            #define VERTC welded[(((row+1)*(row+2))/2) + (roundcol/2) + 1]
            #define STACKA stacks[((row*(row+1))/2) + (roundcol/2)]
            #define STACKB stacks[rounded ? ((((row+1)*(row+2))/2) + (roundcol/2)) : (((row*(row+1))/2) + (roundcol/2) + 1)]
            #define STACKC stacks[(((row+1)*(row+2))/2) + (roundcol/2) + 1]
            const BOOL rounded = col%2 == 0;
            const size_t roundcol = rounded ? col : col - 1;
            vec3 a, b, c;
            glm_vec3_scale(rightstep, roundcol / 2.0f, a);
            glm_vec3_add(a, rowpoint, a);
            glm_vec3_add(a, leftstep, b);
            glm_vec3_add(b, rightstep, c);
            if (!rounded) glm_vec3_add(a, rightstep, b);
            float alpha = (((float)pixel(tex.height - row + col - 1, row).a) * depth) / -255.0f;
            float* zvals[3] = { &(VERTA[2]), &(VERTB[2]), &(VERTC[2]) };
            size_t* stackvals[3] = { &(STACKA), &(STACKB), &(STACKC) };
            for (int i = 0; i < 3; i++) {
                size_t newstack = *(stackvals[i]) + 1;
                float majority = ((float)(*(stackvals[i]))) / ((float)newstack);
                float minority = 1.0f / ((float)newstack);
                float broad = *(zvals[i]) * majority;
                float additive = alpha * minority;
                *zvals[i] = broad + additive;
                *stackvals[i] = newstack;
            }
            if (row == 0 && col == 0) memcpy(VERTA, a, sizeof(vec3));
            if (col == 0) memcpy(VERTB, b, sizeof(vec3));
            memcpy(VERTC, c, sizeof(vec3));
            #undef VERTA
            #undef VERTB
            #undef VERTC
        }
    }
    for (size_t i = 0; i < vcount; i++) SubmitVertex(welded[i]);
    for (size_t row = 0, mc = 1; row < (size_t)tex.height; row++, mc += 2) {
        for (size_t col = 0; col < mc; col++) {
            const BOOL rounded = col%2 == 0;
            const size_t roundcol = rounded ? col : col - 1;
            SubmitTriangle((Triangle){
                vstart + ((row*(row+1))/2) + (roundcol/2),
                vstart + (rounded ? ((((row+1)*(row+2))/2) + (roundcol/2)) : (((row*(row+1))/2) + (roundcol/2) + 1)),
                vstart + (((row+1)*(row+2))/2) + (roundcol/2) + 1,
                (uint32_t)-1,
                (uint32_t)-1,
                (uint32_t)-1, 0 });
        }
    }
    MeshDescriptor md = (MeshDescriptor) {
        FALSE, vstart, NumVertices() - 1,
        tstart, NumTriangles() - 1,
        0, (uint32_t)-1, {0, 0, 0}, {1, 1, 1},
        { 0 }, { 0 }, { 1, 1, 1 }, GLM_MAT4_IDENTITY_INIT
    };
    D20FaceTransform(face, md.transform); // NOTE: this does not update the vec3 translate, scale, and rotate values of the md
    SubmitMeshDescriptor(md, "Design");
    EZ_FREE(welded);
    EZ_FREE(stacks);
    UnloadImageColors(colors);
    UnloadImage(image);
    UnloadTexture(tex);
}

static void DrawDicePanel(float width, float height) {
    if (UIButton("Reset", width - 20)) {
        for (size_t i = 0; i < 20; i++) ImportDesign("extensions/dice/assets/png/test.png", i);
    }
}

Panel GenerateDicePanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Dice");
	p.draw = DrawDicePanel;
	return p;
}
