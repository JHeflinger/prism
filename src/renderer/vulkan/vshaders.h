#ifndef VSHADERS_H
#define VSHADERS_H

#include "renderer/vulkan/vstructs.h"

VulkanShader* GenerateShader(BOOL enabled, Renderer* context, const char* readfile, const char* sourcefile);

#endif
