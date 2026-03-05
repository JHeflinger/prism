#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <easybool.h>

void RunGeometryExecutor(const char* scenefile, const char* outfile, int method, int arg);

void RunRenderExecutor(const char* scenefile, const char* outfile, int width, int height, int samples, BOOL direct_lighting, BOOL direct_only);

#endif
