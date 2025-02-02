#ifndef VUPDATE_H
#define VUPDATE_H

#include "renderer/vulkan/vstructs.h"

void VUPDT_Vertices(VulkanDataBuffer* vertices);

void VUPDT_Indices(VulkanDataBuffer* indices);

void VUPDT_SetVulkanUpdateContext(Renderer* renderer);

#endif
