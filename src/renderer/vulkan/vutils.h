#ifndef VUTILS_H
#define VUTILS_H

#include "core/file.h"
#include <vulkan/vulkan.h>
#include "renderer/vulkan/vstructs.h"

void VUTIL_SetVulkanUtilsContext(Renderer* renderer);

VKAPI_ATTR VkBool32 VKAPI_CALL VUTIL_VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);

BOOL VUTIL_CheckValidationLayerSupport();

BOOL VUTIL_CheckGPUExtensionSupport(VkPhysicalDevice device);

VulkanFamilyGroup VUTIL_FindQueueFamilies(VkPhysicalDevice gpu);

VkShaderModule VUTIL_CreateShader(SimpleFile* file);

void VUTIL_CopyHostToBuffer(void* hostdata, size_t size, VkDeviceSize buffersize, VkBuffer buffer);

void VUTIL_GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

VkVertexInputBindingDescription VUTIL_VertexBindingDescription();

QUAD_VkVertexInputAttributeDescription VUTIL_VertexAttributeDescriptions();

Schrodingnum VUTIL_FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

VkCommandBuffer VUTIL_BeginSingleTimeCommands();

BOOL VUTIL_HasStencilComponent(VkFormat format);

VkFormat VUTIL_FindSupportedFormat(
    VkFormat* candidates,
    size_t num_candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features);

VkFormat VUTIL_FindDepthFormat();

void VUTIL_EndSingleTimeCommands(VkCommandBuffer commandBuffer);

void VUTIL_CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

void VUTIL_CopyBufferToImage(
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height);

void VUTIL_TransitionImageLayout(
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels);

void VUTIL_CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VulkanDataBuffer* buffer);

void VUTIL_DestroyBuffer(VulkanDataBuffer buffer);

void VUTIL_CreateImage(
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

VkSampleCountFlagBits VUTIL_GetMaximumSampleCount();

#endif