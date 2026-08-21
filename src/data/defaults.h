#ifndef DEFAULTS_H
#define DEFAULTS_H

#ifdef __WIN32
#define OPSYS "WINDOWS"
#else
#define OPSYS "LINUX"
#endif

#ifndef BOOL
#define BOOL int
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define EDITOR_DEFAULT_WIDTH 1600
#define EDITOR_DEFAULT_HEIGHT 900

#define MAX_SOURCE_NAME_SIZE 256
#define MAX_FORCE_NAME_SIZE 256
#define MAX_MATERIAL_NAME_SIZE 256
#define MAX_LIGHT_NAME_SIZE 256
#define MAX_MESH_NAME_SIZE 256

void DefaultPreload();

void DefaultPostload();

void DefaultPreupdate();

void DefaultCleanup();

#endif
