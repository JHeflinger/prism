#ifndef LOADER_H
#define LOADER_H

#include <easybool.h>

const char* GetOpenFile(const char* type);

const char* GetSaveFile(const char* type);

BOOL LoadOBJ(const char* filepath);

BOOL LoadFBX(const char* filepath);

BOOL LoadPLY(const char* filepath);

#endif
