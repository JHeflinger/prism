#include "renderer.h"
#include "core/log.h"
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
            group.graphicsFamily = (Schrodingnum){ i, TRUE };
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
        uint32_t curr_score = 0;
        VkPhysicalDeviceProperties deviceProperties;
        VkPhysicalDeviceFeatures deviceFeatures;
        VulkanFamilyGroup families = FindQueueFamilies(devices[i]);
        if (!families.graphicsFamily.exists) continue;
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

void DestroyVulkan() {
    // destroy debug messenger
    if (ENABLE_VK_VALIDATION_LAYERS) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g_renderer.vulkan.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_messenger != NULL)
            destroy_messenger(g_renderer.vulkan.instance, g_renderer.vulkan.messenger, NULL);
    }

    // destroy vulkan instance
    vkDestroyInstance(g_renderer.vulkan.instance, NULL);

    // clean required extensions
    ARRLIST_StaticString_clear(&(g_renderer.vulkan.required_extensions));
    ARRLIST_StaticString_clear(&(g_renderer.vulkan.validation_layers));
}

void InitializeRenderer() {
    InitializeVulkanData();
    CreateVulkanInstance();
    SetupVulkanMessenger();
    PickGPU();
}

void DestroyRenderer() {
    DestroyVulkan();
}