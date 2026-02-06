#include "core/editor.h"
#include "core/executor.h"
#include "renderer/renderer.h"
#include <easylogger.h>
// input path output path samples resolution 
int main(int argc, char** argv) {
    if (argc == 1) {
        RunEditor();
    } else if (argc == 3) {
		int rx = atoi(argv[1]);
		int ry = atoi(argv[2]);
		EZ_INFO("Setting resolution to %dx%d", rx, ry);
		OverrideResolution(rx, ry);
        RunEditor();
	} else if (argc == 6) {
        RunExecutor(argv[1], argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
    } else {
        printf("Incorrect program usage detected - please refer to the following ways to use prism:\n");
        printf("  ./<executable>                                             // Runs editor\n");
        printf("  ./<executable> <width> <height>                            // Runs editor with custom viewport resolution\n");
        printf("  ./<executable> <scene> <output> <width> <height> <samples> // Renders a single image\n");
        return 1;
    }
    EZ_INFO("See you, Space Cowboy");
    return 0;
}
