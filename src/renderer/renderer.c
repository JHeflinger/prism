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
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = 800;
    imageInfo.extent.height = 600;
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
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(g_renderer.vulkan.interface, g_renderer.vulkan.image, &memRequirements);
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = 0;
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(g_renderer.vulkan.gpu, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            allocInfo.memoryTypeIndex = i;
            break;
        }
    }
    result = vkAllocateMemory(g_renderer.vulkan.interface, &allocInfo, NULL, &g_renderer.vulkan.memory);
    LOG_ASSERT(result == VK_SUCCESS, "Unable to allocate image memory");
    vkBindImageMemory(g_renderer.vulkan.interface, g_renderer.vulkan.image, g_renderer.vulkan.memory, 0);

    // create image view
    VkImageViewCreateInfo viewInfo = {};
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

	FreeFile(vertshadercode);
	FreeFile(fragshadercode);
	vkDestroyShaderModule(g_renderer.vulkan.interface, vertshader, NULL);
	vkDestroyShaderModule(g_renderer.vulkan.interface, fragshader, NULL);
}

void DestroyVulkan() {
    // destroy image memory
    vkFreeMemory(g_renderer.vulkan.interface, g_renderer.vulkan.memory, NULL);

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

void InitializeRenderer() {
    InitializeVulkanData();
    CreateVulkanInstance();
    SetupVulkanMessenger();
    PickGPU();
    CreateDeviceInterface();
    CreateImage();
    CreatePipeline();
}

void DestroyRenderer() {
    DestroyVulkan();
}
