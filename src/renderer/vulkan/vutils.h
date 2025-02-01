#ifndef VUTILS_H
#define VUTILS_H

#include "core/file.h"
#include <vulkan/vulkan.h>
#include "renderer/vulkan/vstructs.h"

void VKU_SetVulkanUtilsContext(Renderer* renderer);

VKAPI_ATTR VkBool32 VKAPI_CALL VKU_VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);

BOOL VKU_CheckValidationLayerSupport();

BOOL VKU_CheckGPUExtensionSupport(VkPhysicalDevice device);

VulkanFamilyGroup VKU_FindQueueFamilies(VkPhysicalDevice gpu);

VkShaderModule VKU_CreateShader(SimpleFile* file);

void VKU_CopyHostToBuffer(void* hostdata, size_t size, VkDeviceSize buffersize, VkBuffer buffer);

void VKU_GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

VkVertexInputBindingDescription VKU_VertexBindingDescription();

QUAD_VkVertexInputAttributeDescription VKU_VertexAttributeDescriptions();

Schrodingnum VKU_FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

VkCommandBuffer VKU_BeginSingleTimeCommands();

BOOL VKU_HasStencilComponent(VkFormat format);

VkFormat VKU_FindSupportedFormat(
    VkFormat* candidates,
    size_t num_candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features);

VkFormat VKU_FindDepthFormat();

void VKU_EndSingleTimeCommands(VkCommandBuffer commandBuffer);

void VKU_CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

void VKU_CopyBufferToImage(
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height);

void VKU_TransitionImageLayout(
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels);

void VKU_CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VulkanDataBuffer* buffer);

void VKU_DestroyBuffer(VulkanDataBuffer buffer);

void VKU_CreateImage(
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    VkSampleCountFlagBits numSamples,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkImageAspectFlags aspectFlags,
    VulkanImage* image);

VkSampleCountFlagBits VKU_GetMaximumSampleCount();

#endif