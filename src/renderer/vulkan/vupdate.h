#ifndef VUPDATE_H
#define VUPDATE_H

#include "renderer/vulkan/vstructs.h"

void VUPDT_Simulation(VulkanFluidSimulation* vfs);

void VUPDT_Transforms(VulkanDataBuffer* transforms);

void VUPDT_Skins(VulkanDataBuffer* skins);

void VUPDT_Poses(VulkanDataBuffer* poses);

void VUPDT_Lights(VulkanDataBuffer* lights);

void VUPDT_Normals(VulkanDataBuffer* normals);

void VUPDT_Vertices(VulkanVertices* vertices);

void VUPDT_Triangles(VulkanDataBuffer* triangles);

void VUPDT_Emissives(VulkanDataBuffer* emissives);

void VUPDT_Materials(VulkanDataBuffer* materials);

void VUPDT_RecordCommand(VkCommandBuffer command);

void VUPDT_DescriptorSets(VulkanDescriptors* descriptors);

void VUPDT_DescriptorSetsAll(VulkanDescriptors* descriptors);

void VUPDT_UniformBuffers(UBOArray* ubos);

void VUPDT_SetVulkanUpdateContext(Renderer* renderer);

#endif
