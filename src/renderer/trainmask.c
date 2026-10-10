#include "trainmask.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define MASK_NEAR 0.02f
#define MASK_MIN_FACING 0.25f
#define MASK_MIN_ALPHA_BYTE 242

typedef struct {
    float x, y, z;
} CamPt;

static float Dot3(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void ToCamera(const MaskCamera* c, const float* p, CamPt* out) {
    float r[3] = { p[0] - c->eye[0], p[1] - c->eye[1], p[2] - c->eye[2] };
    out->x = Dot3(r, c->right);
    out->y = Dot3(r, c->down);
    out->z = Dot3(r, c->fwd);
}

size_t MaskScratchSize(uint32_t w, uint32_t h) {
    return (size_t)w * MASK_SUPERSAMPLE * (size_t)h * MASK_SUPERSAMPLE;
}

// Rasterizes one camera space triangle (all z >= MASK_NEAR) into the supersampled depth buffer.
static void RasterTriangle(const CamPt* a, const CamPt* b, const CamPt* c, float focal, float cx, float cy,
    uint32_t sw, uint32_t sh, float* zbuf) {
    const float S = (float)MASK_SUPERSAMPLE;
    const CamPt* p[3] = { a, b, c };
    float X[3], Y[3], Z[3];
    for (int k = 0; k < 3; k++) {
        X[k] = (focal * p[k]->x / p[k]->z + cx) * S;
        Y[k] = (focal * p[k]->y / p[k]->z + cy) * S;
        Z[k] = p[k]->z;
    }
    float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (fabsf(area) < 1e-12f) return;
    float inv_area = 1.0f / area;
    float minx = fminf(X[0], fminf(X[1], X[2])), maxx = fmaxf(X[0], fmaxf(X[1], X[2]));
    float miny = fminf(Y[0], fminf(Y[1], Y[2])), maxy = fmaxf(Y[0], fmaxf(Y[1], Y[2]));
    // clamp in float space first, vertices can project absurdly far away when they are close to the near plane
    float fx0 = fmaxf(0.0f, floorf(minx - 0.5f)), fx1 = fminf((float)sw - 1.0f, ceilf(maxx - 0.5f));
    float fy0 = fmaxf(0.0f, floorf(miny - 0.5f)), fy1 = fminf((float)sh - 1.0f, ceilf(maxy - 0.5f));
    if (fx0 > fx1 || fy0 > fy1) return;
    int x0 = (int)fx0, x1 = (int)fx1, y0 = (int)fy0, y1 = (int)fy1;
    float iz0 = 1.0f / Z[0], iz1 = 1.0f / Z[1], iz2 = 1.0f / Z[2];
    const float eps = -1e-5f;
    for (int y = y0; y <= y1; y++) {
        float py = (float)y + 0.5f;
        for (int x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f;
            float l0 = ((X[1] - px) * (Y[2] - py) - (X[2] - px) * (Y[1] - py)) * inv_area;
            float l1 = ((px - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (py - Y[0])) * inv_area;
            float l2 = ((X[1] - X[0]) * (py - Y[0]) - (px - X[0]) * (Y[1] - Y[0])) * inv_area;
            if (l0 < eps || l1 < eps || l2 < eps) continue;
            float z = 1.0f / (l0 * iz0 + l1 * iz1 + l2 * iz2);
            float* dst = &zbuf[(size_t)y * sw + (size_t)x];
            if (z < *dst) *dst = z;
        }
    }
}

// Clips a camera space triangle against the near plane and rasterizes what is left.
static void RasterClipped(const CamPt* v, float focal, float cx, float cy, uint32_t sw, uint32_t sh, float* zbuf) {
    CamPt poly[4];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const CamPt* cur = &v[i];
        const CamPt* nxt = &v[(i + 1) % 3];
        int cur_in = cur->z >= MASK_NEAR, nxt_in = nxt->z >= MASK_NEAR;
        if (cur_in) poly[n++] = *cur;
        if (cur_in != nxt_in) {
            float t = (MASK_NEAR - cur->z) / (nxt->z - cur->z);
            poly[n].x = cur->x + t * (nxt->x - cur->x);
            poly[n].y = cur->y + t * (nxt->y - cur->y);
            poly[n].z = MASK_NEAR;
            n++;
        }
    }
    if (n < 3) return;
    RasterTriangle(&poly[0], &poly[1], &poly[2], focal, cx, cy, sw, sh, zbuf);
    if (n == 4) RasterTriangle(&poly[0], &poly[2], &poly[3], focal, cx, cy, sw, sh, zbuf);
}

void RasterizeCoverage(const float (*tris)[3][3], size_t tri_count, const MaskCamera* cam, uint32_t w, uint32_t h,
    float* scratch, float* coverage, float* depth) {
    const uint32_t S = MASK_SUPERSAMPLE;
    uint32_t sw = w * S, sh = h * S;
    size_t samples = (size_t)sw * sh;
    for (size_t i = 0; i < samples; i++) scratch[i] = FLT_MAX;
    float cx = (float)w * 0.5f, cy = (float)h * 0.5f;
    for (size_t t = 0; t < tri_count; t++) {
        CamPt v[3];
        for (int k = 0; k < 3; k++) ToCamera(cam, tris[t][k], &v[k]);
        // cheap reject: entirely behind the near plane
        if (v[0].z < MASK_NEAR && v[1].z < MASK_NEAR && v[2].z < MASK_NEAR) continue;
        RasterClipped(v, cam->focal, cx, cy, sw, sh, scratch);
    }
    float inv = 1.0f / (float)(S * S);
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t hits = 0;
            float zmin = FLT_MAX;
            for (uint32_t sy = 0; sy < S; sy++) {
                for (uint32_t sx = 0; sx < S; sx++) {
                    float z = scratch[(size_t)(y * S + sy) * sw + (x * S + sx)];
                    if (z < FLT_MAX) {
                        hits++;
                        if (z < zmin) zmin = z;
                    }
                }
            }
            coverage[(size_t)y * w + x] = (float)hits * inv;
            depth[(size_t)y * w + x] = zmin;
        }
    }
}

void ApplyCoverageToImage(uint8_t* rgba, const float* coverage, uint32_t w, uint32_t h, float* bg_out) {
    // measure the background from pixels that are certainly empty (and so are their neighbours)
    double sum[3] = { 0, 0, 0 };
    double count = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            size_t i = (size_t)y * w + x;
            if (coverage[i] > 0.0f) continue;
            if (x > 0 && coverage[i - 1] > 0.0f) continue;
            if (x + 1 < w && coverage[i + 1] > 0.0f) continue;
            if (y > 0 && coverage[i - w] > 0.0f) continue;
            if (y + 1 < h && coverage[i + w] > 0.0f) continue;
            for (int k = 0; k < 3; k++) sum[k] += rgba[i * 4 + k] / 255.0;
            count += 1.0;
        }
    }
    float bg[3] = { 0, 0, 0 };
    if (count > 0.0) for (int k = 0; k < 3; k++) bg[k] = (float)(sum[k] / count);
    if (bg_out) memcpy(bg_out, bg, sizeof(bg));
    for (size_t i = 0; i < (size_t)w * h; i++) {
        float a = coverage[i];
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        for (int k = 0; k < 3; k++) {
            float v = rgba[i * 4 + k] / 255.0f - (1.0f - a) * bg[k];
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            rgba[i * 4 + k] = (uint8_t)(v * 255.0f + 0.5f);
        }
        rgba[i * 4 + 3] = (uint8_t)(a * 255.0f + 0.5f);
    }
}

void AccumulateSeedColors(const float* geo, uint32_t count, const MaskCamera* cam, uint32_t w, uint32_t h,
    const uint8_t* rgba, const float* depth, float scene_radius, float* acc) {
    float cx = (float)w * 0.5f, cy = (float)h * 0.5f;
    for (uint32_t i = 0; i < count; i++) {
        const float* pos = &geo[(size_t)i * 6];
        const float* nrm = &geo[(size_t)i * 6 + 3];
        CamPt c;
        ToCamera(cam, pos, &c);
        if (c.z <= MASK_NEAR) continue;
        float sx = cam->focal * c.x / c.z + cx;
        float sy = cam->focal * c.y / c.z + cy;
        if (!(sx >= 0.0f && sy >= 0.0f && sx < (float)w && sy < (float)h)) continue;
        int px = (int)sx, py = (int)sy;
        // surface orientation relative to the viewer (normals have an arbitrary sign, so use |cos|)
        float to_eye[3] = { cam->eye[0] - pos[0], cam->eye[1] - pos[1], cam->eye[2] - pos[2] };
        float len = sqrtf(Dot3(to_eye, to_eye));
        if (len < 1e-8f) continue;
        float facing = fabsf(Dot3(nrm, to_eye)) / len;
        if (facing < MASK_MIN_FACING) continue;
        // visibility: nothing noticeably closer to the camera around this pixel
        float zmin = FLT_MAX;
        for (int dy = -1; dy <= 1; dy++) {
            int qy = py + dy;
            if (qy < 0 || qy >= (int)h) continue;
            for (int dx = -1; dx <= 1; dx++) {
                int qx = px + dx;
                if (qx < 0 || qx >= (int)w) continue;
                float z = depth[(size_t)qy * w + (size_t)qx];
                if (z < zmin) zmin = z;
            }
        }
        float tan_theta = sqrtf(fmaxf(0.0f, 1.0f - facing * facing)) / facing;
        float tol = 0.005f * scene_radius + 2.5f * (c.z / cam->focal) * tan_theta;
        if (c.z - zmin > tol) continue;
        const uint8_t* px_rgba = &rgba[((size_t)py * w + (size_t)px) * 4];
        if (px_rgba[3] < MASK_MIN_ALPHA_BYTE) continue; // silhouette pixel, mixed with the background
        float wgt = facing * facing;
        float* a = &acc[(size_t)i * 4];
        a[0] += wgt * (px_rgba[0] / 255.0f);
        a[1] += wgt * (px_rgba[1] / 255.0f);
        a[2] += wgt * (px_rgba[2] / 255.0f);
        a[3] += wgt;
    }
}
