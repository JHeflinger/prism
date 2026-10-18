#ifndef TRAINER_H
#define TRAINER_H

#include "renderer/rstructs.h"
#include <stdint.h>
#include <tgpu.h>

typedef struct {
    uint32_t iterations;
    uint32_t max_gaussians;
    float lr_position_init; // multiplied by scene radius
    float lr_position_final; // multiplied by scene radius
    float lr_scale;
    float lr_rotation;
    float lr_opacity;
    float lr_color;
    float lambda_ssim;
    float lambda_alpha; // weight of the silhouette (alpha) loss, keeps empty space empty
    uint32_t densify_from;
    uint32_t densify_until;
    uint32_t densify_interval;
    uint32_t opacity_reset_interval;
    float densify_grad_threshold; // average screen space gradient (NDC units)
    float percent_dense; // split vs clone threshold as a fraction of scene radius
    float min_opacity;
    float max_world_scale; // prune gaussians bigger than this fraction of scene radius
    float max_screen_radius; // prune gaussians covering more than this fraction of image width (0 = off)
    vec3 background;
    uint32_t log_interval;
    int gpu_index; // -1 = auto
} TrainerConfig;

typedef struct {
    vec3 eye;
    vec3 right;
    vec3 down;
    vec3 fwd;
    float focal;
    float depth_min, depth_scale;
} TrainerView;

typedef struct {
    TGPUBuffer params;
    TGPUBuffer grads;
    TGPUBuffer m;
    TGPUBuffer v;
    TGPUBuffer geom;
    TGPUBuffer counts;
    TGPUBuffer offsets;
    TGPUBuffer blocks;
    TGPUBuffer keys_a;
    TGPUBuffer vals_a;
    TGPUBuffer keys_b;
    TGPUBuffer vals_b;
    TGPUBuffer hist;
    TGPUBuffer ranges;
    TGPUBuffer meta;
    TGPUBuffer render;
    TGPUBuffer nconts;
    TGPUBuffer gt;
    TGPUBuffer dldpix;
    TGPUBuffer ssim;
    TGPUBuffer grad2d;
    TGPUBuffer dens;
    TGPUBuffer actions;
    TGPUBuffer loss;
} TrainerBuffers;

typedef struct {
    TGPUKernel preprocess;
    TGPUKernel scan_block;
    TGPUKernel scan_top;
    TGPUKernel scan_add;
    TGPUKernel setup;
    TGPUKernel duplicate;
    TGPUKernel sort_hist;
    TGPUKernel sort_scan;
    TGPUKernel sort_scatter;
    TGPUKernel ranges;
    TGPUKernel render_fwd;
    TGPUKernel render_bwd;
    TGPUKernel loss_stats;
    TGPUKernel loss_grad;
    TGPUKernel preprocess_bwd;
    TGPUKernel adam;
    TGPUKernel densify_flags;
    TGPUKernel densify_apply;
    TGPUKernel opacity_reset;
} TrainerKernels;

typedef struct {
    TrainerConfig cfg;
    TGPU gpu;
    int ready;
    uint32_t width;
    uint32_t height;
    uint32_t tiles_x;
    uint32_t tiles_y;
    uint32_t num_views;
    TrainerView* views;
    uint32_t* view_order;
    uint32_t order_cursor;
    vec3 scene_center;
    float scene_radius;
    uint32_t count;
    uint32_t capacity;
    uint32_t pair_capacity;
    uint32_t iteration;
    uint32_t rng;
    float last_loss;
    float last_l1;
    float last_ssim;
    uint32_t last_pairs;
    // When > 0 the training images were tonemapped (extended Reinhard + gamma) and exported gaussian colours are
    // converted back to linear radiance, which is what the path tracer expects splat albedo to be.
    float export_whitepoint;
    float export_gamma;
    int pairs_truncated;
    uint32_t sampled_since_check;
    int sorted_in_a;
    TrainerBuffers buffers;
    TrainerKernels kernels;
} Trainer;

typedef enum {
    TRAINSCENE_CAPTURE_PREVIEW = 0,
    TRAINSCENE_CAPTURE_PATHTRACE = 1
} TrainingSceneCapture;

typedef enum {
    TRAINSCENE_IDLE = 0,
    TRAINSCENE_CAPTURING,
    TRAINSCENE_TRAINING,
    TRAINSCENE_DONE,
    TRAINSCENE_FAILED
} TrainingScenePhase;

typedef struct {
    uint32_t views;
    uint32_t train_width;
    uint32_t samples;
    uint32_t init_points;
    uint32_t iterations;
    uint32_t max_gaussians;
    float fov;
    float distance_scale;
    float min_elevation;
    TrainingSceneCapture capture;
    BOOL replace_splats;
    const char* output_ply;
    TrainerConfig trainer;
} TrainingSceneConfig;

typedef struct {
    float eye[3];
    float right[3];
    float down[3];
    float fwd[3];
    float focal; // in pixels of the (training resolution) image
} MaskCamera;

TrainerConfig DefaultTrainerConfig();

BOOL InitializeTrainer(Trainer* t, const TrainerConfig* cfg, uint32_t width, uint32_t height, uint32_t views);

void DestroyTrainer(Trainer* t);

void SetTrainerScene(Trainer* t, const vec3 center, float radius);

BOOL SetTrainerView(Trainer* t, uint32_t index, const SimpleCamera* cam, const uint8_t* rgba);

BOOL SetTrainerGaussians(Trainer* t, const GaussianSplat* g, uint32_t count);

BOOL TrainerDone(const Trainer* t);

BOOL StepTrainer(Trainer* t);

uint32_t DownloadFromTrainer(Trainer* t, GaussianSplat* out, uint32_t max);

BOOL SaveTrainerPLY(Trainer* t, const char* path);

BOOL RenderTrainerView(Trainer* t, uint32_t view, float* out);

BOOL EvaluateTrainerPSNR(Trainer* t, double* psnr);

BOOL LossAndGradients(Trainer* t, uint32_t view, double* loss, float* grads);

TrainingSceneConfig TrainingSceneDefaultConfig();

BOOL TrainingSceneStart(const TrainingSceneConfig* config);

BOOL TrainingSceneTick(double seconds);

void TrainingSceneCancel();

TrainingScenePhase TrainingScenePhaseNow();

float TrainingSceneProgress();

const char* TrainingSceneStatus();

BOOL TrainingSceneRunBlocking(const TrainingSceneConfig* config);

#endif
