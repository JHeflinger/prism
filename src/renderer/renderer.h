#ifndef RENDERER_H
#define RENDERER_H

#include "data/config.h"
#include "data/profile.h"
#include <vulkan/vulkan.h>
#include <easyobjects.h>
#include <raylib.h>
#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

#define CPUSWAP_LENGTH 2
#define MAX_TEXTURES 32 // update in fragment shader too if changing this

/* Overall TODO:
 * - Condense all buffers into one and use offsets to increase cache performance
*/

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

typedef uint32_t TextureID; // this will need to be increased given

typedef struct {
    vec3 position;
    vec3 color;
    vec2 texcoord;
    TextureID texid;
} Vertex;

typedef struct {
	mat4 model;
	mat4 view;
	mat4 projection;
} UniformBufferObject;

typedef struct {
    VkBuffer buffers[CPUSWAP_LENGTH];
    VkDeviceMemory memory[CPUSWAP_LENGTH];
    void* mapped[CPUSWAP_LENGTH];
} UBOArray;

typedef struct {
    const char* filepath;
    size_t width;
    size_t height;
    uint32_t mip_levels;
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    VkSampler sampler;
    TextureID id;
} VulkanTexture;

typedef uint64_t TriangleID;

typedef struct {
    Vertex vertices[3];
} Triangle;

typedef struct {
    Vector3 position;
    Vector3 look;
    Vector3 up;
} SimpleCamera;

typedef struct {
    size_t max_indices;
    size_t max_vertices;
    BOOL update_indices;
    BOOL update_vertices;
} ChangeSet;

typedef uint32_t Index; // TODO: may need to make this bigger if we run out of indices
#define INDEX_VK_TYPE VK_INDEX_TYPE_UINT32

DECLARE_ARRLIST(StaticString);
DECLARE_ARRLIST(Vertex);
DECLARE_ARRLIST(Index);
DECLARE_ARRLIST(VulkanTexture);
DECLARE_ARRLIST(TriangleID);
DECLARE_QUAD(VkVertexInputAttributeDescription);

typedef struct {
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    VkPhysicalDevice gpu;
    VkDevice interface;
    VkImage image;
    VkImageView view;
    VkDeviceMemory image_memory;
    VkImage depth_image;
    VkImageView depth_image_view;
    VkDeviceMemory depth_image_memory;
    VkImage color_image;
    VkImageView color_image_view;
    VkDeviceMemory color_image_memory;
    VkDeviceMemory cross_memory;
    VkDeviceMemory vertex_memory;
    VkDeviceMemory index_memory;
    VkRenderPass render_pass;
    VkPipeline pipeline;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_sets[CPUSWAP_LENGTH];
    VkDescriptorSetLayout descriptor_set_layout;
    VkPipelineLayout pipeline_layout;
    VkQueue graphics_queue;
    VkFramebuffer framebuffer;
    VkCommandPool command_pool;
    VkCommandBuffer commands[CPUSWAP_LENGTH];
    VulkanSyncro syncro;
    VkBuffer cross_buffer;
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    UBOArray uniform_buffers;
    VkSampleCountFlagBits msaa_samples;
    ARRLIST_VulkanTexture textures;
    ARRLIST_TriangleID triangle_refs;
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
    ARRLIST_Index indices;
    ChangeSet changes;
    SimpleCamera camera;
} Renderer;

void InitializeRenderer();

void DestroyRenderer();

SimpleCamera GetCamera();

void MoveCamera(SimpleCamera camera);

TriangleID SubmitTriangle(Triangle triangle);

void RemoveTriangle(TriangleID id);

void ClearTriangles();

TextureID SubmitTexture(const char* filepath);

void RemoveTexture(TextureID id);

void ClearTextures();

void Render();

void Draw(float x, float y, float w, float h);

float RenderTime();

#endif