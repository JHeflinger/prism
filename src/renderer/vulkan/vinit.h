#ifndef VINIT_H
#define VINIT_H

#include "data/config.h"
#include "renderer/vulkan/vstructs.h"

BOOL VINIT_Scheduler(VulkanScheduler* scheduler);

BOOL VINIT_Bridge(VulkanDataBuffer* bridge);

BOOL VINIT_RenderContext(VulkanRenderContext* context);

BOOL VINIT_General(VulkanGeneral* general);

BOOL VINIT_Vertices(VulkanDataBuffer* vertices);

BOOL VINIT_Indices(VulkanDataBuffer* indices);

BOOL VINIT_Geometry(VulkanGeometry* geometry);

BOOL VINIT_Metadata(VulkanMetadata* metadata);

BOOL VINIT_Core(VulkanCore* core);

BOOL VINIT_Vulkan(VulkanObject* vulkan);

#endif