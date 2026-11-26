#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <easybool.h>

void RunSceneRenderExecutor(const char* scenefile, const char* outfile, int samples);

void RunTrainExecutor(const char* scenefile, const char* outply, int views, int iterations, int width, int height);

int RunCompareExecutor(const char* imagefile, const char* referencefile, const char* difffile);

#endif
