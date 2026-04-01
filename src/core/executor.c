#include "executor.h"
#include "renderer/processor.h"
#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "core/file.h"
#include <easylogger.h>
#include <raylib.h>
#include <easyobjects.h>
#include <time.h>

void InitializeExecutor(int w, int h) {
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

void CleanExecutor() {
    DestroyRenderer();
}

void ImportExecuteScene(const char* scenefile) {
    FileType ft = GetFileType(scenefile);
    switch (ft) {
        case DOTOBJ:
            LoadOBJ(scenefile);
            break;
        default:
            EZ_ERROR("Unsupported scene file type detected - unable to render");
            CleanExecutor();
            exit(1);
    }
}

void RunGeometryExecutor(const char* scenefile, const char* outfile, int method, int arg) {
    size_t memcheck = EZ_ALLOCATED();
    EZ_INFO("Initializing prism execution suite (geometry processing version)...");
    InitializeExecutor(1, 1);
    ImportExecuteScene(scenefile);
    EZ_INFO("Executing execution suite...");
    Geometry* geometry = RendererGeometry();
    geometry->manifold = GenerateManifoldMesh(geometry->vertices, geometry->normals, geometry->triangles);
    switch (method) {
        case 0:
            for (int i = 0; i < arg; i++) SerialSubdivide(&(geometry->manifold));
            break;
        case 1:
            size_t iterations = ceil(((float)arg)/2.0f);
            SerialSimplify(&(geometry->manifold), iterations);
            break;
        case 2:
            float filter_param;
            memcpy(&filter_param, &arg, sizeof(float));
            SerialFilter(&(geometry->manifold), filter_param);
            break;
        case 3:
            float remesh_param;
            memcpy(&remesh_param, &arg, sizeof(float));
            SerialRemesh(&(geometry->manifold), remesh_param);
            break;
        default:
            EZ_ERROR("Unknown geometry processing method detected");
            exit(1);
    }
    EZ_INFO("Saving results...");
    SaveManifoldOBJ(outfile, &(geometry->manifold));
    EZ_INFO("Cleaning up and exiting prism execution suite...");
    CleanExecutor();
    EZ_ASSERT(memcheck == EZ_ALLOCATED(), "Memory cleanup revealed a leak of %d bytes", (int)(EZ_ALLOCATED() - memcheck));
}

void RunRenderExecutor(const char* scenefile, const char* outfile, int width, int height, int samples, BOOL direct_lighting, BOOL direct_only) {
    size_t memcheck = EZ_ALLOCATED();
    EZ_INFO("Initialzing prism execution suite...");
    InitializeExecutor(width, height);
    RenderConfig()->direct = direct_lighting;
    RenderConfig()->directonly = direct_only;
    EZ_INFO("Importing scene...");
    ImportExecuteScene(scenefile);
    EZ_INFO("Running render...\n");
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
    time = GetTime() - time;
    EZ_INFO("Successfully rendered image in %.3f seconds", time);
    EZ_INFO("Saving render...");
    SaveRender(outfile);
    EZ_INFO("Cleaning up and exiting prism execution suite...");
    CleanExecutor();	
    EZ_ASSERT(memcheck == EZ_ALLOCATED(), "Memory cleanup revealed a leak of %d bytes", (int)(EZ_ALLOCATED() - memcheck));
}
