#include "core/editor.h"
#include "core/executor.h"
#include "renderer/renderer.h"
#include <easylogger.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_STR 512

typedef struct {
    char infile[MAX_STR];
    char outfile[MAX_STR];
    int method;
    int args1;
} Config;

static void trim(char *str) {
    while (isspace((unsigned char)*str)) memmove(str, str + 1, strlen(str));
    int len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

int load_config(const char *filepath, Config *cfg) {
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '\0')
            continue;
        if (line[0] == ';')
            continue;
        if (line[0] == '[')
            continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char key[256];
        char value[512];
        strncpy(key, line, sizeof(key));
        strncpy(value, eq + 1, sizeof(value));
        trim(key);
        trim(value);
        if (strcmp(key, "infile") == 0) {
            strncpy(cfg->infile, value, MAX_STR);
        }
        else if (strcmp(key, "outfile") == 0) {
            strncpy(cfg->outfile, value, MAX_STR);
        }
        else if (strcmp(key, "method") == 0) {
            if (strcmp(value, "subdivide") == 0) cfg->method = 0;
            else if (strcmp(value, "simplify") == 0) cfg->method = 1;
            else if (strcmp(value, "filter") == 0) cfg->method = 2;
        }
        else if (strcmp(key, "args1") == 0) {
            cfg->args1 = atoi(value);
        }
    }
    fclose(f);
    return 1;
}

int main(int argc, char** argv) {
    if (argc == 1) {
        RunEditor();
    } else if (argc == 2) {
        Config cfg = { 0 };
        load_config(argv[1], &cfg);
        EZ_INFO("%s %s %d %d", cfg.infile, cfg.outfile, cfg.method, cfg.args1);
        RunGeometryExecutor(cfg.infile, cfg.outfile, cfg.method, cfg.args1);
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
        else { printf("Unknown geometry processing method detected - \"%s\"\n", argv[3]); return 1; }
        RunGeometryExecutor(argv[1], argv[2], method, argc == 4 ? 0 : atoi(argv[4]));
	} else if (argc == 8) {
        RunRenderExecutor(argv[1], argv[2], atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), strcmp(argv[6], "true") == 0 ? TRUE : FALSE, strcmp(argv[7], "true") == 0 ? TRUE : FALSE);
    } else {
        printf("Incorrect program usage detected - please refer to the following ways to use prism:\n");
        printf("  ./<executable>                                                                             // Runs editor\n");
        printf("  ./<executable> <width> <height>                                                            // Runs editor with custom viewport resolution\n");
        printf("  ./<executable> <scene> <output> <method> <arg?>                                            // Processes object geometry\n");
        printf("  ./<executable> <scene> <output> <width> <height> <samples> <direct_lighting> <direct_only> // Renders a single image\n");
        return 1;
    }
    EZ_INFO("See you, Space Cowboy");
    return 0;
}
