#ifndef RENDERER_H
#define RENDERER_H

#include "data/config.h"
#include <vulkan/vulkan.h>
#include <easyobjects.h>

typedef const char* StaticString;

DECLARE_ARRLIST(StaticString);

typedef struct {
    uint32_t value;
    BOOL exists;
} Schrodingnum;

typedef struct {
    Schrodingnum graphics;
} VulkanFamilyGroup;

typedef struct {
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    VkPhysicalDevice gpu;
    VkDevice interface;
    VkImage image;
    VkImageView view;
    VkQueue graphics_queue;
    ARRLIST_StaticString validation_layers;
    ARRLIST_StaticString required_extensions;
    ARRLIST_StaticString device_extensions;
} VulkanData;

typedef struct {
    VulkanData vulkan;
} Renderer;

void InitializeRenderer();

void DestroyRenderer();

#endif
