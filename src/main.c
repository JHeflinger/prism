#include "core/editor.h"
#include "core/log.h"
#include "renderer/renderer.h"

int main(int argc, char** argv) {
	if (argc == 3) {
		int rx = atoi(argv[1]);
		int ry = atoi(argv[2]);
		LOG_INFO("Setting resolution to %dx%d", rx, ry);
		OverrideResolution(rx, ry);
	}
    RunEditor();
    LOG_INFO("See you, Space Cowboy");
    return 0;
}
