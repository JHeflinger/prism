#include "vutils.h"
#include <easylogger.h>

Renderer* g_vutil_renderer_ref = NULL;

void VUTIL_SetVulkanUtilsContext(Renderer* renderer) {
    g_vutil_renderer_ref = renderer;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VUTIL_VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    EZ_ASSERT(pUserData == NULL, "User data has not been set up to be handled");
    if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        EZ_WARN("%s[VULKAN] [%s]%s %s",
            EZ_YELLOW,
            (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT ? "GENERAL" :
                (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ? "VALIDATION" : "PERFORMANCE")),
            EZ_RESET,
            pCallbackData->pMessage);
    } else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        printf("%s[FATAL] [VULKAN] [%s]%s %s",
            EZ_RED,
            (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT ? "GENERAL" :
                (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ? "VALIDATION" : "PERFORMANCE")),
            EZ_RESET,
            pCallbackData->pMessage);
		exit(1);
    }
    return VK_FALSE;
}

BOOL VUTIL_CheckValidationLayerSupport() {
    // grab all available layers
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    VkLayerProperties* availableLayers = EZ_ALLOC(layerCount, sizeof(VkLayerProperties));
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
            EZ_FREE(availableLayers);
            return FALSE;
        }
    }
    EZ_FREE(availableLayers);
    return TRUE;
}

BOOL VUTIL_CheckGPUExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);
    VkExtensionProperties* availableExtensions = EZ_ALLOC(extensionCount, sizeof(VkExtensionProperties));
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
            EZ_FREE(availableExtensions);
            return FALSE;
        }
    }
    EZ_FREE(availableExtensions);
    return TRUE;
}

VulkanFamilyGroup VUTIL_FindQueueFamilies(VkPhysicalDevice gpu) {
    VulkanFamilyGroup group = { 0 };
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, NULL);
    VkQueueFamilyProperties* families = EZ_ALLOC(count, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families);
    for (uint32_t i = 0; i < count; i++) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            group.graphics = (Schrodingnum){ i, TRUE };
        }
        if ((families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
           !(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
           !(families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            group.transfer = (Schrodingnum){ i, TRUE };
        }
    }
    if (!group.transfer.exists)
        group.transfer = group.graphics;
    EZ_FREE(families);
    return group;
}

VkShaderModule VUTIL_CreateShader(SimpleFile* file) {
	VkShaderModuleCreateInfo createInfo = { 0 };
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = file->size;
	createInfo.pCode = (const uint32_t*)(file->data);
	VkShaderModule shader;
	VkResult result = vkCreateShaderModule(g_vutil_renderer_ref->vulkan.core.general.interface, &createInfo, NULL, &shader);
	EZ_ASSERT(result == VK_SUCCESS, "Failed to create shader module");
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

void VUTIL_AsyncCopyHostToBuffer(void* hostdata, size_t size, VkDeviceSize buffersize, VkBuffer buffer) {
    VkDevice dev =  g_vutil_renderer_ref->vulkan.core.general.interface;
    VkDeviceSize align  = 256;
    VkDeviceSize offset = (g_vutil_renderer_ref->vulkan.core.transfer.offset + align - 1) & ~(align - 1);
    if (offset + buffersize > g_vutil_renderer_ref->vulkan.core.transfer.size) {
        vkUnmapMemory(dev, g_vutil_renderer_ref->vulkan.core.transfer.staging.memory);
        VUTIL_DestroyBuffer(g_vutil_renderer_ref->vulkan.core.transfer.staging);
        VkDeviceSize newsize = (offset + buffersize) * 2;
        VUTIL_CreateBuffer(
            newsize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &g_vutil_renderer_ref->vulkan.core.transfer.staging);
        vkMapMemory(dev, g_vutil_renderer_ref->vulkan.core.transfer.staging.memory, 0, newsize, 0, &g_vutil_renderer_ref->vulkan.core.transfer.mapped);
        g_vutil_renderer_ref->vulkan.core.transfer.size = newsize;
        g_vutil_renderer_ref->vulkan.core.transfer.offset = 0;
        offset = 0;
    }
    memcpy(g_vutil_renderer_ref->vulkan.core.transfer.mapped + offset, hostdata, size);
    g_vutil_renderer_ref->vulkan.core.transfer.offset = offset + buffersize;
    VkCommandBuffer cmd = VUTIL_BeginTransferCommands();
    VkBufferCopy region = { 0 };
    region.size = buffersize;
    region.srcOffset = offset;
    region.dstOffset = 0;
    vkCmdCopyBuffer(cmd, g_vutil_renderer_ref->vulkan.core.transfer.staging.buffer, buffer, 1, &region);
    VulkanFamilyGroup families = VUTIL_FindQueueFamilies(g_vutil_renderer_ref->vulkan.core.general.gpu);
    if (families.transfer.value != families.graphics.value) {
        VkBufferMemoryBarrier release = { 0 };
        release.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        release.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        release.dstAccessMask = 0;
        release.srcQueueFamilyIndex = families.transfer.value;
        release.dstQueueFamilyIndex = families.graphics.value;
        release.buffer = buffer;
        release.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 1, &release, 0, NULL);
    }
    VUTIL_EndTransferCommands();
}

void VUTIL_CopyBufferToHost(void* hostdata, size_t size, VkDeviceSize buffersize, VkBuffer buffer) {
    VulkanDataBuffer stagingBuffer;
    VUTIL_CreateBuffer(
        buffersize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer);
    VUTIL_CopyBuffer(buffer, stagingBuffer.buffer, buffersize);
    void* data;
    vkMapMemory(g_vutil_renderer_ref->vulkan.core.general.interface, stagingBuffer.memory, 0, buffersize, 0, &data);
    memcpy(hostdata, data, size);
    vkUnmapMemory(g_vutil_renderer_ref->vulkan.core.general.interface, stagingBuffer.memory);
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
    VkFence fence;
    VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    vkCreateFence(g_vutil_renderer_ref->vulkan.core.general.interface, &fi, NULL, &fence);
    VkSubmitInfo submitInfo = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &commandBuffer };
    vkQueueSubmit(g_vutil_renderer_ref->vulkan.core.scheduler.queue, 1, &submitInfo, fence);
    vkWaitForFences(g_vutil_renderer_ref->vulkan.core.general.interface, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(g_vutil_renderer_ref->vulkan.core.general.interface, fence, NULL);
    vkFreeCommandBuffers(g_vutil_renderer_ref->vulkan.core.general.interface, g_vutil_renderer_ref->vulkan.core.scheduler.commands.pool, 1, &commandBuffer);
}

VkCommandBuffer VUTIL_BeginTransferCommands() {
    g_vutil_renderer_ref->vulkan.core.transfer.index = (g_vutil_renderer_ref->vulkan.core.transfer.index + 1) % CPUSWAP_LENGTH;
    VkCommandBuffer cmd = g_vutil_renderer_ref->vulkan.core.transfer.commands[g_vutil_renderer_ref->vulkan.core.transfer.index];
    if (g_vutil_renderer_ref->vulkan.core.transfer.signal >= CPUSWAP_LENGTH) {
        uint64_t wait_for = g_vutil_renderer_ref->vulkan.core.transfer.signal - (CPUSWAP_LENGTH - 1);
        VkSemaphoreWaitInfo wi = { 0 };
        wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wi.semaphoreCount = 1;
        wi.pSemaphores = &g_vutil_renderer_ref->vulkan.core.transfer.semaphore;
        wi.pValues = &wait_for;
        vkWaitSemaphores(g_vutil_renderer_ref->vulkan.core.general.interface, &wi, UINT64_MAX);
    }
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi = { 0 };
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void VUTIL_EndTransferCommands() {
    VkCommandBuffer cmd = g_vutil_renderer_ref->vulkan.core.transfer.commands[g_vutil_renderer_ref->vulkan.core.transfer.index];
    vkEndCommandBuffer(cmd);
    uint64_t waitVal = g_vutil_renderer_ref->vulkan.core.transfer.signal;
    g_vutil_renderer_ref->vulkan.core.transfer.signal++;
    uint64_t signalVal = g_vutil_renderer_ref->vulkan.core.transfer.signal;
    VkTimelineSemaphoreSubmitInfo tsi = { 0 };
    tsi.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    tsi.waitSemaphoreValueCount = waitVal > 0 ? 1 : 0;
    tsi.pWaitSemaphoreValues = &waitVal;
    tsi.signalSemaphoreValueCount = 1;
    tsi.pSignalSemaphoreValues = &signalVal;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si = { 0 };
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.pNext = &tsi;
    si.waitSemaphoreCount = waitVal > 0 ? 1 : 0;
    si.pWaitSemaphores = &g_vutil_renderer_ref->vulkan.core.transfer.semaphore;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &g_vutil_renderer_ref->vulkan.core.transfer.semaphore;
    vkQueueSubmit(g_vutil_renderer_ref->vulkan.core.transfer.queue, 1, &si, VK_NULL_HANDLE);
}

void VUTIL_CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = VUTIL_BeginSingleTimeCommands();
    VkBufferCopy copyRegion = { 0 };
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    VUTIL_EndSingleTimeCommands(commandBuffer);
}

void VUTIL_RecordGeneralBarrier(VkCommandBuffer command) {
    VkMemoryBarrier memoryBarrier = { 0 };
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(
        command,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memoryBarrier, 0, NULL, 0, NULL);
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
        EZ_ASSERT(FALSE, "Unsupported layout transition!");
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
    EZ_ASSERT(result == VK_SUCCESS, "Unable to create buffer");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_vutil_renderer_ref->vulkan.core.general.interface, buffer->buffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    Schrodingnum memoryType = VUTIL_FindMemoryType(memRequirements.memoryTypeBits, properties);
    EZ_ASSERT(memoryType.exists, "Unable to find memory for vertex buffer");
    allocInfo.memoryTypeIndex = memoryType.value;
    result = vkAllocateMemory(g_vutil_renderer_ref->vulkan.core.general.interface, &allocInfo, NULL, &(buffer->memory));
    EZ_ASSERT(result == VK_SUCCESS, "Unable to allocate memory for buffer");

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
    EZ_ASSERT(result == VK_SUCCESS, "Failed to create image!");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(g_vutil_renderer_ref->vulkan.core.general.interface, image->image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    Schrodingnum memoryType = VUTIL_FindMemoryType(memRequirements.memoryTypeBits, properties);
    EZ_ASSERT(memoryType.exists, "Unable to find valid memory type");
    allocInfo.memoryTypeIndex = memoryType.value;
    result = vkAllocateMemory(g_vutil_renderer_ref->vulkan.core.general.interface, &allocInfo, NULL, &(image->memory));
    EZ_ASSERT(result == VK_SUCCESS, "Failed to allocate image memory!");

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
    EZ_ASSERT(result == VK_SUCCESS, "failed to create texture image view!");
}

void VUTIL_UploadImage(
    VulkanImage* image,
    void* data,
    uint32_t width,
    uint32_t height,
    VkFormat format) {
    VUTIL_UploadImage3D(image, data, width, height, 1, format);
}

void VUTIL_CreateImage3D(
    uint32_t width,
    uint32_t height,
    uint32_t length,
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
    imageInfo.imageType = VK_IMAGE_TYPE_3D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = length;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = numSamples;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateImage(g_vutil_renderer_ref->vulkan.core.general.interface, &imageInfo, NULL, &(image->image));
    EZ_ASSERT(result == VK_SUCCESS, "Failed to create image!");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(g_vutil_renderer_ref->vulkan.core.general.interface, image->image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    Schrodingnum memoryType = VUTIL_FindMemoryType(memRequirements.memoryTypeBits, properties);
    EZ_ASSERT(memoryType.exists, "Unable to find valid memory type");
    allocInfo.memoryTypeIndex = memoryType.value;
    result = vkAllocateMemory(g_vutil_renderer_ref->vulkan.core.general.interface, &allocInfo, NULL, &(image->memory));
    EZ_ASSERT(result == VK_SUCCESS, "Failed to allocate image memory!");

    vkBindImageMemory(g_vutil_renderer_ref->vulkan.core.general.interface, image->image, image->memory, 0);

    VkImageViewCreateInfo viewInfo = { 0 };
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    result = vkCreateImageView(g_vutil_renderer_ref->vulkan.core.general.interface, &viewInfo, NULL, &(image->view));
    EZ_ASSERT(result == VK_SUCCESS, "Failed to create texture image view!");
}

void VUTIL_UploadImage3D(
    VulkanImage* image,
    void* data,
    uint32_t width,
    uint32_t height,
    uint32_t length,
    VkFormat format) {
    uint32_t bytesPerPixel;
    switch (format) {
        case VK_FORMAT_R32_SFLOAT: bytesPerPixel = 4; break;
        case VK_FORMAT_R32G32_SFLOAT: bytesPerPixel = 8; break;
        case VK_FORMAT_R32G32B32A32_SFLOAT: bytesPerPixel = 16; break;
        case VK_FORMAT_R8_UNORM: bytesPerPixel = 1; break;
        case VK_FORMAT_R8G8B8A8_UNORM: bytesPerPixel = 4; break;
        default:
            EZ_ASSERT(0, "Unsupported format");
            return;
    }

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkDeviceSize totalBytes = (VkDeviceSize)width * height * length * bytesPerPixel;
    VkBufferCreateInfo bufInfo = { 0 };
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = totalBytes;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateBuffer(g_vutil_renderer_ref->vulkan.core.general.interface, &bufInfo, NULL, &stagingBuffer);
    EZ_ASSERT(result == VK_SUCCESS, "Failed to create staging buffer");

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(g_vutil_renderer_ref->vulkan.core.general.interface, stagingBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    Schrodingnum memType = VUTIL_FindMemoryType(
        memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    EZ_ASSERT(memType.exists, "Failed to find staging memory type");
    allocInfo.memoryTypeIndex = memType.value;
    result = vkAllocateMemory(g_vutil_renderer_ref->vulkan.core.general.interface, &allocInfo, NULL, &stagingMemory);
    EZ_ASSERT(result == VK_SUCCESS, "Failed to allocate staging memory");

    vkBindBufferMemory(g_vutil_renderer_ref->vulkan.core.general.interface, stagingBuffer, stagingMemory, 0);
    void *mapped;
    vkMapMemory(g_vutil_renderer_ref->vulkan.core.general.interface, stagingMemory, 0, totalBytes, 0, &mapped);
    memcpy(mapped, data, (size_t)totalBytes);
    vkUnmapMemory(g_vutil_renderer_ref->vulkan.core.general.interface, stagingMemory);
    VkCommandBuffer cmd = VUTIL_BeginSingleTimeCommands();
    VkImageMemoryBarrier toTransfer = { 0 };
    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransfer.srcAccessMask = 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = image->image;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel = 0;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.baseArrayLayer = 0;
    toTransfer.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &toTransfer);

    VkBufferImageCopy region = { 0 };
    region.bufferOffset = 0;
    region.bufferRowLength = 0;  // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = (VkOffset3D){ 0, 0, 0 };
    region.imageExtent = (VkExtent3D){ width, height, length };
    vkCmdCopyBufferToImage(cmd,
        stagingBuffer,
        image->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region);

    VkImageMemoryBarrier toShader = { 0 };
    toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.image = image->image;
    toShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toShader.subresourceRange.baseMipLevel = 0;
    toShader.subresourceRange.levelCount = 1;
    toShader.subresourceRange.baseArrayLayer = 0;
    toShader.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, NULL, 0, NULL, 1, &toShader);
    VUTIL_EndSingleTimeCommands(cmd);

    vkDestroyBuffer(g_vutil_renderer_ref->vulkan.core.general.interface, stagingBuffer, NULL);
    vkFreeMemory(g_vutil_renderer_ref->vulkan.core.general.interface, stagingMemory, NULL);
}

void VUTIL_DestroyImage(VulkanImage image) {
    vkDestroyImageView(g_vutil_renderer_ref->vulkan.core.general.interface, image.view, NULL);
    vkDestroyImage(g_vutil_renderer_ref->vulkan.core.general.interface, image.image, NULL);
    vkFreeMemory(g_vutil_renderer_ref->vulkan.core.general.interface, image.memory, NULL);
}

VkSampler VUTIL_CreateSampler3D() {
    VkSamplerCreateInfo info = { 0 };
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter= VK_FILTER_LINEAR;
    info.minFilter= VK_FILTER_LINEAR;
    info.mipmapMode= VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.mipLodBias = 0.0f;
    info.anisotropyEnable = VK_FALSE;
    info.compareEnable = VK_FALSE;
    info.minLod = 0.0f;
    info.maxLod = 0.0f;
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;
    VkSampler sampler;
    VkResult result = vkCreateSampler(
        g_vutil_renderer_ref->vulkan.core.general.interface,
        &info, NULL, &sampler);
    EZ_ASSERT(result == VK_SUCCESS, "Failed to create sampler");
    return sampler;
}
