#include "dice.h"
#include "renderer/renderer.h"
#include "renderer/rmath.h"
#include <math.h>

static void CreateD20(void) {
    const float phi = (1.0f + sqrtf(5.0f)) / 2.0f;
    const float a = 0.5f;
    const float b = phi * 0.5f;
    vec3 verts[12] = {
        { -a,  b,  0 }, {  a,  b,  0 }, { -a, -b,  0 }, {  a, -b,  0 },
        {  0, -a,  b }, {  0,  a,  b }, {  0, -a, -b }, {  0,  a, -b },
        {  b,  0, -a }, {  b,  0,  a }, { -b,  0, -a }, { -b,  0,  a },
    };
    uint32_t faces[20][3] = {
        { 0,  11,  5 }, { 0,  5,  1 }, { 0,  1,  7 }, { 0,  7, 10 }, { 0, 10, 11 },
        { 1,  5,  9 }, { 5, 11,  4 }, { 11, 10,  2 }, { 10,  7,  6 }, { 7,  1,  8 },
        { 3,  9,  4 }, { 3,  4,  2 }, {  3,  2,  6 }, {  3,  6,  8 }, { 3,  8,  9 },
        { 4,  9,  5 }, { 2,  4, 11 }, {  6,  2, 10 }, {  8,  6,  7 }, { 9,  8,  1 },
    };
    VertexID vstart = (VertexID)NumVertices();
    TriangleID tstart = (TriangleID)NumTriangles();
    for (int i = 0; i < 12; i++) SubmitVertex(verts[i]);
    for (int i = 0; i < 20; i++) {
        Triangle tri = {
            vstart + faces[i][0],
            vstart + faces[i][1],
            vstart + faces[i][2],
            (uint32_t)-1,
            (uint32_t)-1,
            (uint32_t)-1,
            0,
        };
        SubmitTriangle(tri);
    }
    vec3 min, max, center, extent;
    glm_vec3_copy(verts[0], min);
    glm_vec3_copy(verts[0], max);
    for (int i = 1; i < 12; i++) {
        glm_vec3_minv(verts[i], min, min);
        glm_vec3_maxv(verts[i], max, max);
    }
    glm_vec3_add(max, min, center);
    glm_vec3_scale(center, 0.5f, center);
    glm_vec3_sub(max, min, extent);
    glm_vec3_scale(extent, 0.5f, extent);
    SubmitMeshDescriptor((MeshDescriptor) {
        FALSE,
        vstart, (VertexID)(NumVertices() - 1),
        tstart, (TriangleID)(NumTriangles() - 1),
        0, (uint32_t)-1,
        INLINEV3(center), INLINEV3(extent),
        { 0 }, { 0 },
        { 1.0f, 1.0f, 1.0f },
        GLM_MAT4_IDENTITY_INIT
    }, "D20");
}

static void ImportDesign(const char* path) {
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
    SubmitMeshDescriptor((MeshDescriptor) {
        FALSE, vstart, NumVertices() - 1,
        tstart, NumTriangles() - 1,
        0, (uint32_t)-1, {0, 0, 0}, {1, 1, 1},
        { 0 }, { 0 }, { 1, 1, 1 }, GLM_MAT4_IDENTITY_INIT
    }, "Design");
    EZ_FREE(welded);
    EZ_FREE(stacks);
    UnloadImageColors(colors);
    UnloadImage(image);
    UnloadTexture(tex);
}

static void DrawDicePanel(float width, float height) {
    if (UIButton("Reset", width - 20)) {
        //CreateD20();
        ImportDesign("extensions/dice/assets/png/test.png");
    }
}

void DiceCanvasUpdate(RenderTexture2D canvas, float width, float height) {
}

Panel GenerateDicePanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Dice");
	p.draw = DrawDicePanel;
	return p;
}
