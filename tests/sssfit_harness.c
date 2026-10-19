// Standalone harness for src/renderer/sssfit.c: fits synthetic meshes and prints the model vs reference walk accuracy
// report. No GPU or renderer needed. From the project root:
//   gcc -O2 -DSSSFIT_STANDALONE -Isrc tests/sssfit_harness.c src/renderer/sssfit.c -lm -o sssfit_harness
//   ./sssfit_harness [walks] [lobes] [surface gaussians] [x = sphere only]
#include "renderer/sssfit.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

typedef struct { float* v; uint32_t* t; uint32_t nv, nt; } Mesh;

static void add_v(Mesh* m, float x, float y, float z) {
    m->v = realloc(m->v, (m->nv + 1) * 12);
    m->v[m->nv * 3] = x; m->v[m->nv * 3 + 1] = y; m->v[m->nv * 3 + 2] = z;
    m->nv++;
}
static void add_t(Mesh* m, uint32_t a, uint32_t b, uint32_t c) {
    m->t = realloc(m->t, (m->nt + 1) * 12);
    m->t[m->nt * 3] = a; m->t[m->nt * 3 + 1] = b; m->t[m->nt * 3 + 2] = c;
    m->nt++;
}

// uv sphere with deliberately random triangle winding (exercises the outward normal parity test)
static Mesh sphere(float r, int seg) {
    Mesh m = { 0 };
    for (int i = 0; i <= seg; i++) {
        float th = M_PI * i / seg;
        for (int j = 0; j < 2 * seg; j++) {
            float ph = 2 * M_PI * j / (2 * seg);
            add_v(&m, r * sinf(th) * cosf(ph), r * cosf(th), r * sinf(th) * sinf(ph));
        }
    }
    srand(3);
    for (int i = 0; i < seg; i++)
        for (int j = 0; j < 2 * seg; j++) {
            uint32_t a = i * 2 * seg + j, b = i * 2 * seg + (j + 1) % (2 * seg);
            uint32_t c = (i + 1) * 2 * seg + j, d = (i + 1) * 2 * seg + (j + 1) % (2 * seg);
            if (rand() & 1) add_t(&m, a, b, c); else add_t(&m, a, c, b);
            if (rand() & 1) add_t(&m, b, d, c); else add_t(&m, b, c, d);
        }
    return m;
}

// subdivided box (sx, sy, sz half extents), n cells per unit edge length
static Mesh box(float sx, float sy, float sz, int n) {
    Mesh m = { 0 };
    float h[3] = { sx, sy, sz };
    for (int axis = 0; axis < 3; axis++)
        for (int side = -1; side <= 1; side += 2) {
            int u = (axis + 1) % 3, v = (axis + 2) % 3;
            int nu = (int)ceilf(2 * h[u] * n), nvv = (int)ceilf(2 * h[v] * n);
            if (nu < 1) nu = 1;
            if (nvv < 1) nvv = 1;
            uint32_t base = m.nv;
            for (int i = 0; i <= nu; i++)
                for (int j = 0; j <= nvv; j++) {
                    float p[3];
                    p[axis] = side * h[axis];
                    p[u] = -h[u] + 2 * h[u] * i / nu;
                    p[v] = -h[v] + 2 * h[v] * j / nvv;
                    add_v(&m, p[0], p[1], p[2]);
                }
            for (int i = 0; i < nu; i++)
                for (int j = 0; j < nvv; j++) {
                    uint32_t a = base + i * (nvv + 1) + j, b = a + nvv + 1, c = a + 1, d = b + 1;
                    add_t(&m, a, b, c);
                    add_t(&m, b, d, c);
                }
        }
    return m;
}

static void run(const char* name, Mesh m, float sigma, float g, SSSFitConfig cfg) {
    SSSFitInput in = { m.v, m.t, m.nv, m.nt, sigma, g };
    SSSFitter* f = SSSFitCreate(&in, &cfg);
    double t0 = now();
    while (SSSFitStep(f, 0.5, now) != SSSFIT_DONE) {
        if (SSSFitPhaseNow(f) == SSSFIT_FAILED) { printf("%s: %s\n", name, SSSFitStatus(f)); SSSFitDestroy(f); free(m.v); free(m.t); return; }
    }
    const SSSFitStats* s = SSSFitGetStats(f);
    uint32_t words;
    SSSFitBlob(f, &words);
    printf("\n== %s  (%u tris, sigma %.1f, g %.2f, %.1fs, %.1f KB)\n", name, m.nt, sigma, g, now() - t0, words * 4 / 1024.0);
    printf("walks kept %u lost %u, mean steps %.1f, events/(sigma L) %.3f\n", s->walks_kept, s->walks_lost, s->mean_steps, s->events_per_length);
    printf("nll %.4f -> %.4f   R^2(log excess) %.3f\n", s->nll_initial, s->nll_final, s->r2_mean);
    printf("validation %u samples, projection failures %u\n", s->validation_samples, s->projection_failures);
    printf("exit dist q25/50/75/90 (mfp)  walk %6.2f %6.2f %6.2f %6.2f | model %6.2f %6.2f %6.2f %6.2f\n",
        s->walk_dist_q[0], s->walk_dist_q[1], s->walk_dist_q[2], s->walk_dist_q[3], s->model_dist_q[0], s->model_dist_q[1], s->model_dist_q[2], s->model_dist_q[3]);
    printf("path len  q25/50/75/90 (mfp)  walk %6.2f %6.2f %6.2f %6.2f | model %6.2f %6.2f %6.2f %6.2f\n",
        s->walk_len_q[0], s->walk_len_q[1], s->walk_len_q[2], s->walk_len_q[3], s->model_len_q[0], s->model_len_q[1], s->model_len_q[2], s->model_len_q[3]);
    printf("E[exp(-k L)] k=.01/.1/1 sigma  walk %.4f %.4f %.4f | model %.4f %.4f %.4f\n",
        s->walk_atten[0], s->walk_atten[1], s->walk_atten[2], s->model_atten[0], s->model_atten[1], s->model_atten[2]);
    printf("albedo a=.5/.9/.99: E[a^n] %.4f %.4f %.4f | model %.4f %.4f %.4f (k %.3f c %.3f)\n", s->albedo_true[0], s->albedo_true[1], s->albedo_true[2], s->albedo_model[0], s->albedo_model[1], s->albedo_model[2], s->event_k, s->event_c);
    printf("mean exit cos  walk %.3f | model %.3f\n", s->walk_cos, s->model_cos);
    SSSFitDestroy(f);
    free(m.v);
    free(m.t);
}

int main(int argc, char** argv) {
    SSSFitConfig cfg = SSSFitDefaultConfig();
    if (argc > 1) cfg.walks = atoi(argv[1]);
    if (argc > 2) cfg.lobes = atoi(argv[2]);
    if (argc > 3) cfg.surface_gaussians = atoi(argv[3]);
    if (argc > 4) { run("sphere r=1, 10 mfp radius", sphere(1.0f, 48), 10.0f, 0.0f, cfg); return 0; }
    run("sphere r=1, 10 mfp radius", sphere(1.0f, 48), 10.0f, 0.0f, cfg);
    run("sphere r=1, 2 mfp radius (optically thin)", sphere(1.0f, 48), 2.0f, 0.0f, cfg);
    run("thin slab 2x2x0.1, 10 mfp/unit", box(1.0f, 0.05f, 1.0f, 12), 10.0f, 0.0f, cfg);
    run("sphere forward scattering g=0.7", sphere(1.0f, 48), 10.0f, 0.7f, cfg);
    return 0;
}
