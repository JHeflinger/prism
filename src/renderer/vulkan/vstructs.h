#ifndef VSTRUCTS_H
#define VSTRUCTS_H

#include "renderer/rstructs.h"

typedef struct {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
} VulkanImage;

typedef struct {
    Schrodingnum graphics;
} VulkanFamilyGroup;

typedef struct {
    VkFence fences[CPUSWAP_LENGTH];
} VulkanSyncro;

typedef struct {
    alignas(16) vec3 look;
    alignas(16) vec3 position;
    alignas(16) vec3 up;
    alignas(16) vec3 u;
    alignas(16) vec3 v;
    alignas(16) vec3 w;
    alignas(8) vec2 viewport;
    alignas(4) uint32_t triangles;
    alignas(4) uint32_t emissives;
    alignas(4) uint32_t lights;
    alignas(4) uint32_t samples;
	alignas(4) uint32_t seed;
    alignas(4) uint32_t grid;
    alignas(4) uint32_t reset;
    alignas(4) uint32_t direct;
    alignas(4) uint32_t directonly;
    alignas(4) uint32_t showdof;
    alignas(4) uint32_t normals;
    alignas(4) uint32_t scenelighting;
    alignas(4) uint32_t scenelightingonly;
    alignas(4) float fov;
    alignas(4) float width;
    alignas(4) float height;
    alignas(4) float swap;
	alignas(4) float frametime;
    alignas(4) float whitepoint;
    alignas(4) float gamma;
    alignas(4) float lightarea;
    alignas(4) float aperature;
    alignas(4) float focus;
} UniformBufferObject;

typedef struct {
    alignas(16) vec3 minBB;
    alignas(16) vec3 maxBB;
} GeometryUniformBufferObject;

typedef struct {
    alignas(4) uint32_t mouse_x;
    alignas(4) uint32_t mouse_y;
	alignas(4) uint32_t image_width;
	alignas(4) uint32_t image_height;
	alignas(4) uint32_t single_selected_tid;
	alignas(4) uint32_t single_selected_vid;
	alignas(4) uint32_t divisor;
    alignas(4) uint32_t mode;
} OverlayUniformBufferObject;

typedef struct {
	alignas(4) TriangleID hovered_tid;
    alignas(8) vec2 vertex_position;
    alignas(8) vec2 selected_vertex_position;
} OverlaySSBO;

typedef struct {
    alignas(4) uint32_t elements;
    alignas(4) uint32_t bitstart;
} VulkanPushConstants;

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
} VulkanDataBuffer;

typedef struct {
    VulkanDataBuffer objects[CPUSWAP_LENGTH];
    VulkanDataBuffer overlay_objects[CPUSWAP_LENGTH];
    VulkanDataBuffer geometry_objects[CPUSWAP_LENGTH];
    void* mapped[CPUSWAP_LENGTH];
    void* overlay_mapped[CPUSWAP_LENGTH];
    void* geometry_mapped[CPUSWAP_LENGTH];
} UBOArray;

typedef struct {
    VkDebugUtilsMessengerEXT messenger;
    VkInstance instance;
    VkPhysicalDevice gpu;
    VkDevice interface;
	char gpuname[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
} VulkanGeneral;

typedef struct {
    VkPipeline* pipeline;
    VkPipelineLayout* layout;
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
    VulkanDescriptors* descriptors;
    UBOArray ubos;
    VulkanDataBuffer ssbos[CPUSWAP_LENGTH];
	VulkanDataBuffer overlay_ssbos[CPUSWAP_LENGTH];
    VulkanDataBuffer overlay_bridge;
    void* overlay_mapped;
} VulkanRenderData;

typedef struct {
    VulkanPipeline pipeline;
    VulkanRenderData renderdata;
    VulkanImage hdr[CPUSWAP_LENGTH];
    VulkanImage targets[CPUSWAP_LENGTH];
} VulkanRenderContext;

typedef struct {
    VulkanDataBuffer mortons;
    VulkanDataBuffer workhistory;
    VulkanDataBuffer workoffsets;
    VulkanDataBuffer indices;
    VulkanDataBuffer mortonswap;
    VulkanDataBuffer indexswap;
    VulkanDataBuffer boundingboxes;
    VulkanDataBuffer nodes;
    VulkanDataBuffer buckets;
} VulkanBVH;

typedef struct {
    VulkanBVH bvh;
    VulkanDataBuffer normals;
    VulkanDataBuffer vertices;
    VulkanDataBuffer triangles;
    VulkanDataBuffer emissives;
    VulkanDataBuffer materials;
    VulkanDataBuffer lights;
} VulkanGeometry;

typedef struct {
    VulkanImage image;
} VulkanTarget;

typedef enum {
	UNIFORM_BUFFER = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	STORAGE_BUFFER = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	STORAGE_IMAGE = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
} VulkanVariableType;

typedef struct {
	BOOL reference;
	void* value;
} SchrodingRef;

typedef struct {
	SchrodingRef count;
    float reduction;
	size_t size;
} SchrodingSize;

typedef struct {
	VulkanVariableType type;
	SchrodingRef data;
	SchrodingSize size;
} VulkanBoundVariable;

DECLARE_ARRLIST(VulkanBoundVariable);

typedef struct {
	const char* filename;
	ARRLIST_VulkanBoundVariable variables[CPUSWAP_LENGTH];
} VulkanShader;

DECLARE_ARRLIST_NAMED(VulkanShaderPtr, VulkanShader*);

typedef struct {
    VulkanGeneral general;
	ARRLIST_VulkanShaderPtr shaders;
    VulkanGeometry geometry;
    VulkanRenderContext context;
    VulkanDataBuffer bridge;
    VulkanScheduler scheduler;
    VulkanTarget target;
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
    RendererStats stats;
    VulkanObject vulkan;
    CPUSwap swapchain;
    Vector2 dimensions;
    Geometry geometry;
    SimpleCamera camera;
    Vector2 viewport;
    RendererConfig config;
} Renderer;

#endif
