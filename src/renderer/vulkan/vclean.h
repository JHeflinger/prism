#ifndef VCLEAN_H
#define VCLEAN_H

#include "renderer/vulkan/vstructs.h"

void VCLEAN_BoundingVolumeHierarchy(VulkanDataBuffer* bvh);

void VCLEAN_Triangles(VulkanDataBuffer* triangles);

void VCLEAN_Materials(VulkanDataBuffer* materials);

void VCLEAN_Geometry(VulkanGeometry* geometry);

void VCLEAN_Metadata(VulkanMetadata* metadata);

void VCLEAN_General(VulkanGeneral* general);

void VCLEAN_RenderData(VulkanRenderData* renderdata);

void VCLEAN_RenderContext(VulkanRenderContext* context);

void VCLEAN_Bridge(VulkanDataBuffer* bridge);

void VCLEAN_Scheduler(VulkanScheduler* scheduler);

void VCLEAN_Core(VulkanCore* core);

void VCLEAN_Vulkan(VulkanObject* vulkan);

void VCLEAN_SetVulkanCleanContext(Renderer* renderer);

#endif