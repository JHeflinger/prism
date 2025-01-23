#include "renderer.h"
#include "core/log.h"
#include "core/file.h"
#include <GLFW/glfw3.h>
#include <easymemory.h>
#include <string.h>

Renderer g_renderer;

#ifdef PROD_BUILD
    #define ENABLE_VK_VALIDATION_LAYERS FALSE
#else
    #define ENABLE_VK_VALIDATION_LAYERS TRUE
#endif

#define TEMP_W 800
#define TEMP_H 600

IMPL_ARRLIST(StaticString);

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

void InitializeVulkanData() {
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
        if (curr_score > score) {
            score = curr_score;
            ind = i;
        }
    }
    LOG_ASSERT(score != 0, "A suitable GPU could not be found");
    g_renderer.vulkan.gpu = devices[ind];
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

void CreateImage() {
    // create image
    VkImageCreateInfo imageInfo = { 0 };
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = TEMP_W;
    imageInfo.extent.height = TEMP_H;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateImage(g_renderer.vulkan.interface, &imageInfo, NULL, &(g_renderer.vulkan.image));
    LOG_ASSERT(result == VK_SUCCESS, "Unable to create vulkan image");

    // allocate memory for the image
    VkMemoryRequirements imageMemRequirements = { 0 };
    vkGetImageMemoryRequirements(g_renderer.vulkan.interface, g_renderer.vulkan.image, &imageMemRequirements);
    VkMemoryAllocateInfo imageAllocInfo = { 0 };
    imageAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAllocInfo.allocationSize = imageMemRequirements.size;
    imageAllocInfo.memoryTypeIndex = 0;
    VkPhysicalDeviceMemoryProperties imageMemProperties = { 0 };
    vkGetPhysicalDeviceMemoryProperties(g_renderer.vulkan.gpu, &imageMemProperties);
    for (uint32_t i = 0; i < imageMemProperties.memoryTypeCount; i++) {
        if ((imageMemRequirements.memoryTypeBits & (1 << i)) &&
            (imageMemProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            (imageMemProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            imageAllocInfo.memoryTypeIndex = i;
            break;
        }
    }
    result = vkAllocateMemory(g_renderer.vulkan.interface, &imageAllocInfo, NULL, &g_renderer.vulkan.image_memory);
    LOG_ASSERT(result == VK_SUCCESS, "Unable to allocate image memory");
    result = vkBindImageMemory(g_renderer.vulkan.interface, g_renderer.vulkan.image, g_renderer.vulkan.image_memory, 0);
    LOG_ASSERT(result == VK_SUCCESS, "Unable to bind image memory");

    // create image view
    VkImageViewCreateInfo viewInfo = { 0 };
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = g_renderer.vulkan.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    result = vkCreateImageView(g_renderer.vulkan.interface, &viewInfo, NULL, &(g_renderer.vulkan.view));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create image view");

    // create buffer
    VkBufferCreateInfo bufferInfo = { 0 };
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = TEMP_W * TEMP_H * 4; // Assuming VK_FORMAT_B8G8R8A8_SRGB
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    result = vkCreateBuffer(g_renderer.vulkan.interface, &bufferInfo, NULL, &(g_renderer.vulkan.buffer));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create staging buffer");

    // allocate memory for buffer
    VkMemoryRequirements bufferMemRequirements = { 0 };
    vkGetBufferMemoryRequirements(g_renderer.vulkan.interface, g_renderer.vulkan.buffer, &bufferMemRequirements);
    VkMemoryAllocateInfo bufferAllocInfo = { 0 };
    bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufferAllocInfo.allocationSize = bufferMemRequirements.size;
    bufferAllocInfo.memoryTypeIndex = 0;
    VkPhysicalDeviceMemoryProperties bufferMemProperties = { 0 };
    vkGetPhysicalDeviceMemoryProperties(g_renderer.vulkan.gpu, &bufferMemProperties);
    for (uint32_t i = 0; i < bufferMemProperties.memoryTypeCount; i++) {
        if ((bufferMemRequirements.memoryTypeBits & (1 << i)) &&
            (bufferMemProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            (bufferMemProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            bufferAllocInfo.memoryTypeIndex = i;
            break;
        }
    }
    result = vkAllocateMemory(g_renderer.vulkan.interface, &bufferAllocInfo, NULL, &(g_renderer.vulkan.buffer_memory));
    LOG_ASSERT(result == VK_SUCCESS, "Unable to allocate buffer memory");
    result = vkBindBufferMemory(g_renderer.vulkan.interface, g_renderer.vulkan.buffer, g_renderer.vulkan.buffer_memory, 0);
    LOG_ASSERT(result == VK_SUCCESS, "Unable to bind buffer memory");
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

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { 0 };
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = NULL; // Optional
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = NULL; // Optional

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { 0 };
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // VkViewport viewport = { 0 };
    // viewport.x = 0.0f;
    // viewport.y = 0.0f;
    // viewport.width = TEMP_W;
    // viewport.height = TEMP_H;
    // viewport.minDepth = 0.0f;
    // viewport.maxDepth = 1.0f;

    // VkRect2D scissor = { 0 };

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
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f; // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

    VkPipelineMultisampleStateCreateInfo multisampling = { 0 };
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f; // Optional
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
    pipelineLayoutInfo.setLayoutCount = 0; // Optional
    pipelineLayoutInfo.pSetLayouts = NULL; // Optional
    pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
    pipelineLayoutInfo.pPushConstantRanges = NULL; // Optional

    VkResult result = vkCreatePipelineLayout(g_renderer.vulkan.interface, &pipelineLayoutInfo, NULL, &(g_renderer.vulkan.pipeline_layout));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create pipeline layout");

    VkGraphicsPipelineCreateInfo pipelineInfo = { 0 };
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderstages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = NULL; // Optional
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
    colorAttachment.format = VK_FORMAT_B8G8R8A8_SRGB;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorAttachmentRef = { 0 };
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = { 0 };
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency = { 0 };
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = { 0 };
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(g_renderer.vulkan.interface, &renderPassInfo, NULL, &(g_renderer.vulkan.render_pass));
    LOG_ASSERT(result == VK_SUCCESS, "Unable to create render pass");
}

void CreateFramebuffer() {
    VkImageView attachments[] = { g_renderer.vulkan.view };
    VkFramebufferCreateInfo framebufferInfo = { 0 };
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = g_renderer.vulkan.render_pass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = TEMP_W;
    framebufferInfo.height = TEMP_H;
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

void CreateCommandBuffer() {
    VkCommandBufferAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = g_renderer.vulkan.command_pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkResult result = vkAllocateCommandBuffers(g_renderer.vulkan.interface, &allocInfo, &(g_renderer.vulkan.command));
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
        renderPassInfo.renderArea.extent = (VkExtent2D){ TEMP_W, TEMP_H };

        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(command, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, g_renderer.vulkan.pipeline);

        VkViewport viewport = { 0 };
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)TEMP_W;
        viewport.height = (float)TEMP_H;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(command, 0, 1, &viewport);

        VkRect2D scissor = { 0 };
        scissor.offset = (VkOffset2D){ 0, 0 };
        scissor.extent = (VkExtent2D){ TEMP_W, TEMP_H };
        vkCmdSetScissor(command, 0, 1, &scissor);

        vkCmdDraw(command, 3, 1, 0, 0);

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
        region.imageExtent = (VkExtent3D){ TEMP_W, TEMP_H, 1 };
        vkCmdCopyImageToBuffer(g_renderer.vulkan.command, g_renderer.vulkan.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_renderer.vulkan.buffer, 1, &region);
    }

    // End command
    result = vkEndCommandBuffer(command);
    LOG_ASSERT(result == VK_SUCCESS, "Failed to record command buffer!");
}

void CreateSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo = { 0 };
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo = { 0 };
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    //fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkResult result = vkCreateSemaphore(g_renderer.vulkan.interface, &semaphoreInfo, NULL, &(g_renderer.vulkan.syncro.image_available));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create semaphore");
    result = vkCreateSemaphore(g_renderer.vulkan.interface, &semaphoreInfo, NULL, &(g_renderer.vulkan.syncro.render_finished));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create semaphore");
    result = vkCreateFence(g_renderer.vulkan.interface, &fenceInfo, NULL, &(g_renderer.vulkan.syncro.in_flight));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create fence");
}

void DestroyVulkan() {
    // wait for device to finish
    vkDeviceWaitIdle(g_renderer.vulkan.interface);

    // destroy syncro objects
    vkDestroySemaphore(g_renderer.vulkan.interface, g_renderer.vulkan.syncro.image_available, NULL);
    vkDestroySemaphore(g_renderer.vulkan.interface, g_renderer.vulkan.syncro.render_finished, NULL);
    vkDestroyFence(g_renderer.vulkan.interface, g_renderer.vulkan.syncro.in_flight, NULL);

    // destroy command pool
    vkDestroyCommandPool(g_renderer.vulkan.interface, g_renderer.vulkan.command_pool, NULL);

    // destroy framebuffer
    vkDestroyFramebuffer(g_renderer.vulkan.interface, g_renderer.vulkan.framebuffer, NULL);

    // destroy pipeline
    vkDestroyPipeline(g_renderer.vulkan.interface, g_renderer.vulkan.pipeline, NULL);
    vkDestroyPipelineLayout(g_renderer.vulkan.interface, g_renderer.vulkan.pipeline_layout, NULL);
    vkDestroyRenderPass(g_renderer.vulkan.interface, g_renderer.vulkan.render_pass, NULL);

    // destroy buffer memory
    vkFreeMemory(g_renderer.vulkan.interface, g_renderer.vulkan.buffer_memory, NULL);

    // destroy buffer
    vkDestroyBuffer(g_renderer.vulkan.interface, g_renderer.vulkan.buffer, NULL);

    // destroy image memory
    vkFreeMemory(g_renderer.vulkan.interface, g_renderer.vulkan.image_memory, NULL);

    // destroy image view
    vkDestroyImageView(g_renderer.vulkan.interface, g_renderer.vulkan.view, NULL);

    // destroy image
    vkDestroyImage(g_renderer.vulkan.interface, g_renderer.vulkan.image, NULL);

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
    InitializeVulkanData();
    CreateVulkanInstance();
    SetupVulkanMessenger();
    PickGPU();
    CreateDeviceInterface();
    CreateImage();
    CreateRenderPass();
    CreatePipeline();
    CreateFramebuffer();
    CreateCommandPool();
    CreateCommandBuffer();
    CreateSyncObjects();
}

void InitializeRenderer() {
    InitializeVulkan();
    g_renderer.target = LoadRenderTexture(TEMP_W, TEMP_H);
    LOG_ASSERT(IsRenderTextureValid(g_renderer.target), "Unable to load target texture");
}

void DestroyRenderer() {
    DestroyVulkan();
    UnloadRenderTexture(g_renderer.target);
}

void Render() {
    // reset command buffer and record it
    vkResetCommandBuffer(g_renderer.vulkan.command, 0);
    RecordCommand(g_renderer.vulkan.command);

    // submit command buffer
    VkSubmitInfo submitInfo = { 0 };
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    //VkSemaphore waitSemaphores[] = { g_renderer.vulkan.syncro.image_available };
    //VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 0;//1;
    //submitInfo.pWaitSemaphores = waitSemaphores;
    //submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &(g_renderer.vulkan.command);
    //VkSemaphore signalSemaphores[] = { g_renderer.vulkan.syncro.render_finished };
    submitInfo.signalSemaphoreCount = 0;//1;
    //submitInfo.pSignalSemaphores = signalSemaphores;
    VkResult result = vkQueueSubmit(g_renderer.vulkan.graphics_queue, 1, &submitInfo, g_renderer.vulkan.syncro.in_flight);
    LOG_ASSERT(result == VK_SUCCESS, "failed to submit draw command buffer!");

    // wait for and reset rendering fence
    vkWaitForFences(g_renderer.vulkan.interface, 1, &(g_renderer.vulkan.syncro.in_flight), VK_TRUE, UINT64_MAX);
    vkResetFences(g_renderer.vulkan.interface, 1, &(g_renderer.vulkan.syncro.in_flight));

    // update render target
    void* data;
    vkMapMemory(g_renderer.vulkan.interface, g_renderer.vulkan.buffer_memory, 0, VK_WHOLE_SIZE, 0, &data);
    glBindTexture(GL_TEXTURE_2D, g_renderer.target.texture.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TEMP_W, TEMP_H, GL_RGBA, GL_UNSIGNED_BYTE, data);
    vkUnmapMemory(g_renderer.vulkan.interface, g_renderer.vulkan.buffer_memory);
}

void Draw(float x, float y) {
    DrawTexture(g_renderer.target.texture, x, y, WHITE);
}