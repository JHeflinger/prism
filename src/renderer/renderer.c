#include "renderer.h"
#include "core/log.h"
#include "core/file.h"
#include <GLFW/glfw3.h>
#include <easymemory.h>
#include <string.h>
#include <stb_image.h>
#include <math.h>

Renderer g_renderer;

#ifdef PROD_BUILD
    #define ENABLE_VK_VALIDATION_LAYERS FALSE
#else
    #define ENABLE_VK_VALIDATION_LAYERS TRUE
#endif

#define IMAGE_FORMAT VK_FORMAT_R8G8B8A8_SRGB

#define TEMP_WIDTH 800
#define TEMP_HEIGHT 600
#define TEMP_MODEL_PATH "assets/models/room.obj"
#define TEMP_TEXTURE_PATH "assets/images/room.png"

IMPL_ARRLIST(StaticString);
IMPL_ARRLIST(Vertex);
IMPL_ARRLIST(Index);

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
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
        LOG_FATAL("%s[VULKAN] [%s]%s %s",
            LOG_RED,
            (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT ? "GENERAL" :
                (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ? "VALIDATION" : "PERFORMANCE")),
            LOG_RESET,
            pCallbackData->pMessage);
    }

    return VK_FALSE;
}

VkVertexInputBindingDescription VertexBindingDescription() {
    VkVertexInputBindingDescription bindingDescription = { 0 };
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

TRIPLET_VkVertexInputAttributeDescription VertexAttributeDescriptions() {
    TRIPLET_VkVertexInputAttributeDescription attributeDescriptions = { 0 };
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
    return attributeDescriptions;
}

Schrodingnum FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    Schrodingnum result = { 0 };
    VkPhysicalDeviceMemoryProperties memProperties = { 0 };
    vkGetPhysicalDeviceMemoryProperties(g_renderer.vulkan.gpu, &memProperties);
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

VkCommandBuffer BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = g_renderer.vulkan.command_pool;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(g_renderer.vulkan.interface, &allocInfo, &commandBuffer);
    VkCommandBufferBeginInfo beginInfo = { 0 };
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

BOOL HasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

VkFormat FindSupportedFormat(VkFormat* candidates, size_t num_candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (size_t i = 0; i < num_candidates; i++) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(g_renderer.vulkan.gpu, candidates[i], &props);
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return candidates[i];
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return candidates[i];
        }
    }
    LOG_FATAL("Failed to find supported format!");
}

VkFormat FindDepthFormat() {
    VkFormat formats[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    return FindSupportedFormat(
        formats,
        3,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

void EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo = { 0 };
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(g_renderer.vulkan.graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_renderer.vulkan.graphics_queue);
    vkFreeCommandBuffers(g_renderer.vulkan.interface, g_renderer.vulkan.command_pool, 1, &commandBuffer);
}

void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
    VkBufferCopy copyRegion = { 0 };
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    EndSingleTimeCommands(commandBuffer);
}

void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
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
    EndSingleTimeCommands(commandBuffer);
}

void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
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
    EndSingleTimeCommands(commandBuffer);
}

void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* buffer, VkDeviceMemory* bufferMemory) {
    VkBufferCreateInfo bufferInfo = { 0 };
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateBuffer(g_renderer.vulkan.interface, &bufferInfo, NULL, buffer);
    LOG_ASSERT(result == VK_SUCCESS, "Unable to create buffer");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_renderer.vulkan.interface, *buffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    Schrodingnum memoryType = FindMemoryType(memRequirements.memoryTypeBits, properties);
    LOG_ASSERT(memoryType.exists, "Unable to find memory for vertex buffer");
    allocInfo.memoryTypeIndex = memoryType.value;
    result = vkAllocateMemory(g_renderer.vulkan.interface, &allocInfo, NULL, bufferMemory);
    LOG_ASSERT(result == VK_SUCCESS, "Unable to allocate memory for buffer");

    vkBindBufferMemory(g_renderer.vulkan.interface, *buffer, *bufferMemory, 0);
}

void CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage* image, VkDeviceMemory* imageMemory) {
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
    VkResult result = vkCreateImage(g_renderer.vulkan.interface, &imageInfo, NULL, image);
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create image!");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(g_renderer.vulkan.interface, *image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    Schrodingnum memoryType = FindMemoryType(memRequirements.memoryTypeBits, properties);
    LOG_ASSERT(memoryType.exists, "Unable to find valid memory type");
    allocInfo.memoryTypeIndex = memoryType.value;
    result = vkAllocateMemory(g_renderer.vulkan.interface, &allocInfo, NULL, imageMemory);
    LOG_ASSERT(result == VK_SUCCESS, "Failed to allocate image memory!");

    vkBindImageMemory(g_renderer.vulkan.interface, *image, *imageMemory, 0);
}

VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels) {
    VkImageViewCreateInfo viewInfo = { 0 };
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    VkResult result = vkCreateImageView(g_renderer.vulkan.interface, &viewInfo, NULL, &imageView);
    LOG_ASSERT(result == VK_SUCCESS, "failed to create texture image view!");

    return imageView;
}

VkSampleCountFlagBits GetMaximumSampleCount() {
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(g_renderer.vulkan.gpu, &physicalDeviceProperties);

    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
    if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
    if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
    if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
    if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
    if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

    return VK_SAMPLE_COUNT_1_BIT;
}

void LoadTempModel() {
    Model model = LoadModel(TEMP_MODEL_PATH);
    LOG_ASSERT(model.meshCount != 0, "Failed to load model!");
    Mesh mesh = model.meshes[0];
    for (int i = 0; i < mesh.vertexCount; i++) {
        Vertex vertex = {
            {
                mesh.vertices[i * 3 + 0],
                mesh.vertices[i * 3 + 1],
                mesh.vertices[i * 3 + 2]
            },
            { 1.0f, 1.0f, 1.0f },
            {
                mesh.texcoords[i * 2 + 0],
                mesh.texcoords[i * 2 + 1]
            }
        };
        ARRLIST_Vertex_add(&(g_renderer.vertices), vertex);
        ARRLIST_Index_add(&(g_renderer.indices), (Index)i); //TODO: implement hashmap in EasyObjects to help reduce duplicate triangles
    }
    UnloadModel(model);
}

void InitializeVulkanData() {
    // initialize sample count
    g_renderer.vulkan.msaa_samples = VK_SAMPLE_COUNT_1_BIT;

    // set up validation layers
    ARRLIST_StaticString_add(&(g_renderer.vulkan.validation_layers), "VK_LAYER_KHRONOS_validation");

    // set up required extensions
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    for (size_t i = 0; i < glfwExtensionCount; i++)
        ARRLIST_StaticString_add(&(g_renderer.vulkan.required_extensions), glfwExtensions[i]);
    if (ENABLE_VK_VALIDATION_LAYERS) {
        ARRLIST_StaticString_add(&(g_renderer.vulkan.required_extensions), VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // set up device extensions
    ARRLIST_StaticString_add(&(g_renderer.vulkan.device_extensions), VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // default gpu to null
    g_renderer.vulkan.gpu = VK_NULL_HANDLE;
}

BOOL CheckValidationLayerSupport() {
    // grab all available layers
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    VkLayerProperties* availableLayers = EZALLOC(layerCount, sizeof(VkLayerProperties));
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

    // check if layers in validation layers exist in the available layers
    for (size_t i = 0; i < g_renderer.vulkan.validation_layers.size; i++) {
        BOOL layerFound = FALSE;
        for (size_t j = 0; j < layerCount; j++) {
            if (strcmp(ARRLIST_StaticString_get(&(g_renderer.vulkan.validation_layers), i), availableLayers[j].layerName) == 0) {
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

BOOL CheckGPUExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);
    VkExtensionProperties* availableExtensions = EZALLOC(extensionCount, sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, availableExtensions);
    for (size_t i = 0; i < g_renderer.vulkan.device_extensions.size; i++) {
        BOOL extensionFound = FALSE;
        for (size_t j = 0; j < extensionCount; j++) {
            if (strcmp(ARRLIST_StaticString_get(&(g_renderer.vulkan.device_extensions), i), availableExtensions[j].extensionName) == 0) {
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

void CreateVulkanInstance() {
    // error check for validation layer support
    LOG_ASSERT(ENABLE_VK_VALIDATION_LAYERS && CheckValidationLayerSupport(), "Requested validation layers are not available");

    // create app info (technically optional)
    VkApplicationInfo appInfo = { 0 };
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Prism Renderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // create info
    VkInstanceCreateInfo createInfo = { 0 };
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = (uint32_t)(g_renderer.vulkan.required_extensions.size);
    createInfo.ppEnabledExtensionNames = g_renderer.vulkan.required_extensions.data;
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = { 0 };
    if (ENABLE_VK_VALIDATION_LAYERS) {
        createInfo.enabledLayerCount = g_renderer.vulkan.validation_layers.size;
        createInfo.ppEnabledLayerNames = g_renderer.vulkan.validation_layers.data;

        // set up additional debug callback for the creation of the instance
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = VulkanDebugCallback;
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    // create instance
    VkResult result = vkCreateInstance(&createInfo, NULL, &(g_renderer.vulkan.instance));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create vulkan instance");
}

void SetupVulkanMessenger() {
    if (!ENABLE_VK_VALIDATION_LAYERS) return;
    VkDebugUtilsMessengerCreateInfoEXT createInfo = { 0 };
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = VulkanDebugCallback;
    createInfo.pUserData = NULL;
    PFN_vkCreateDebugUtilsMessengerEXT messenger_extension = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g_renderer.vulkan.instance, "vkCreateDebugUtilsMessengerEXT");
    LOG_ASSERT(messenger_extension != NULL, "Failed to set up debug messenger");
    messenger_extension(g_renderer.vulkan.instance, &createInfo, NULL, &(g_renderer.vulkan.messenger));
}

VulkanFamilyGroup FindQueueFamilies(VkPhysicalDevice gpu) {
    VulkanFamilyGroup group = { 0 };
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* queueFamilies = EZALLOC(queueFamilyCount, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queueFamilyCount, queueFamilies);
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            group.graphics = (Schrodingnum){ i, TRUE };
            break;
        }
    }
    EZFREE(queueFamilies);
    return group;
}

void PickGPU() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(g_renderer.vulkan.instance, &deviceCount, NULL);
    LOG_ASSERT(deviceCount != 0, "No devices with vulkan support were found");
    VkPhysicalDevice* devices = EZALLOC(deviceCount, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(g_renderer.vulkan.instance, &deviceCount, devices);
    uint32_t score = 0;
    uint32_t ind = 0;
    for (uint32_t i = 0; i < deviceCount; i++) {
        // check if device is suitable
        VulkanFamilyGroup families = FindQueueFamilies(devices[i]);
        if (!families.graphics.exists) continue;
        if (!CheckGPUExtensionSupport(devices[i])) continue;

        uint32_t curr_score = 0;
        VkPhysicalDeviceProperties deviceProperties;
        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);
        vkGetPhysicalDeviceFeatures(devices[i], &deviceFeatures);
        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) curr_score += 1000;
        curr_score += deviceProperties.limits.maxImageDimension2D;
        if (!deviceFeatures.geometryShader) curr_score = 0;
        if (!deviceFeatures.samplerAnisotropy) curr_score = 0;
        if (curr_score > score) {
            score = curr_score;
            ind = i;
        }
    }
    LOG_ASSERT(score != 0, "A suitable GPU could not be found");
    g_renderer.vulkan.gpu = devices[ind];
    g_renderer.vulkan.msaa_samples = GetMaximumSampleCount();
    EZFREE(devices);
}

void CreateDeviceInterface() {
    VulkanFamilyGroup families = FindQueueFamilies(g_renderer.vulkan.gpu);
    VkDeviceQueueCreateInfo queueCreateInfo = { 0 };
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = families.graphics.value;
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    VkPhysicalDeviceFeatures deviceFeatures = { 0 };
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.sampleRateShading = VK_TRUE;
    VkDeviceCreateInfo createInfo = { 0 };
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = g_renderer.vulkan.device_extensions.size;
    createInfo.ppEnabledExtensionNames  = g_renderer.vulkan.device_extensions.data;
    if (ENABLE_VK_VALIDATION_LAYERS) {
        createInfo.enabledLayerCount = g_renderer.vulkan.validation_layers.size;
        createInfo.ppEnabledLayerNames = g_renderer.vulkan.validation_layers.data;
    } else {
        createInfo.enabledLayerCount = 0;
    }
    VkResult result = vkCreateDevice(g_renderer.vulkan.gpu, &createInfo, NULL, &(g_renderer.vulkan.interface));
    LOG_ASSERT(result == VK_SUCCESS, "failed to create logical device");
    vkGetDeviceQueue(g_renderer.vulkan.interface, families.graphics.value, 0, &(g_renderer.vulkan.graphics_queue));
}

void CreateDestinationImage() {
    // create image
    CreateImage(
        g_renderer.dimensions.x,
        g_renderer.dimensions.y,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        IMAGE_FORMAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(g_renderer.vulkan.image),
        &(g_renderer.vulkan.image_memory));

    // create image view
    g_renderer.vulkan.view = CreateImageView(g_renderer.vulkan.image, IMAGE_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    // create cross buffer
    CreateBuffer(
        g_renderer.dimensions.x * g_renderer.dimensions.y * 4, // Assuming VK_FORMAT_B8G8R8A8_SRGB
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        &(g_renderer.vulkan.cross_buffer),
        &(g_renderer.vulkan.cross_memory));

    // map memory to buffer
    vkMapMemory(g_renderer.vulkan.interface, g_renderer.vulkan.cross_memory, 0, VK_WHOLE_SIZE, 0, &(g_renderer.swapchain.reference));
}

VkShaderModule CreateShader(SimpleFile* file) {
	VkShaderModuleCreateInfo createInfo = { 0 };
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = file->size;
	createInfo.pCode = (const uint32_t*)(file->data);
	VkShaderModule shader;
	VkResult result = vkCreateShaderModule(g_renderer.vulkan.interface, &createInfo, NULL, &shader);
	LOG_ASSERT(result == VK_SUCCESS, "Failed to create shader module");
	return shader;
}

void CreatePipeline() {
	SimpleFile* vertshadercode = ReadFile("build/shaders/shader.vert.spv");
	SimpleFile* fragshadercode = ReadFile("build/shaders/shader.frag.spv");
	VkShaderModule vertshader = CreateShader(vertshadercode);
	VkShaderModule fragshader = CreateShader(fragshadercode);

	VkPipelineShaderStageCreateInfo vertShaderStageInfo = { 0 };
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertshader;
	vertShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo = { 0 };
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragshader;
	fragShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderstages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkVertexInputBindingDescription bindingDescription = VertexBindingDescription();
    TRIPLET_VkVertexInputAttributeDescription attributeDescriptions = VertexAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { 0 };
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.value;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { 0 };
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkDynamicState dynamicStates[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState = { 0 };
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineViewportStateCreateInfo viewportState = { 0 };
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = { 0 };
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f; // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

    VkPipelineMultisampleStateCreateInfo multisampling = { 0 };
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.rasterizationSamples = g_renderer.vulkan.msaa_samples;
    multisampling.minSampleShading = 0.2f; // closer to one is smoother anti-aliasing
    multisampling.pSampleMask = NULL; // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE; // Optional

    VkPipelineColorBlendAttachmentState colorBlendAttachment = { 0 };
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending = { 0 };
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { 0 };
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &(g_renderer.vulkan.descriptor_set_layout);
    pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
    pipelineLayoutInfo.pPushConstantRanges = NULL; // Optional

    VkResult result = vkCreatePipelineLayout(g_renderer.vulkan.interface, &pipelineLayoutInfo, NULL, &(g_renderer.vulkan.pipeline_layout));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create pipeline layout");

    VkPipelineDepthStencilStateCreateInfo depthStencil = { 0 };
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f; // Optional
    depthStencil.maxDepthBounds = 1.0f; // Optional
    depthStencil.stencilTestEnable = VK_FALSE;
    //depthStencil.front = { 0 }; // Optional
    //depthStencil.back = { 0 }; // Optional

    VkGraphicsPipelineCreateInfo pipelineInfo = { 0 };
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderstages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = g_renderer.vulkan.pipeline_layout;
    pipelineInfo.renderPass = g_renderer.vulkan.render_pass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1; // Optional

    result = vkCreateGraphicsPipelines(g_renderer.vulkan.interface, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &(g_renderer.vulkan.pipeline));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create pipeline");

	FreeFile(vertshadercode);
	FreeFile(fragshadercode);
	vkDestroyShaderModule(g_renderer.vulkan.interface, vertshader, NULL);
	vkDestroyShaderModule(g_renderer.vulkan.interface, fragshader, NULL);
}

void CreateRenderPass() {
    VkAttachmentDescription colorAttachment = { 0 };
    colorAttachment.format = IMAGE_FORMAT;
    colorAttachment.samples = g_renderer.vulkan.msaa_samples;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef = { 0 };
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment = { 0 };
    depthAttachment.format = FindDepthFormat();
    depthAttachment.samples = g_renderer.vulkan.msaa_samples;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = { 0 };
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorAttachmentResolve = { 0 };
    colorAttachmentResolve.format = IMAGE_FORMAT;
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorAttachmentResolveRef = { 0 };
    colorAttachmentResolveRef.attachment = 2;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = { 0 };
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    subpass.pResolveAttachments = &colorAttachmentResolveRef;

    VkSubpassDependency dependency = { 0 };
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment, colorAttachmentResolve };
    VkRenderPassCreateInfo renderPassInfo = { 0 };
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 3;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(g_renderer.vulkan.interface, &renderPassInfo, NULL, &(g_renderer.vulkan.render_pass));
    LOG_ASSERT(result == VK_SUCCESS, "Unable to create render pass");
}

void CreateFramebuffers() {
    VkImageView attachments[] = { g_renderer.vulkan.color_image_view, g_renderer.vulkan.depth_image_view, g_renderer.vulkan.view };
    VkFramebufferCreateInfo framebufferInfo = { 0 };
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = g_renderer.vulkan.render_pass;
    framebufferInfo.attachmentCount = 3;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = g_renderer.dimensions.x;
    framebufferInfo.height = g_renderer.dimensions.y;
    framebufferInfo.layers = 1;
    VkResult result = vkCreateFramebuffer(g_renderer.vulkan.interface, &framebufferInfo, NULL, &(g_renderer.vulkan.framebuffer));
    LOG_ASSERT(result == VK_SUCCESS, "Unable to create framebuffer");
}

void CreateCommandPool() {
    VulkanFamilyGroup queueFamilyIndices = FindQueueFamilies(g_renderer.vulkan.gpu);
    VkCommandPoolCreateInfo poolInfo = { 0 };
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphics.value;
    VkResult result = vkCreateCommandPool(g_renderer.vulkan.interface, &poolInfo, NULL, &(g_renderer.vulkan.command_pool));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create command pool!");
}

void CreateCommandBuffers() {
    VkCommandBufferAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = g_renderer.vulkan.command_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = CPUSWAP_LENGTH;
    VkResult result = vkAllocateCommandBuffers(g_renderer.vulkan.interface, &allocInfo, g_renderer.vulkan.commands);
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create command buffer");
}

void RecordCommand(VkCommandBuffer command) {
    // Start command
    VkCommandBufferBeginInfo beginInfo = { 0 };
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // Optional
    beginInfo.pInheritanceInfo = NULL; // Optional
    VkResult result = vkBeginCommandBuffer(command, &beginInfo);
    LOG_ASSERT(result == VK_SUCCESS, "Failed to begin recording command buffer!");

    // Rendering
    {
        VkRenderPassBeginInfo renderPassInfo = { 0 };
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = g_renderer.vulkan.render_pass;
        renderPassInfo.framebuffer = g_renderer.vulkan.framebuffer;
        renderPassInfo.renderArea.offset = (VkOffset2D){ 0, 0 };
        renderPassInfo.renderArea.extent = (VkExtent2D){ g_renderer.dimensions.x, g_renderer.dimensions.y };

        VkClearValue clearValues[2] = { 0 };
        clearValues[0].color = (VkClearColorValue){{ 0.0f, 0.0f, 0.0f, 1.0f }};
        clearValues[1].depthStencil = (VkClearDepthStencilValue){ 1.0f, 0 };
        renderPassInfo.clearValueCount = 2;
        renderPassInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(command, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, g_renderer.vulkan.pipeline);

        VkViewport viewport = { 0 };
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = g_renderer.dimensions.x;
        viewport.height = g_renderer.dimensions.y;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(command, 0, 1, &viewport);

        VkRect2D scissor = { 0 };
        scissor.offset = (VkOffset2D){ 0, 0 };
        scissor.extent = (VkExtent2D){ g_renderer.dimensions.x, g_renderer.dimensions.y };
        vkCmdSetScissor(command, 0, 1, &scissor);

        VkBuffer vertexBuffers[] = { g_renderer.vulkan.vertex_buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(command, 0, 1, vertexBuffers, offsets);

        vkCmdBindIndexBuffer(command, g_renderer.vulkan.index_buffer, 0, INDEX_VK_TYPE);

        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, g_renderer.vulkan.pipeline_layout, 0, 1, &(g_renderer.vulkan.descriptor_sets[g_renderer.swapchain.index]), 0, NULL);

        vkCmdDrawIndexed(command, (uint32_t)g_renderer.indices.size, 1, 0, 0, 0);

        vkCmdEndRenderPass(command);
    }

    // Copy image to staging
    {
        VkBufferImageCopy region = { 0 };
        region.bufferOffset = 0;
        region.bufferRowLength = 0; // Tightly packed
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = (VkOffset3D){ 0, 0, 0 };
        region.imageExtent = (VkExtent3D){ g_renderer.dimensions.x, g_renderer.dimensions.y, 1 };
        vkCmdCopyImageToBuffer(command, g_renderer.vulkan.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_renderer.vulkan.cross_buffer, 1, &region);
    }

    // End command
    result = vkEndCommandBuffer(command);
    LOG_ASSERT(result == VK_SUCCESS, "Failed to record command buffer!");
}

void CreateSyncObjects() {
    for (int i = 0; i < CPUSWAP_LENGTH; i++) {
        VkFenceCreateInfo fenceInfo = { 0 };
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (i != 0) fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkResult result = vkCreateFence(g_renderer.vulkan.interface, &fenceInfo, NULL, &(g_renderer.vulkan.syncro.fences[i]));
        LOG_ASSERT(result == VK_SUCCESS, "Failed to create fence");
    }
}

void CreateDepthResources() {
    VkFormat depthFormat = FindDepthFormat();
    CreateImage(
        g_renderer.dimensions.x,
        g_renderer.dimensions.y,
        1,
        g_renderer.vulkan.msaa_samples,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(g_renderer.vulkan.depth_image), 
        &(g_renderer.vulkan.depth_image_memory));
    g_renderer.vulkan.depth_image_view = CreateImageView(g_renderer.vulkan.depth_image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

void CreateColorResources() {
    CreateImage(
        g_renderer.dimensions.x,
        g_renderer.dimensions.y,
        1,
        g_renderer.vulkan.msaa_samples,
        IMAGE_FORMAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(g_renderer.vulkan.color_image),
        &(g_renderer.vulkan.color_image_memory));
    g_renderer.vulkan.color_image_view = CreateImageView(g_renderer.vulkan.color_image, IMAGE_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void CleanSwapchain() {
    // destroy depth buffer
    vkDestroyImageView(g_renderer.vulkan.interface, g_renderer.vulkan.depth_image_view, NULL);
    vkDestroyImage(g_renderer.vulkan.interface, g_renderer.vulkan.depth_image, NULL);
    vkFreeMemory(g_renderer.vulkan.interface, g_renderer.vulkan.depth_image_memory, NULL);

    // destroy framebuffer
    vkDestroyFramebuffer(g_renderer.vulkan.interface, g_renderer.vulkan.framebuffer, NULL);

    // unmap and destroy cross buffer
    vkUnmapMemory(g_renderer.vulkan.interface, g_renderer.vulkan.cross_memory);
    vkFreeMemory(g_renderer.vulkan.interface, g_renderer.vulkan.cross_memory, NULL);
    vkDestroyBuffer(g_renderer.vulkan.interface, g_renderer.vulkan.cross_buffer, NULL);

    // destroy image
    vkDestroyImageView(g_renderer.vulkan.interface, g_renderer.vulkan.view, NULL);
    vkDestroyImage(g_renderer.vulkan.interface, g_renderer.vulkan.image, NULL);
    vkFreeMemory(g_renderer.vulkan.interface, g_renderer.vulkan.image_memory, NULL);

    // destroy color image
    vkDestroyImageView(g_renderer.vulkan.interface, g_renderer.vulkan.color_image_view, NULL);
    vkDestroyImage(g_renderer.vulkan.interface, g_renderer.vulkan.color_image, NULL);
    vkFreeMemory(g_renderer.vulkan.interface, g_renderer.vulkan.color_image_memory, NULL);
}

void RecreateSwapchain() {
    vkDeviceWaitIdle(g_renderer.vulkan.interface);
    g_renderer.dimensions = (Vector2){ GetScreenWidth(), GetScreenHeight() };
    CleanSwapchain();
    CreateDestinationImage();
    CreateColorResources();
    CreateDepthResources();
    CreateFramebuffers();
}

void CreateVertexBuffer() {
    VkDeviceSize size = sizeof(Vertex) * g_renderer.vertices.size;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer,
        &stagingBufferMemory);

    void* data;
    vkMapMemory(g_renderer.vulkan.interface, stagingBufferMemory, 0, size, 0, &data);
    memcpy(data, g_renderer.vertices.data, (size_t)size);
    vkUnmapMemory(g_renderer.vulkan.interface, stagingBufferMemory);

    CreateBuffer(
        size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &(g_renderer.vulkan.vertex_buffer),
        &(g_renderer.vulkan.vertex_memory));
    
    CopyBuffer(stagingBuffer, g_renderer.vulkan.vertex_buffer, size);

    vkDestroyBuffer(g_renderer.vulkan.interface, stagingBuffer, NULL);
    vkFreeMemory(g_renderer.vulkan.interface, stagingBufferMemory, NULL);
}

void CreateIndexBuffer() {
    VkDeviceSize size = sizeof(Index) * g_renderer.indices.size;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer,
        &stagingBufferMemory);

    void* data;
    vkMapMemory(g_renderer.vulkan.interface, stagingBufferMemory, 0, size, 0, &data);
    memcpy(data, g_renderer.indices.data, (size_t)size);
    vkUnmapMemory(g_renderer.vulkan.interface, stagingBufferMemory);

    CreateBuffer(
        size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &(g_renderer.vulkan.index_buffer),
        &(g_renderer.vulkan.index_memory));
    
    CopyBuffer(stagingBuffer, g_renderer.vulkan.index_buffer, size);

    vkDestroyBuffer(g_renderer.vulkan.interface, stagingBuffer, NULL);
    vkFreeMemory(g_renderer.vulkan.interface, stagingBufferMemory, NULL);
}

void CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding = { 0 };
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = NULL;

    VkDescriptorSetLayoutBinding samplerLayoutBinding = { 0 };
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = NULL;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bindings[] = { uboLayoutBinding, samplerLayoutBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo = { 0 };
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    VkResult result = vkCreateDescriptorSetLayout(g_renderer.vulkan.interface, &layoutInfo, NULL, &(g_renderer.vulkan.descriptor_set_layout));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create descriptor set layout!");
}

void CreateUniformBuffers() {
    VkDeviceSize size = sizeof(UniformBufferObject);
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        CreateBuffer(
            size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &(g_renderer.vulkan.uniform_buffers.buffers[i]),
            &(g_renderer.vulkan.uniform_buffers.memory[i]));

        vkMapMemory(g_renderer.vulkan.interface, g_renderer.vulkan.uniform_buffers.memory[i], 0, size, 0, &g_renderer.vulkan.uniform_buffers.mapped[i]);
    }
}

void UpdateUniformBuffers() {
    static float time = 0.0f;
    time += GetFrameTime();
    UniformBufferObject ubo = { 0 };
    glm_mat4_identity(ubo.model);
    glm_rotate(ubo.model, time * glm_rad(90.0f), (vec3){ 0.0f, 0.0f, 1.0f });
    glm_lookat((vec3){ 2.0f, 2.0f, 2.0f }, (vec3){ 0.0f, 0.0f, 0.0f }, (vec3){ 0.0f, 0.0f, 1.0f }, ubo.view);
    glm_perspective(glm_rad(45.0f), g_renderer.dimensions.x / g_renderer.dimensions.y, 0.1f, 10.0f, ubo.projection);
    ubo.projection[1][1] *= -1;
    memcpy(g_renderer.vulkan.uniform_buffers.mapped[g_renderer.swapchain.index], &ubo, sizeof(UniformBufferObject));
}

void CreateDescriptorPool() {
    VkDescriptorPoolSize poolSizes[2] = { 0 };
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = CPUSWAP_LENGTH;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = CPUSWAP_LENGTH;

    VkDescriptorPoolCreateInfo poolInfo = { 0 };
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = CPUSWAP_LENGTH;
    VkResult result = vkCreateDescriptorPool(g_renderer.vulkan.interface, &poolInfo, NULL, &(g_renderer.vulkan.descriptor_pool));
    LOG_ASSERT(result == VK_SUCCESS, "failed to create descriptor pool!");
}

void CreateDescriptorSets() {
    VkDescriptorSetLayout layouts[CPUSWAP_LENGTH];
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) layouts[i] = g_renderer.vulkan.descriptor_set_layout;
    VkDescriptorSetAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = g_renderer.vulkan.descriptor_pool;
    allocInfo.descriptorSetCount = CPUSWAP_LENGTH;
    allocInfo.pSetLayouts = layouts;
    VkResult result = vkAllocateDescriptorSets(g_renderer.vulkan.interface, &allocInfo, g_renderer.vulkan.descriptor_sets);
    LOG_ASSERT(result == VK_SUCCESS, "Failed to allocate descriptor sets!");
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VkDescriptorBufferInfo bufferInfo = { 0 };
        bufferInfo.buffer = g_renderer.vulkan.uniform_buffers.buffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo = { 0 };
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = g_renderer.vulkan.texture_image_view;
        imageInfo.sampler = g_renderer.vulkan.texture_sampler;

        VkWriteDescriptorSet descriptorWrites[2] = { 0 };
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = g_renderer.vulkan.descriptor_sets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = g_renderer.vulkan.descriptor_sets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(g_renderer.vulkan.interface, 2, descriptorWrites, 0, NULL);
    }
}

void GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) {
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(g_renderer.vulkan.gpu, imageFormat, &formatProperties);
    LOG_ASSERT(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT, "Texture format does not support linear filtering!");

    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

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

    EndSingleTimeCommands(commandBuffer);
}

void CreateTextureImage() {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(TEMP_TEXTURE_PATH, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    LOG_ASSERT(pixels, "Failed to load texture image");
    g_renderer.vulkan.mip_levels = (uint32_t)floor(log2(texWidth > texHeight ? texWidth : texHeight)) + 1;
    VkDeviceSize imageSize = texWidth * texHeight * 4;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer,
        &stagingBufferMemory);
    void* data;
    vkMapMemory(g_renderer.vulkan.interface, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, (size_t)imageSize);
    vkUnmapMemory(g_renderer.vulkan.interface, stagingBufferMemory);
    stbi_image_free(pixels);
    CreateImage(
        texWidth,
        texHeight,
        g_renderer.vulkan.mip_levels,
        VK_SAMPLE_COUNT_1_BIT,
        IMAGE_FORMAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(g_renderer.vulkan.texture_image),
        &(g_renderer.vulkan.texture_image_memory));
    TransitionImageLayout(g_renderer.vulkan.texture_image, IMAGE_FORMAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, g_renderer.vulkan.mip_levels);
    CopyBufferToImage(stagingBuffer, g_renderer.vulkan.texture_image, (uint32_t)texWidth, (uint32_t)texHeight);
    GenerateMipmaps(g_renderer.vulkan.texture_image, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, g_renderer.vulkan.mip_levels);
    vkDestroyBuffer(g_renderer.vulkan.interface, stagingBuffer, NULL);
    vkFreeMemory(g_renderer.vulkan.interface, stagingBufferMemory, NULL);
}

void CreateTextureImageView() {
    g_renderer.vulkan.texture_image_view = CreateImageView(g_renderer.vulkan.texture_image, IMAGE_FORMAT, VK_IMAGE_ASPECT_COLOR_BIT, g_renderer.vulkan.mip_levels);
}

void CreateTextureSampler() {
    VkPhysicalDeviceProperties properties = { 0 };
    vkGetPhysicalDeviceProperties(g_renderer.vulkan.gpu, &properties);
    VkSamplerCreateInfo samplerInfo = { 0 };
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = (float)g_renderer.vulkan.mip_levels;
    VkResult result = vkCreateSampler(g_renderer.vulkan.interface, &samplerInfo, NULL, &(g_renderer.vulkan.texture_sampler));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create texture sampler");
}

void DestroyVulkan() {
    // wait for device to finish
    vkDeviceWaitIdle(g_renderer.vulkan.interface);

    // clean swapchain
    CleanSwapchain();

    // destroy texture
    vkDestroyImage(g_renderer.vulkan.interface, g_renderer.vulkan.texture_image, NULL);
    vkFreeMemory(g_renderer.vulkan.interface, g_renderer.vulkan.texture_image_memory, NULL);
    vkDestroySampler(g_renderer.vulkan.interface, g_renderer.vulkan.texture_sampler, NULL);
    vkDestroyImageView(g_renderer.vulkan.interface, g_renderer.vulkan.texture_image_view, NULL);

    // destroy uniform buffers
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        vkDestroyBuffer(g_renderer.vulkan.interface, g_renderer.vulkan.uniform_buffers.buffers[i], NULL);
        vkFreeMemory(g_renderer.vulkan.interface, g_renderer.vulkan.uniform_buffers.memory[i], NULL);
    }

    // destroy descriptor pool
    vkDestroyDescriptorPool(g_renderer.vulkan.interface, g_renderer.vulkan.descriptor_pool, NULL);

    // destroy descriptor set layout
    vkDestroyDescriptorSetLayout(g_renderer.vulkan.interface, g_renderer.vulkan.descriptor_set_layout, NULL);

    // destroy vertex buffer
    vkDestroyBuffer(g_renderer.vulkan.interface, g_renderer.vulkan.vertex_buffer, NULL);

    // free vertex memory
    vkFreeMemory(g_renderer.vulkan.interface, g_renderer.vulkan.vertex_memory, NULL);

    // destroy index buffer
    vkDestroyBuffer(g_renderer.vulkan.interface, g_renderer.vulkan.index_buffer, NULL);

    // free index memory
    vkFreeMemory(g_renderer.vulkan.interface, g_renderer.vulkan.index_memory, NULL);

    // destroy syncro objects
    for (int i = 0; i < CPUSWAP_LENGTH; i++)
        vkDestroyFence(g_renderer.vulkan.interface, g_renderer.vulkan.syncro.fences[i], NULL);

    // destroy command pool
    vkDestroyCommandPool(g_renderer.vulkan.interface, g_renderer.vulkan.command_pool, NULL);

    // destroy pipeline
    vkDestroyPipeline(g_renderer.vulkan.interface, g_renderer.vulkan.pipeline, NULL);
    vkDestroyPipelineLayout(g_renderer.vulkan.interface, g_renderer.vulkan.pipeline_layout, NULL);
    vkDestroyRenderPass(g_renderer.vulkan.interface, g_renderer.vulkan.render_pass, NULL);

    // destroy vulkan device
    vkDestroyDevice(g_renderer.vulkan.interface, NULL);

    // destroy debug messenger
    if (ENABLE_VK_VALIDATION_LAYERS) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g_renderer.vulkan.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_messenger != NULL)
            destroy_messenger(g_renderer.vulkan.instance, g_renderer.vulkan.messenger, NULL);
    }

    // destroy vulkan instance
    vkDestroyInstance(g_renderer.vulkan.instance, NULL);

    // clean required extensions
    ARRLIST_StaticString_clear(&(g_renderer.vulkan.validation_layers));
    ARRLIST_StaticString_clear(&(g_renderer.vulkan.required_extensions));
    ARRLIST_StaticString_clear(&(g_renderer.vulkan.device_extensions));
}

void InitializeVulkan() {
    LoadTempModel();
    InitializeVulkanData();
    CreateVulkanInstance();
    SetupVulkanMessenger();
    PickGPU();
    CreateDeviceInterface();
    CreateDestinationImage();
    CreateRenderPass();
	CreateDescriptorSetLayout();
    CreatePipeline();
    CreateCommandPool();
    CreateColorResources();
    CreateDepthResources();
    CreateFramebuffers();
    CreateTextureImage();
    CreateTextureImageView();
    CreateTextureSampler();
    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateCommandBuffers();
    CreateSyncObjects();
}

void InitializeRenderer() {
    // set up dimensions
    g_renderer.dimensions = (Vector2){ GetScreenWidth(), GetScreenHeight() };

    // initialize vulkan resources
    InitializeVulkan();

    // set up cpu swap
	for (int i = 0; i < CPUSWAP_LENGTH; i++) {
		g_renderer.swapchain.targets[i] = LoadRenderTexture(g_renderer.dimensions.x, g_renderer.dimensions.y);
		LOG_ASSERT(IsRenderTextureValid(g_renderer.swapchain.targets[i]), "Unable to load target texture");
	}

    // configure stat profiler
    ConfigureProfile(&(g_renderer.stats.profile), "Renderer", 100);
}

void DestroyRenderer() {
    // destroy vulkan resources
    DestroyVulkan();

    // unload cpu swap textures
	for (int i = 0; i < CPUSWAP_LENGTH; i++)
		UnloadRenderTexture(g_renderer.swapchain.targets[i]);

    // clean vertex data
    ARRLIST_Vertex_clear(&(g_renderer.vertices));

    // clean index data
    ARRLIST_Index_clear(&(g_renderer.indices));
}

void Render() {
    // profile for stats
    BeginProfile(&(g_renderer.stats.profile));

    // check for window changes
    if (g_renderer.dimensions.x != GetScreenWidth() || g_renderer.dimensions.y != GetScreenHeight()) RecreateSwapchain();

    // update uniform buffers
    UpdateUniformBuffers();

    // reset command buffer and record it
    vkResetCommandBuffer(g_renderer.vulkan.commands[g_renderer.swapchain.index], 0);
    RecordCommand(g_renderer.vulkan.commands[g_renderer.swapchain.index]);

    // submit command buffer
    VkSubmitInfo submitInfo = { 0 };
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &(g_renderer.vulkan.commands[g_renderer.swapchain.index]);
    submitInfo.signalSemaphoreCount = 0;
    VkResult result = vkQueueSubmit(g_renderer.vulkan.graphics_queue, 1, &submitInfo, g_renderer.vulkan.syncro.fences[g_renderer.swapchain.index]);
    LOG_ASSERT(result == VK_SUCCESS, "failed to submit draw command buffer!");

    // wait for and reset rendering fence
	g_renderer.swapchain.index = (g_renderer.swapchain.index + 1) % CPUSWAP_LENGTH;
    vkWaitForFences(g_renderer.vulkan.interface, 1, &(g_renderer.vulkan.syncro.fences[g_renderer.swapchain.index]), VK_TRUE, UINT64_MAX);
    vkResetFences(g_renderer.vulkan.interface, 1, &(g_renderer.vulkan.syncro.fences[g_renderer.swapchain.index]));

    // update render target
    glBindTexture(GL_TEXTURE_2D, g_renderer.swapchain.targets[g_renderer.swapchain.index].texture.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_renderer.dimensions.x, g_renderer.dimensions.y, GL_RGBA, GL_UNSIGNED_BYTE, g_renderer.swapchain.reference);
    glBindTexture(GL_TEXTURE_2D, 0);

    // end profiling
    EndProfile(&(g_renderer.stats.profile));
}

void Draw(float x, float y, float w, float h) {
	size_t index = (g_renderer.swapchain.index + 1) % CPUSWAP_LENGTH;
    DrawTexturePro(
        g_renderer.swapchain.targets[index].texture,
        (Rectangle){
            (g_renderer.swapchain.targets[index].texture.width / 2.0f) - (w/2.0f),
            (g_renderer.swapchain.targets[index].texture.height / 2.0f) - (h/2.0f),
            w,
            h },
        (Rectangle){ x, y, w, h},
        (Vector2){ 0, 0 },
        0.0f,
        WHITE);
}

float RenderTime() {
    return ProfileResult(&(g_renderer.stats.profile));
}
