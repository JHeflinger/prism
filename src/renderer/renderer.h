#ifndef RENDERER_H
#define RENDERER_H

#include <vulkan/vulkan.h>

typedef struct {
    VkInstance instance;
} VulkanRenderer;

void InitializeRenderer();

void DestroyRenderer();

#endif