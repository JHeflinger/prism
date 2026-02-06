#include "executor.h"
#include "renderer/renderer.h"
#include "renderer/loader.h"
#include "core/file.h"
#include <easylogger.h>
#include <raylib.h>
#include <easyobjects.h>
#include <time.h>

#ifdef __WIN32
#define OPSYS "WINDOWS"
#else
#define OPSYS "LINUX"
#endif

void InitializeExecutor(int w, int h) {
	OverrideResolution(w, h);
	SetViewportSlice(w, h);
	SetTraceLogLevel(LOG_NONE);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
	InitWindow(1, 1, "Prism Headless Executor");
	InitializeRenderer();
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
        case DOTXML:
            LoadXML(scenefile);
            break;
        default:
            EZ_ERROR("Unsupported scene file type detected - unable to render");
            CleanExecutor();
            exit(1);
    }
}

void RunExecutor(const char* scenefile, const char* outfile, int width, int height, int samples) {
    size_t memcheck = EZ_ALLOCATED();
    EZ_INFO("Initialzing prism execution suite...");
    InitializeExecutor(width, height);
    EZ_INFO("Importing scene...");
    ImportExecuteScene(scenefile);
    EZ_INFO("Running render...");
    float time = GetTime();
    for (int i = 0; i < samples; i++) {
        Render();
    }
    time = GetTime() - time;
    EZ_INFO("Successfully rendered image in %.3f seconds", time);
    EZ_INFO("Saving render...");
    SaveRender(outfile);
    EZ_INFO("Cleaning up and exiting prism execution suite...");
    CleanExecutor();	
    EZ_ASSERT(memcheck == EZ_ALLOCATED(), "Memory cleanup revealed a leak of %d bytes", (int)(EZ_ALLOCATED() - memcheck));
}
