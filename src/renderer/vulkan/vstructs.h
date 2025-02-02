#ifndef VSTRUCTS_H
#define VSTRUCTS_H

#include "renderer/rstructs.h"
#include "data/profile.h"
#include <vulkan/vulkan.h>

#define CPUSWAP_LENGTH 2
#define IMAGE_FORMAT VK_FORMAT_R8G8B8A8_SRGB
#define MAX_TEXTURES 32 // update in fragment shader too if changing this

#ifdef PROD_BUILD
    #define ENABLE_VK_VALIDATION_LAYERS FALSE
#else
    #define ENABLE_VK_VALIDATION_LAYERS TRUE
#endif

/* Overall TODO:
 * - Condense all buffers into one and use offsets to increase cache performance
*/

DECLARE_QUAD(VkVertexInputAttributeDescription);

typedef const char* StaticString;
DECLARE_ARRLIST(StaticString);

typedef uint32_t Index; // TODO: may need to make this bigger if we run out of indices
#define INDEX_VK_TYPE VK_INDEX_TYPE_UINT32
DECLARE_ARRLIST(Index);

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
    const char* filepath;
    size_t width;
    size_t height;
    uint32_t mip_levels;
    VulkanImage image;
    VkSampler sampler;
    TextureID id;
} VulkanTexture;
DECLARE_ARRLIST(VulkanTexture);

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
	mat4 model;
	mat4 view;
	mat4 projection;
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
    size_t max_indices;
    size_t max_vertices;
    BOOL update_indices;
    BOOL update_vertices;
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
    VulkanImage target;
    VulkanImage depth;
    VulkanImage color;
} VulkanAttachments;

typedef struct {
    VulkanSyncro syncro;
    VulkanCommands commands;
    VkQueue queue;
} VulkanScheduler;

typedef struct {
    VulkanDescriptors descriptors;
    UBOArray ubos;
} VulkanRenderData;

typedef struct {
    VulkanDataBuffer ssbos[CPUSWAP_LENGTH];
    VulkanImage targets[CPUSWAP_LENGTH];
    VulkanDescriptors descriptors;
} VulkanRaytracer;

typedef struct {
    VulkanPipeline pipeline;
    VkRenderPass renderpass;
    VulkanRenderData renderdata;
    VkFramebuffer framebuffer;
    VkSampleCountFlagBits samples;
    VulkanAttachments attachments;
    VulkanRaytracer raytracer;
} VulkanRenderContext;

typedef struct {
    VulkanGeneral general;
    VulkanRenderContext context;
    VulkanDataBuffer bridge;
    VulkanScheduler scheduler;
} VulkanCore;

typedef struct {
    VulkanDataBuffer indices;
    VulkanDataBuffer vertices;
    ARRLIST_VulkanTexture textures;
    ARRLIST_TriangleID triangles;
} VulkanGeometry;

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
    VulkanGeometry geometry;
} VulkanObject;

typedef struct {
    Profiler profile;
} RendererStats;

typedef struct {
    ARRLIST_Vertex vertices;
    ARRLIST_Index indices;
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