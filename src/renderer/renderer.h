#ifndef RENDERER_H
#define RENDERER_H

#include "data/config.h"
#include "data/profile.h"
#include <vulkan/vulkan.h>
#include <easyobjects.h>
#include <raylib.h>
#include <cglm/cglm.h>

#define CPUSWAP_LENGTH 2

typedef const char* StaticString;

typedef struct {
    uint32_t value;
    BOOL exists;
} Schrodingnum;

typedef struct {
    Schrodingnum graphics;
} VulkanFamilyGroup;

typedef struct {
    VkFence fences[CPUSWAP_LENGTH];
} VulkanSyncro;

typedef struct {
	RenderTexture2D targets[CPUSWAP_LENGTH];
	size_t index;
    void* reference;
} CPUSwap;

typedef struct {
    vec2 position;
    vec3 color;
} Vertex;

DECLARE_ARRLIST(StaticString);
DECLARE_ARRLIST(Vertex);
DECLARE_PAIR(VkVertexInputAttributeDescription);

typedef struct {
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    VkPhysicalDevice gpu;
    VkDevice interface;
    VkImage image;
    VkImageView view;
    VkDeviceMemory image_memory;
    VkDeviceMemory cross_memory;
    VkDeviceMemory vertex_memory;
    VkRenderPass render_pass;
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    VkQueue graphics_queue;
    VkFramebuffer framebuffer;
    VkCommandPool command_pool;
    VkCommandBuffer commands[CPUSWAP_LENGTH];
    VulkanSyncro syncro;
    VkBuffer cross_buffer;
    VkBuffer vertex_buffer;
    ARRLIST_StaticString validation_layers;
    ARRLIST_StaticString required_extensions;
    ARRLIST_StaticString device_extensions;
} VulkanData;

typedef struct {
    Profiler profile;
} RendererStats;

typedef struct {
    RendererStats stats;
    VulkanData vulkan;
    CPUSwap swapchain;
    Vector2 dimensions;
    ARRLIST_Vertex vertices;
} Renderer;

void InitializeRenderer();

void DestroyRenderer();

void Render();

void Draw(float x, float y, float w, float h);

float RenderTime();

#endif
