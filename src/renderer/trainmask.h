#ifndef TRAINMASK_H
#define TRAINMASK_H

#include <stdint.h>
#include <stddef.h>

// CPU helpers used while capturing training views for the gaussian splat trainer. They have no GPU
// dependencies on purpose. Given the scene triangles and the exact pinhole camera the trainer uses, they
// produce
//   - a per pixel coverage mask (how much of the pixel is covered by scene geometry), which tells us
//     which pixels are "the path hit nothing" background and must not be explained by gaussians
//   - a per pixel nearest depth, used to decide which seed gaussians a view can really see so their
//     colors can be initialised from the captured images.

#define MASK_SUPERSAMPLE 3

typedef struct {
    float eye[3];
    float right[3];
    float down[3];
    float fwd[3];
    float focal; // in pixels of the (training resolution) image
} MaskCamera;

// number of floats RasterizeCoverage needs as scratch space for a w x h image
size_t MaskScratchSize(uint32_t w, uint32_t h);

// tris: tri_count world space triangles. coverage and depth are w * h floats. depth is FLT_MAX where nothing
// was hit. scratch has MaskScratchSize(w, h) floats.
void RasterizeCoverage(const float (*tris)[3][3], size_t tri_count, const MaskCamera* cam, uint32_t w, uint32_t h,
    float* scratch, float* coverage, float* depth);

// Turns a captured RGBA8 image into premultiplied form: the colour of the empty background (measured from
// pixels the mask says nothing covers) is removed, and the alpha channel is replaced by the coverage.
// Afterwards  rgb + (1 - a) * anyBackground  is the image as it would look in front of that background.
// bg_out (optional) receives the measured background colour.
void ApplyCoverageToImage(uint8_t* rgba, const float* coverage, uint32_t w, uint32_t h, float* bg_out);

// Adds, for every seed, the colour this view sees at the seed's surface point. geo holds 6 floats per seed
// (position, surface normal). acc holds 4 floats per seed (summed weighted rgb, summed weight). Views that
// cannot see the seed (behind another surface, grazing angle, silhouette pixel) add nothing.
void AccumulateSeedColors(const float* geo, uint32_t count, const MaskCamera* cam, uint32_t w, uint32_t h,
    const uint8_t* rgba, const float* depth, float scene_radius, float* acc);

#endif
