#ifndef OVERRIDE_PRISM

#include "core/editor.h"
#include "core/executor.h"
#include "renderer/renderer.h"
#include <easylogger.h>

int main(int argc, char** argv) {
    if (argc >= 4 && argc <= 8 && strcmp(argv[1], "train") == 0) {
        int views = argc > 4 ? atoi(argv[4]) : 0;
        int iterations = argc > 5 ? atoi(argv[5]) : 0;
        int width = argc > 6 ? atoi(argv[6]) : 800;
        int height = argc > 7 ? atoi(argv[7]) : 600;
        RunTrainExecutor(argv[2], argv[3], views, iterations, width, height);
    } else if (argc == 5 && strcmp(argv[1], "render") == 0) {
        int samples = atoi(argv[4]);
        if (samples <= 0) { printf("Sample count must be a positive integer - got \"%s\"\n", argv[4]); return 1; }
        RunSceneRenderExecutor(argv[2], argv[3], samples);
    } else if ((argc == 4 || argc == 5) && strcmp(argv[1], "compare") == 0) {
        return RunCompareExecutor(argv[2], argv[3], argc == 5 ? argv[4] : NULL);
    } else if (argc == 1) {
        RunEditor();
    } else {
        printf("Incorrect program usage detected - please refer to the following ways to use prism:\n");
        printf("  ./<executable>                                                                             // Runs editor\n");
        printf("  ./<executable> render <scene> <output> <samples>                                          // Renders a single image with the scene's own settings\n");
        printf("  ./<executable> train <scene> <output.ply> [views] [iterations] [width] [height]           // Trains gaussian splats of a scene\n");
        printf("  ./<executable> compare <image> <reference> [diff.png]                                     // Compares an image against a reference\n");
        return 1;
    }
    EZ_INFO("See you, Space Cowboy");
    return 0;
}

#endif
