#ifndef VINIT_H
#define VINIT_H

#include "data/config.h"
#include "renderer/vulkan/vstructs.h"

BOOL VINIT_Queue(VkQueue* queue);

BOOL VINIT_Commands(VulkanCommands* commands);

BOOL VINIT_Syncro(VulkanSyncro* syncro);

BOOL VINIT_UniformBuffers(UBOArray* ubos);

BOOL VINIT_Descriptors(VulkanDescriptors* descriptors);

BOOL VINIT_Attachments(VulkanAttachments* attachments);

BOOL VINIT_Framebuffers(VkFramebuffer* framebuffer);

BOOL VINIT_RenderData(VulkanRenderData* renderdata);

BOOL VINIT_RenderPass(VkRenderPass* renderpass);

BOOL VINIT_Pipeline(VulkanPipeline* pipeline);

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

BOOL VINIT_RendererReference(Renderer* renderer);

#endif
