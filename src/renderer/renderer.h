#ifndef RENDERER_H
#define RENDERER_H

#include <vulkan/vulkan.h>
#include <easyobjects.h>

typedef const char* StaticString;

DECLARE_ARRLIST(StaticString);

typedef struct {
    VkInstance instance;
    ARRLIST_StaticString validation_layers;
    ARRLIST_StaticString required_extensions;
} VulkanData;

typedef struct {
    VulkanData vulkan;
} Renderer;

void InitializeRenderer();

void DestroyRenderer();

#endif