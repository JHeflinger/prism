#include "executor.h"
#include "renderer/processor.h"
#include "renderer/renderer.h"
#include "renderer/trainer.h"
#include "renderer/loader.h"
#include <easylogger.h>
#include <core/binds.h>
#include <easyfile.h>
#include <ui/notification.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

static void JsonEscape(const char* in, char* out, size_t size) {
    size_t o = 0;
    for (size_t i = 0; in && in[i] != '\0' && o + 7 < size; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c < 0x20) o += (size_t)snprintf(out + o, size - o, "\\u%04x", c);
        else out[o++] = (char)c;
    }
    out[o] = '\0';
}

static void InitializeExecutor(int w, int h) {
	OverrideResolution(w, h);
	SetViewportSlice(w, h);
	SetTraceLogLevel(LOG_NONE);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
	InitWindow(1, 1, "Prism Headless Executor");
	InitializeRenderer();
    SetPipelineFlags(HEADLESS_PIPELINE_FLAGS);
    RenderConfig()->async = FALSE;
    printf("\nEnvironment configuration:\n\tGPU: %s\n\tResolution: %dx%d\n\tOperating System: %s\n\n", GPUModel(), w, h, OPSYS);
}

static void ApplyHeadlessExecution() {
    SetPipelineFlags(HEADLESS_PIPELINE_FLAGS);
    RenderConfig()->async = FALSE;
}

// Resizing has to wait for one Render(): the GPU geometry buffers are only reallocated to fit a freshly
// loaded scene inside Render(), and resizing rewrites every descriptor set - doing it first binds
// descriptors sized for the new scene to the old (empty scene) buffers. Render() resizes to the camera
// resolution itself at the end of the frame, after the upload, so one discarded warm-up frame does both
// in the same order the editor does.
static BOOL PrepareRenderer() {
    Vector2 res = RenderConfig()->resolution;
    if (res.x < 1.0f || res.y < 1.0f) {
        printf("PRISM_EXECUTOR_ERROR the scene camera has an invalid resolution (%.0fx%.0f)\n", res.x, res.y);
        fflush(stdout);
        return FALSE;
    }
    Render();
    Vector2 actual = RenderResolution();
    if (actual.x != res.x || actual.y != res.y) {
        printf("PRISM_EXECUTOR_ERROR unable to render at the scene camera's resolution (%.0fx%.0f)\n", res.x, res.y);
        fflush(stdout);
        return FALSE;
    }
    SetViewportSlice((size_t)res.x, (size_t)res.y);
    ResetAccumulation();
    return TRUE;
}

static void PrintRenderSummary(int samples, double seconds) {
    RendererConfig* c = RenderConfig();
    Geometry* g = RendererGeometry();
    char gpu_escaped[512] = { 0 };
    JsonEscape(GPUModel(), gpu_escaped, sizeof(gpu_escaped));
    int w = (int)c->resolution.x, h = (int)c->resolution.y;
    printf("PRISM_RENDER_JSON {\"gpu\":\"%s\",\"os\":\"%s\",\"width\":%d,\"height\":%d,\"samples\":%d,\"render_seconds\":%.6f}\n",
        gpu_escaped, OPSYS, w, h, samples, seconds);
    printf("PRISM_SETTINGS_JSON {\"width\":%d,\"height\":%d,\"samples\":%d,\"camera\":%zu,\"direct_lighting\":%s,\"direct_only\":%s,"
        "\"max_bounces\":%zu,\"max_sss_bounces\":%zu,\"scene_lighting\":%s,\"scene_lighting_only\":%s,\"scene_light_shadows\":%s,"
        "\"spectral\":%s,\"screenspace\":%s,\"splat_shadows\":%s,\"splat_occlusion\":%s,\"gsss\":%s,\"exit_sss\":%s,"
        "\"denoise\":%s,\"denoise_iterations\":%zu,\"whitepoint\":%.4f,\"gamma\":%.4f,\"debug\":%d}\n",
        w, h, samples, g->primarycamera,
        c->direct ? "true" : "false", c->directonly ? "true" : "false",
        c->maxbounces, c->maxsssbounces,
        c->scenelighting ? "true" : "false", c->scenelightingonly ? "true" : "false", c->scenelightshadows ? "true" : "false",
        c->spectral ? "true" : "false", c->screenspace ? "true" : "false", c->splatshadows ? "true" : "false", c->splatocclusion ? "true" : "false",
        c->gsssenabled ? "true" : "false", c->exitsss ? "true" : "false",
        c->denoise ? "true" : "false", c->denoiseiterations, c->whitepoint, c->gamma, (int)c->debug);
    fflush(stdout);
}

static void CleanExecutor() {
    CleanBinds();
    DestroyRenderer();
    // every loginfo/logwarn/logerror also posts a UI notification into a heap list; the editor frees
    // it on shutdown, so the executor has to as well
    CleanNotifications();
}

static void ImportExecuteScene(const char* scenefile) {
    ez_FileType ft = ez_get_filetype(scenefile);
    switch (ft) {
        case DOTOBJ:
            LoadOBJ(scenefile);
            break;
        case DOTPRISM:
            if (!LoadPRISM(scenefile)) {
                printf("PRISM_EXECUTOR_ERROR unable to load \"%s\" - it may have been saved by an incompatible build; re-save it from the editor\n", scenefile);
                fflush(stdout);
                CleanExecutor();
                exit(1);
            }
            break;
        default:
            printf("PRISM_EXECUTOR_ERROR unsupported scene file type \"%s\" (expected .prism or .obj)\n", scenefile);
            fflush(stdout);
            CleanExecutor();
            exit(1);
    }
}

static float RenderSamples(int samples) {
    float time = GetTime();
    for (int i = 0; i < samples; i++) {
        Render();
        float pct = 100.0f * (((float)i + 1) / ((float)samples));
        char backspace_buffer[128] = { 0 };
        char eq_buffer[64] = { 0 };
        char sp_buffer[64] = { 0 };
        char pct_buffer[12] = { 0 };
        char sp2_buffer[12] = { 0 };
        memset(eq_buffer, '=', 64);
        memset(sp_buffer, ' ', 64);
        sprintf(pct_buffer, "%.3f", pct);
        memset(sp2_buffer, ' ', 12);
        memset(backspace_buffer, '\b', 72);
        int b = pct/2;
        eq_buffer[b + 1] = '\0';
        sp_buffer[50 - b] = '\0';
        sp2_buffer[7 - strlen(pct_buffer)] = '\0';
        printf("Progress: [%s%s] %s%%%s", eq_buffer, sp_buffer, pct_buffer, sp2_buffer);
        if (i == samples - 1) printf("\n\n");
        else printf("%s", backspace_buffer);
    }
    return GetTime() - time;
}

static double CompareLuma(const unsigned char* p) {
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
}

static double CompareSSIM(const unsigned char* a, const unsigned char* b, int w, int h) {
    const int win = 8, stride = 4;
    const double c1 = (0.01 * 255.0) * (0.01 * 255.0);
    const double c2 = (0.03 * 255.0) * (0.03 * 255.0);
    if (w < win || h < win) return 1.0;
    double total = 0.0;
    size_t count = 0;
    for (int y = 0; y + win <= h; y += stride) {
        for (int x = 0; x + win <= w; x += stride) {
            double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
            for (int j = 0; j < win; j++) {
                for (int i = 0; i < win; i++) {
                    size_t idx = ((size_t)(y + j) * (size_t)w + (size_t)(x + i)) * 4;
                    double la = CompareLuma(a + idx);
                    double lb = CompareLuma(b + idx);
                    sa += la; sb += lb; saa += la * la; sbb += lb * lb; sab += la * lb;
                }
            }
            const double n = (double)(win * win);
            double ma = sa / n, mb = sb / n;
            double va = saa / n - ma * ma, vb = sbb / n - mb * mb, cov = sab / n - ma * mb;
            total += ((2.0 * ma * mb + c1) * (2.0 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2));
            count++;
        }
    }
    return count > 0 ? total / (double)count : 1.0;
}

void RunSceneRenderExecutor(const char* scenefile, const char* outfile, int samples) {
    size_t memcheck = EZ_ALLOCATED();
    EZ_INFO("Initialzing prism execution suite (scene settings version)...");
    InitializeExecutor(64, 64);
    EZ_INFO("Importing scene...");
    ImportExecuteScene(scenefile);
    ApplyHeadlessExecution();
    if (!PrepareRenderer()) { CleanExecutor(); exit(1); }
    EZ_INFO("Rendering at %.0fx%.0f using the scene's camera settings", RenderConfig()->resolution.x, RenderConfig()->resolution.y);
    EZ_INFO("Running render...\n");
    float time = RenderSamples(samples);
    EZ_INFO("Successfully rendered image in %.3f seconds", time);
    EZ_INFO("Saving render...");
    SaveRender(outfile);
    PrintRenderSummary(samples, (double)time);
    EZ_INFO("Cleaning up and exiting prism execution suite...");
    CleanExecutor();
    EZ_ASSERT(memcheck == EZ_ALLOCATED(), "Memory cleanup revealed a leak of %d bytes", (int)(EZ_ALLOCATED() - memcheck));
}

void RunTrainExecutor(const char* scenefile, const char* outply, int views, int iterations, int width, int height) {
    size_t memcheck = EZ_ALLOCATED();
    EZ_INFO("Initializing prism execution suite (gaussian splat training version)...");
    InitializeExecutor(width, height);
    EZ_INFO("Importing scene...");
    ImportExecuteScene(scenefile);
    TrainingSceneConfig config = TrainingSceneDefaultConfig();
    if (views > 0) config.views = (uint32_t)views;
    if (iterations > 0) config.iterations = (uint32_t)iterations;
    config.output_ply = outply;
    config.replace_splats = FALSE;
    if (!TrainingSceneStart(&config)) {
        EZ_ERROR("Unable to start training: %s", TrainingSceneStatus());
        CleanExecutor();
        exit(1);
    }
    double last_print = -1.0;
    while (TrainingSceneTick(0.25)) {
        double now = GetTime();
        if (now - last_print > 2.0) {
            printf("%s (%.1f%%)\n", TrainingSceneStatus(), 100.0f * TrainingSceneProgress());
            last_print = now;
        }
    }
    BOOL ok = TrainingScenePhaseNow() == TRAINSCENE_DONE;
    printf("%s\n", TrainingSceneStatus());
    EZ_INFO("Cleaning up and exiting prism execution suite...");
    CleanExecutor();
    if (!ok) exit(1);
    EZ_ASSERT(memcheck == EZ_ALLOCATED(), "Memory cleanup revealed a leak of %d bytes", (int)(EZ_ALLOCATED() - memcheck));
}

int RunCompareExecutor(const char* imagefile, const char* referencefile, const char* difffile) {
    SetTraceLogLevel(LOG_NONE);
    if (!FileExists(imagefile)) { EZ_ERROR("Rendered image \"%s\" does not exist", imagefile); return 2; }
    if (!FileExists(referencefile)) { EZ_ERROR("Reference image \"%s\" does not exist", referencefile); return 2; }
    Image a = LoadImage(imagefile);
    Image b = LoadImage(referencefile);
    if (a.data == NULL || b.data == NULL) {
        EZ_ERROR("Unable to decode one of the images being compared");
        if (a.data) UnloadImage(a);
        if (b.data) UnloadImage(b);
        return 2;
    }
    if (a.width != b.width || a.height != b.height) {
        EZ_ERROR("Image dimensions differ (%dx%d vs %dx%d)", a.width, a.height, b.width, b.height);
        printf("PRISM_COMPARE_JSON {\"error\":\"dimension mismatch\",\"width\":%d,\"height\":%d,\"reference_width\":%d,\"reference_height\":%d}\n",
            a.width, a.height, b.width, b.height);
        UnloadImage(a);
        UnloadImage(b);
        return 3;
    }
    ImageFormat(&a, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageFormat(&b, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const unsigned char* pa = (const unsigned char*)a.data;
    const unsigned char* pb = (const unsigned char*)b.data;
    const size_t pixels = (size_t)a.width * (size_t)a.height;
    double se = 0.0, ae = 0.0, maxerr = 0.0;
    size_t differing = 0;
    for (size_t p = 0; p < pixels; p++) {
        int pixel_max = 0;
        for (int c = 0; c < 3; c++) {
            int d = abs((int)pa[p * 4 + c] - (int)pb[p * 4 + c]);
            se += (double)d * (double)d;
            ae += (double)d;
            if (d > pixel_max) pixel_max = d;
        }
        if (pixel_max > 2) differing++;
        if (pixel_max > maxerr) maxerr = pixel_max;
    }
    const double n = (double)pixels * 3.0;
    double mse = se / n;
    double rmse = sqrt(mse) / 255.0;
    double mae = (ae / n) / 255.0;
    double psnr = mse <= 0.0 ? 99.0 : 10.0 * log10((255.0 * 255.0) / mse);
    double ssim = CompareSSIM(pa, pb, a.width, a.height);
    double diffpct = 100.0 * (double)differing / (double)pixels;
    if (difffile != NULL && difffile[0] != '\0') {
        Image d = GenImageColor(a.width, a.height, BLACK);
        ImageFormat(&d, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        unsigned char* pd = (unsigned char*)d.data;
        for (size_t p = 0; p < pixels; p++) {
            for (int c = 0; c < 3; c++) {
                int v = abs((int)pa[p * 4 + c] - (int)pb[p * 4 + c]) * 4;
                pd[p * 4 + c] = (unsigned char)(v > 255 ? 255 : v);
            }
            pd[p * 4 + 3] = 255;
        }
        ExportImage(d, difffile);
        UnloadImage(d);
    }
    printf("PRISM_COMPARE_JSON {\"width\":%d,\"height\":%d,\"mse\":%.6f,\"rmse\":%.8f,\"mae\":%.8f,\"psnr\":%.4f,\"ssim\":%.8f,\"max_error\":%d,\"diff_pixels_pct\":%.4f}\n",
        a.width, a.height, mse, rmse, mae, psnr, ssim, (int)maxerr, diffpct);
    fflush(stdout);
    UnloadImage(a);
    UnloadImage(b);
    return 0;
}
