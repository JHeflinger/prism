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
    } else if (argc == 1) {
        RunEditor();
    } else if (argc == 3) {
		int rx = atoi(argv[1]);
		int ry = atoi(argv[2]);
		EZ_INFO("Setting resolution to %dx%d", rx, ry);
		OverrideResolution(rx, ry);
        RunEditor();
    } else if (argc == 4 || argc == 5) {
        int method = -1;
        if (strcmp(argv[3], "subdivide") == 0) method = 0;
        else if (strcmp(argv[3], "simplify") == 0) method = 1;
        else if (strcmp(argv[3], "filter") == 0) method = 2;
        else if (strcmp(argv[3], "remesh") == 0) method = 3;
        else { printf("Unknown geometry processing method detected - \"%s\"\n", argv[3]); return 1; }
        int args1;
        if (method > 1) {
            float argf = atof(argv[4]);
            memcpy(&args1, &argf, sizeof(int));
        } else {
            args1 = atoi(argv[4]);
        }
        RunGeometryExecutor(argv[1], argv[2], method, args1);
	} else if (argc == 8) {
        RunRenderExecutor(argv[1], argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), strcmp(argv[6], "true") == 0 ? TRUE : FALSE, strcmp(argv[7], "true") == 0 ? TRUE : FALSE);
    } else {
        printf("Incorrect program usage detected - please refer to the following ways to use prism:\n");
        printf("  ./<executable>                                                                             // Runs editor\n");
        printf("  ./<executable> <width> <height>                                                            // Runs editor with custom viewport resolution\n");
        printf("  ./<executable> <scene> <output> <method> <arg?>                                            // Processes object geometry\n");
        printf("  ./<executable> <scene> <output> <width> <height> <samples> <direct_lighting> <direct_only> // Renders a single image\n");
        printf("  ./<executable> train <scene> <output.ply> [views] [iterations] [width] [height]           // Trains gaussian splats of a scene\n");
        return 1;
    }
    EZ_INFO("See you, Space Cowboy");
    return 0;
}

#endif
