#ifndef VUPDATE_H
#define VUPDATE_H

#include "renderer/vulkan/vstructs.h"

void VUPDT_CommandBuffer(VkCommandBuffer command);

void VUPDT_DescriptorSets(VulkanDescriptors* descriptors);

void VUPDT_ComputeDescriptorSets(VulkanDescriptors* descriptors);

void VUPDT_UniformBuffers(UBOArray* ubos);

void VUPDT_RenderSize(size_t width, size_t height);

void VUPDT_Vertices(VulkanDataBuffer* vertices);

void VUPDT_Indices(VulkanDataBuffer* indices);

void VUPDT_SetVulkanUpdateContext(Renderer* renderer);

#endif
