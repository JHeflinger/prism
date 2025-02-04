#include "vutils.h"
#include "core/log.h"

Renderer* g_vutil_renderer_ref = NULL;

void VUTIL_SetVulkanUtilsContext(Renderer* renderer) {
    g_vutil_renderer_ref = renderer;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VUTIL_VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    LOG_ASSERT(pUserData == NULL, "User data has not been set up to be handled");
    if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_WARN("%s[VULKAN] [%s]%s %s",
            LOG_YELLOW,
            (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT ? "GENERAL" :
                (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ? "VALIDATION" : "PERFORMANCE")),
            LOG_RESET,
            pCallbackData->pMessage);
    } else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOG_TRACE("%s[FATAL] [VULKAN] [%s]%s %s",
            LOG_RED,
            (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT ? "GENERAL" :
                (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ? "VALIDATION" : "PERFORMANCE")),
            LOG_RESET,
            pCallbackData->pMessage);
		exit(1);
    }

    return VK_FALSE;
}

BOOL VUTIL_CheckValidationLayerSupport() {
    // grab all available layers
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    VkLayerProperties* availableLayers = EZALLOC(layerCount, sizeof(VkLayerProperties));
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

    // check if layers in validation layers exist in the available layers
    for (size_t i = 0; i < g_vutil_renderer_ref->vulkan.metadata.validation.size; i++) {
        BOOL layerFound = FALSE;
        for (size_t j = 0; j < layerCount; j++) {
            if (strcmp(ARRLIST_StaticString_get(&(g_vutil_renderer_ref->vulkan.metadata.validation), i), availableLayers[j].layerName) == 0) {
                layerFound = TRUE;
                break;
            }
        }
        if (!layerFound) {
            EZFREE(availableLayers);
            return FALSE;
        }
    }
    EZFREE(availableLayers);
    return TRUE;
}

BOOL VUTIL_CheckGPUExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);
    VkExtensionProperties* availableExtensions = EZALLOC(extensionCount, sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, availableExtensions);
    for (size_t i = 0; i < g_vutil_renderer_ref->vulkan.metadata.extensions.device.size; i++) {
        BOOL extensionFound = FALSE;
        for (size_t j = 0; j < extensionCount; j++) {
            if (strcmp(ARRLIST_StaticString_get(&(g_vutil_renderer_ref->vulkan.metadata.extensions.device), i), availableExtensions[j].extensionName) == 0) {
                extensionFound = TRUE;
                break;
            }
        }
        if (!extensionFound) {
            EZFREE(availableExtensions);
            return FALSE;
        }
    }
    EZFREE(availableExtensions);
    return TRUE;
}

VulkanFamilyGroup VUTIL_FindQueueFamilies(VkPhysicalDevice gpu) {
    VulkanFamilyGroup group = { 0 };
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = EZALLOC(queueFamilyCount, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, queueFamilies);
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            group.graphics = (Schrodingnum){ i, TRUE };
            break;
        }
    }
    EZFREE(queueFamilies);
    return group;
}

VkShaderModule VUTIL_CreateShader(SimpleFile* file) {
	VkShaderModuleCreateInfo createInfo = { 0 };
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = file->size;
	createInfo.pCode = (const uint32_t*)(file->data);
	VkShaderModule shader;
	VkResult result = vkCreateShaderModule(g_vutil_renderer_ref->vulkan.core.general.interface, &createInfo, NULL, &shader);
	LOG_ASSERT(result == VK_SUCCESS, "Failed to create shader module");
	return shader;
}

void VUTIL_CopyHostToBuffer(void* hostdata, size_t size, VkDeviceSize buffersize, VkBuffer buffer) {    
    VulkanDataBuffer stagingBuffer;
    VUTIL_CreateBuffer(
        buffersize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer);
    void* data;
    vkMapMemory(g_vutil_renderer_ref->vulkan.core.general.interface, stagingBuffer.memory, 0, buffersize, 0, &data);
    memcpy(data, hostdata, size);
    vkUnmapMemory(g_vutil_renderer_ref->vulkan.core.general.interface, stagingBuffer.memory);
    VUTIL_CopyBuffer(stagingBuffer.buffer, buffer, buffersize);
    VUTIL_DestroyBuffer(stagingBuffer);
}

Schrodingnum VUTIL_FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    Schrodingnum result = { 0 };
    VkPhysicalDeviceMemoryProperties memProperties = { 0 };
    vkGetPhysicalDeviceMemoryProperties(g_vutil_renderer_ref->vulkan.core.general.gpu, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            result.value = i;
            result.exists = TRUE;
            break;
        }
    }
    return result;
}

VkCommandBuffer VUTIL_BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = g_vutil_renderer_ref->vulkan.core.scheduler.commands.pool;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(g_vutil_renderer_ref->vulkan.core.general.interface, &allocInfo, &commandBuffer);
    VkCommandBufferBeginInfo beginInfo = { 0 };
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VUTIL_EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo = { 0 };
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(g_vutil_renderer_ref->vulkan.core.scheduler.queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_vutil_renderer_ref->vulkan.core.scheduler.queue);
    vkFreeCommandBuffers(g_vutil_renderer_ref->vulkan.core.general.interface, g_vutil_renderer_ref->vulkan.core.scheduler.commands.pool, 1, &commandBuffer);
}

void VUTIL_CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = VUTIL_BeginSingleTimeCommands();
    VkBufferCopy copyRegion = { 0 };
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    VUTIL_EndSingleTimeCommands(commandBuffer);
}

void VUTIL_TransitionImageLayout(
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels) {
    VkCommandBuffer commandBuffer = VUTIL_BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier = { 0 };
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else {
        LOG_ASSERT(FALSE, "Unsupported layout transition!");
    }
    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, NULL,
        0, NULL,
        1, &barrier);
    VUTIL_EndSingleTimeCommands(commandBuffer);
}

void VUTIL_CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VulkanDataBuffer* buffer) {
    VkBufferCreateInfo bufferInfo = { 0 };
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateBuffer(g_vutil_renderer_ref->vulkan.core.general.interface, &bufferInfo, NULL, &(buffer->buffer));
    LOG_ASSERT(result == VK_SUCCESS, "Unable to create buffer");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_vutil_renderer_ref->vulkan.core.general.interface, buffer->buffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    Schrodingnum memoryType = VUTIL_FindMemoryType(memRequirements.memoryTypeBits, properties);
    LOG_ASSERT(memoryType.exists, "Unable to find memory for vertex buffer");
    allocInfo.memoryTypeIndex = memoryType.value;
    result = vkAllocateMemory(g_vutil_renderer_ref->vulkan.core.general.interface, &allocInfo, NULL, &(buffer->memory));
    LOG_ASSERT(result == VK_SUCCESS, "Unable to allocate memory for buffer");

    vkBindBufferMemory(g_vutil_renderer_ref->vulkan.core.general.interface, buffer->buffer, buffer->memory, 0);
}

void VUTIL_DestroyBuffer(VulkanDataBuffer buffer) {
    vkDestroyBuffer(g_vutil_renderer_ref->vulkan.core.general.interface, buffer.buffer, NULL);
    vkFreeMemory(g_vutil_renderer_ref->vulkan.core.general.interface, buffer.memory, NULL);
}

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
    VulkanImage* image) {
    VkImageCreateInfo imageInfo = { 0 };
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = numSamples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateImage(g_vutil_renderer_ref->vulkan.core.general.interface, &imageInfo, NULL, &(image->image));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create image!");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(g_vutil_renderer_ref->vulkan.core.general.interface, image->image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    Schrodingnum memoryType = VUTIL_FindMemoryType(memRequirements.memoryTypeBits, properties);
    LOG_ASSERT(memoryType.exists, "Unable to find valid memory type");
    allocInfo.memoryTypeIndex = memoryType.value;
    result = vkAllocateMemory(g_vutil_renderer_ref->vulkan.core.general.interface, &allocInfo, NULL, &(image->memory));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to allocate image memory!");

    vkBindImageMemory(g_vutil_renderer_ref->vulkan.core.general.interface, image->image, image->memory, 0);
    
    VkImageViewCreateInfo viewInfo = { 0 };
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(g_vutil_renderer_ref->vulkan.core.general.interface, &viewInfo, NULL, &(image->view));
    LOG_ASSERT(result == VK_SUCCESS, "failed to create texture image view!");
}
