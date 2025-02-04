#ifndef VSTRUCTS_H
#define VSTRUCTS_H

#include "renderer/rstructs.h"
#include "renderer/vulkan/vconfig.h"
#include "data/profile.h"
#include <vulkan/vulkan.h>

typedef const char* StaticString;
DECLARE_ARRLIST(StaticString);

typedef struct {
    uint32_t x;
    uint32_t y;
} RayGenerator;

typedef struct {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
} VulkanImage;

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
    alignas(16) vec3 look;
    alignas(16) vec3 position;
    alignas(16) vec3 up;
    alignas(16) vec3 u;
    alignas(16) vec3 v;
    alignas(16) vec3 w;
    alignas(16) vec3 camconf;
    alignas(16) vec3 sizes;
} UniformBufferObject;

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
} VulkanDataBuffer;

typedef struct {
    VulkanDataBuffer objects[CPUSWAP_LENGTH];
    void* mapped[CPUSWAP_LENGTH];
} UBOArray;

typedef struct {
    size_t max_triangles;
    BOOL update_triangles;
} ChangeSet;

typedef struct {
    VkDebugUtilsMessengerEXT messenger;
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkDevice interface;
} VulkanGeneral;

typedef struct {
    VkPipeline pipeline;
    VkPipelineLayout layout;
} VulkanPipeline;

typedef struct {
    VkCommandPool pool;
    VkCommandBuffer commands[CPUSWAP_LENGTH];
} VulkanCommands;

typedef struct {
    VkDescriptorPool pool;
    VkDescriptorSet sets[CPUSWAP_LENGTH];
    VkDescriptorSetLayout layout;
} VulkanDescriptors;

typedef struct {
    VulkanSyncro syncro;
    VulkanCommands commands;
    VkQueue queue;
} VulkanScheduler;

typedef struct {
    VulkanDescriptors descriptors;
    UBOArray ubos;
    VulkanDataBuffer ssbos[CPUSWAP_LENGTH];
} VulkanRenderData;

typedef struct {
    VulkanPipeline pipeline;
    VulkanRenderData renderdata;
    VulkanImage targets[CPUSWAP_LENGTH];
} VulkanRenderContext;

typedef struct {
    VulkanDataBuffer triangles;
} VulkanGeometry;

typedef struct {
    VulkanGeneral general;
    VulkanGeometry geometry;
    VulkanRenderContext context;
    VulkanDataBuffer bridge;
    VulkanScheduler scheduler;
} VulkanCore;

typedef struct {
    ARRLIST_StaticString required;
    ARRLIST_StaticString device;
} VulkanExtensionData;

typedef struct {
    VulkanExtensionData extensions;
    ARRLIST_StaticString validation;
} VulkanMetadata;

typedef struct {
    VulkanCore core;
    VulkanMetadata metadata;
} VulkanObject;

typedef struct {
    Profiler profile;
} RendererStats;

typedef struct {
    ARRLIST_Triangle triangles;
    ARRLIST_TriangleID ids;
    ChangeSet changes;
} Geometry;

typedef struct {
    RendererStats stats;
    VulkanObject vulkan;
    CPUSwap swapchain;
    Vector2 dimensions;
    Geometry geometry;
    SimpleCamera camera;
} Renderer;

#endif
