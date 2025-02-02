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

void VUTIL_GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) {
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(g_vutil_renderer_ref->vulkan.core.general.gpu, imageFormat, &formatProperties);
    LOG_ASSERT(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "Texture format does not support linear filtering!");

    VkCommandBuffer commandBuffer = VUTIL_BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier = { 0 };
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, NULL,
            0, NULL,
            1, &barrier);

        VkImageBlit blit = { 0 };
        blit.srcOffsets[0] = (VkOffset3D){ 0, 0, 0 };
        blit.srcOffsets[1] = (VkOffset3D){ mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = (VkOffset3D){ 0, 0, 0 };
        blit.dstOffsets[1] = (VkOffset3D){ mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(
            commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, NULL,
            0, NULL,
            1, &barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, NULL,
        0, NULL,
        1, &barrier);

    VUTIL_EndSingleTimeCommands(commandBuffer);
}

VkVertexInputBindingDescription VUTIL_VertexBindingDescription() {
    VkVertexInputBindingDescription bindingDescription = { 0 };
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

QUAD_VkVertexInputAttributeDescription VUTIL_VertexAttributeDescriptions() {
    QUAD_VkVertexInputAttributeDescription attributeDescriptions = { 0 };
    attributeDescriptions.value[0].binding = 0;
    attributeDescriptions.value[0].location = 0;
    attributeDescriptions.value[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions.value[0].offset = offsetof(Vertex, position);
    attributeDescriptions.value[1].binding = 0;
    attributeDescriptions.value[1].location = 1;
    attributeDescriptions.value[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions.value[1].offset = offsetof(Vertex, color);
    attributeDescriptions.value[2].binding = 0;
    attributeDescriptions.value[2].location = 2;
    attributeDescriptions.value[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions.value[2].offset = offsetof(Vertex, texcoord);
    attributeDescriptions.value[3].binding = 0;
    attributeDescriptions.value[3].location = 3;
    attributeDescriptions.value[3].format = VK_FORMAT_R32_UINT;
    attributeDescriptions.value[3].offset = offsetof(Vertex, texid);
    return attributeDescriptions;
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

BOOL VUTIL_HasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

VkFormat VUTIL_FindSupportedFormat(
    VkFormat* candidates,
    size_t num_candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features) {
    for (size_t i = 0; i < num_candidates; i++) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(g_vutil_renderer_ref->vulkan.core.general.gpu, candidates[i], &props);
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return candidates[i];
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return candidates[i];
        }
    }
    LOG_FATAL("Failed to find supported format!");
}

VkFormat VUTIL_FindDepthFormat() {
    VkFormat formats[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    return VUTIL_FindSupportedFormat(
        formats,
        3,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
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

void VUTIL_CopyBufferToImage(
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height) {
    VkCommandBuffer commandBuffer = VUTIL_BeginSingleTimeCommands();
    VkBufferImageCopy region = { 0 };
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = (VkOffset3D){ 0, 0, 0 };
    region.imageExtent = (VkExtent3D){ width, height, 1 };
    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
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

VkSampleCountFlagBits VUTIL_GetMaximumSampleCount() {
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(g_vutil_renderer_ref->vulkan.core.general.gpu, &physicalDeviceProperties);

    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
    if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
    if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
    if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
    if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
    if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

    return VK_SAMPLE_COUNT_1_BIT;
}
