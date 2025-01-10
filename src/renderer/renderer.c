#include "renderer.h"
#include "core/log.h"
#include "data/config.h"
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
        LOG_WARN("%s[VULKAN]%s [%s] %s",
            LOG_YELLOW,
            LOG_RESET,
            (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT ? "GENERAL" :
                (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ? "VALIDATION" : "PERFORMANCE")),
            pCallbackData->pMessage);
    } else if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOG_FATAL("%s[VULKAN]%s [%s] %s",
            LOG_RED,
            LOG_RESET,
            (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT ? "GENERAL" :
                (messageType == VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT ? "VALIDATION" : "PERFORMANCE")),
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
    if (ENABLE_VK_VALIDATION_LAYERS) {
        createInfo.enabledLayerCount = g_renderer.vulkan.validation_layers.size;
        createInfo.ppEnabledLayerNames = g_renderer.vulkan.validation_layers.data;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    // create instance
    VkResult result = vkCreateInstance(&createInfo, NULL, &(g_renderer.vulkan.instance));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create vulkan instance");
}

void DestroyVulkan() {
    // destroy vulkan instance
    vkDestroyInstance(g_renderer.vulkan.instance, NULL);

    // clean required extensions
    ARRLIST_StaticString_clear(&(g_renderer.vulkan.required_extensions));
    ARRLIST_StaticString_clear(&(g_renderer.vulkan.validation_layers));
}

void InitializeRenderer() {
    InitializeVulkanData();
    CreateVulkanInstance();
}

void DestroyRenderer() {
    DestroyVulkan();
}