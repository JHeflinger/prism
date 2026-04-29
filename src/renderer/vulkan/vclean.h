#ifndef VCLEAN_H
#define VCLEAN_H

#include "renderer/vulkan/vstructs.h"

void VCLEAN_Shaders(ARRLIST_VulkanShaderPtr* shaders);

void VCLEAN_ExtendedBuffers(ARRLIST_ShaderBuffer* ebuffers);

void VCLEAN_Transforms(VulkanDataBuffer* transforms);

void VCLEAN_Skins(VulkanDataBuffer* skins);

void VCLEAN_Poses(VulkanDataBuffer* poses);

void VCLEAN_Lights(VulkanDataBuffer* lights);

void VCLEAN_BVH(VulkanBVH* bvh);

void VCLEAN_Simulation(VulkanFluidSimulation* vfs);

void VCLEAN_Normals(VulkanDataBuffer* normals);

void VCLEAN_Vertices(VulkanVertices* vertices);

void VCLEAN_Triangles(VulkanDataBuffer* triangles);

void VCLEAN_Emissives(VulkanDataBuffer* emissives);

void VCLEAN_Materials(VulkanDataBuffer* materials);

void VCLEAN_Geometry(VulkanGeometry* geometry);

void VCLEAN_Metadata(VulkanMetadata* metadata);

void VCLEAN_General(VulkanGeneral* general);

void VCLEAN_RenderData(VulkanRenderData* renderdata);

void VCLEAN_RenderContext(VulkanRenderContext* context);

void VCLEAN_Bridge(VulkanDataBuffer* bridge);

void VCLEAN_OverlayBridge(VulkanDataBuffer* bridge);

void VCLEAN_Scheduler(VulkanScheduler* scheduler);

void VCLEAN_Transfer(VulkanTransfer* transfer);

void VCLEAN_Core(VulkanCore* core);

void VCLEAN_Vulkan(VulkanObject* vulkan);

void VCLEAN_SetVulkanCleanContext(Renderer* renderer);

#endif
