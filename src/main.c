#include "core/editor.h"
#include <easylogger.h>
#include "renderer/renderer.h"
#include "test/test.h"

int main(int argc, char** argv) {
	#ifndef TEST_SUITE
	if (argc == 3) {
		int rx = atoi(argv[1]);
		int ry = atoi(argv[2]);
		EZ_INFO("Setting resolution to %dx%d", rx, ry);
		OverrideResolution(rx, ry);
	}
    RunEditor();
    EZ_INFO("See you, Space Cowboy");
	#else
	Test();
	#endif
    return 0;
}
