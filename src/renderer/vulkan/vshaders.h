#ifndef VSHADERS_H
#define VSHADERS_H

#include "renderer/vulkan/vstructs.h"

void GenerateDefaultShaders(ARRLIST_VulkanShaderPtr* list, Renderer* renderer);

VulkanShader* GenerateShader(Renderer* context, const char* readfile, const char* sourcefile);

#endif
