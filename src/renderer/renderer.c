#include "renderer.h"
#include "core/log.h"
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/vulkan/vupdate.h"
#include "renderer/vulkan/vclean.h"
#include <GLFW/glfw3.h>
#include <easymemory.h>
#include <string.h>
#include <stb_image.h>
#include <math.h>

Renderer g_renderer = { 0 };
TextureID g_texture_id = 0;
TriangleID g_triangle_id = 0;

void InitializeRenderer() {
    // initialize camera
    g_renderer.camera.position = (Vector3){ 2.0f, 2.0f, 2.0f };
    g_renderer.camera.look = (Vector3){ 0.0f, 0.0f, 0.0f };
    g_renderer.camera.up = (Vector3){ 0.0f, 0.0f, 1.0f };

    // set up dimensions
    g_renderer.dimensions = (Vector2){ GetScreenWidth(), GetScreenHeight() };

    // initialize vulkan resources
	VUTIL_SetVulkanUtilsContext(&g_renderer);
	VINIT_SetVulkanInitContext(&g_renderer);
	VUPDT_SetVulkanUpdateContext(&g_renderer);
	BOOL result = VINIT_Vulkan(&(g_renderer.vulkan));
	LOG_ASSERT(result, "Failed to initialize vulkan");

    // default texture
    SubmitTexture("assets/images/default.png");

    // set up cpu swap
	for (int i = 0; i < CPUSWAP_LENGTH; i++) {
		g_renderer.swapchain.targets[i] = LoadRenderTexture(g_renderer.dimensions.x, g_renderer.dimensions.y);
		LOG_ASSERT(IsRenderTextureValid(g_renderer.swapchain.targets[i]), "Unable to load target texture");
	}

    // configure stat profiler
    ConfigureProfile(&(g_renderer.stats.profile), "Renderer", 100);
}

void DestroyRenderer() {
    // destroy textures
    ClearTextures();

    // clean geometry
    ClearTriangles();

    // destroy vulkan resources
    VCLEAN_Vulkan(&(g_renderer.vulkan));

    // unload cpu swap textures
	for (int i = 0; i < CPUSWAP_LENGTH; i++)
		UnloadRenderTexture(g_renderer.swapchain.targets[i]);
}

SimpleCamera GetCamera() {
    return g_renderer.camera;
}

void MoveCamera(SimpleCamera camera) {
    g_renderer.camera = camera;
}

TriangleID SubmitTriangle(Triangle triangle) {
    ARRLIST_Index_add(&(g_renderer.geometry.indices), g_renderer.geometry.vertices.size);
    ARRLIST_Vertex_add(&(g_renderer.geometry.vertices), triangle.vertices[0]);
    ARRLIST_Index_add(&(g_renderer.geometry.indices), g_renderer.geometry.vertices.size);
    ARRLIST_Vertex_add(&(g_renderer.geometry.vertices), triangle.vertices[1]);
    ARRLIST_Index_add(&(g_renderer.geometry.indices), g_renderer.geometry.vertices.size);
    ARRLIST_Vertex_add(&(g_renderer.geometry.vertices), triangle.vertices[2]);
    g_renderer.geometry.changes.update_indices = TRUE;
    g_renderer.geometry.changes.update_vertices = TRUE;
    ARRLIST_TriangleID_add(&(g_renderer.vulkan.geometry.triangles), g_triangle_id);
    g_triangle_id++;
    return g_triangle_id - 1;
}

void RemoveTriangle(TriangleID id) {
    size_t ind = 0;
    BOOL found = false;
    for (size_t i = 0; i < g_renderer.vulkan.geometry.triangles.size; i++) {
        if (g_renderer.vulkan.geometry.triangles.data[i] == id) {
            ind = i;
            found = TRUE;
            break;
        }
    }
    if (found) {
        for (int i = 3; i > 0; i--) {
            int realind = i - 1;
            ARRLIST_Index_remove(&(g_renderer.geometry.indices), ind + realind);
            ARRLIST_Vertex_remove(&(g_renderer.geometry.vertices), ind + realind);
        }
        ARRLIST_TriangleID_remove(&(g_renderer.vulkan.geometry.triangles), ind);
        g_renderer.geometry.changes.update_indices = TRUE;
        g_renderer.geometry.changes.update_vertices = TRUE;
    } else {
        LOG_FATAL("Unable to remove nonexistant triangle");
    }
}

void ClearTriangles() {
    // wait for device to finish
    vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);

    // clean vertex data
    ARRLIST_Vertex_clear(&(g_renderer.geometry.vertices));

    // clean index data
    ARRLIST_Index_clear(&(g_renderer.geometry.indices));

    // clean triangle ref data
    ARRLIST_TriangleID_clear(&(g_renderer.vulkan.geometry.triangles));

    // destroy vertex buffer
    vkDestroyBuffer(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.vertices.buffer, NULL);

    // free vertex memory
    vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.vertices.memory, NULL);

    // destroy index buffer
    vkDestroyBuffer(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.indices.buffer, NULL);

    // free index memory
    vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.indices.memory, NULL);
}

TextureID SubmitTexture(const char* filepath) {
    LOG_ASSERT(g_renderer.vulkan.geometry.textures.size < MAX_TEXTURES, "No more room for more textures");
    VulkanTexture texture = { 0 };

    // Create texture image
    {
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load(filepath, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        LOG_ASSERT(pixels, "Failed to load texture image");
        texture.filepath = filepath;
        texture.width = texWidth;
        texture.height = texHeight;
        texture.mip_levels = (uint32_t)floor(log2(texWidth > texHeight ? texWidth : texHeight)) + 1;
        VkDeviceSize imageSize = texWidth * texHeight * 4;
        VulkanDataBuffer stagingBuffer;
        VUTIL_CreateBuffer(
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer);
        void* data;
        vkMapMemory(g_renderer.vulkan.core.general.interface, stagingBuffer.memory, 0, imageSize, 0, &data);
        memcpy(data, pixels, (size_t)imageSize);
        vkUnmapMemory(g_renderer.vulkan.core.general.interface, stagingBuffer.memory);
        stbi_image_free(pixels);
        VUTIL_CreateImage(
            texWidth,
            texHeight,
            texture.mip_levels,
            VK_SAMPLE_COUNT_1_BIT,
            IMAGE_FORMAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, 
            &(texture.image));
        VUTIL_TransitionImageLayout(texture.image.image, IMAGE_FORMAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mip_levels);
        VUTIL_CopyBufferToImage(stagingBuffer.buffer, texture.image.image, (uint32_t)texWidth, (uint32_t)texHeight);
        VUTIL_GenerateMipmaps(texture.image.image, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, texture.mip_levels);
        VUTIL_DestroyBuffer(stagingBuffer);
    }

    // Create texture sampler
    {
        VkPhysicalDeviceProperties properties = { 0 };
        vkGetPhysicalDeviceProperties(g_renderer.vulkan.core.general.gpu, &properties);
        VkSamplerCreateInfo samplerInfo = { 0 };
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = (float)texture.mip_levels;
        VkResult result = vkCreateSampler(g_renderer.vulkan.core.general.interface, &samplerInfo, NULL, &(texture.sampler));
        LOG_ASSERT(result == VK_SUCCESS, "Failed to create texture sampler");
    }

    texture.id = g_texture_id;
    g_texture_id++;
    ARRLIST_VulkanTexture_add(&(g_renderer.vulkan.geometry.textures), texture);

    if (g_renderer.vulkan.geometry.textures.size > 1) VUPDT_DescriptorSets(&(g_renderer.vulkan.core.context.renderdata.descriptors));

    return texture.id;
}

void RemoveTexture(TextureID id) {
    for (size_t i = 0; i < g_renderer.vulkan.geometry.textures.size; i++) {
        VulkanTexture texture = ARRLIST_VulkanTexture_get(&(g_renderer.vulkan.geometry.textures), i);
        if (texture.id == id) {
            vkDestroyImage(g_renderer.vulkan.core.general.interface, texture.image.image, NULL);
            vkFreeMemory(g_renderer.vulkan.core.general.interface, texture.image.memory, NULL);
            vkDestroyImageView(g_renderer.vulkan.core.general.interface, texture.image.view, NULL);
            vkDestroySampler(g_renderer.vulkan.core.general.interface, texture.sampler, NULL);
            ARRLIST_VulkanTexture_remove(&(g_renderer.vulkan.geometry.textures), i);
            return;
        }
    }
    LOG_FATAL("Texture could not be removed because it does not exist");
}

void ClearTextures() {
    // wait for device to finish
    vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);

    for (size_t i = 0; i < g_renderer.vulkan.geometry.textures.size; i++) {
        VulkanTexture texture = ARRLIST_VulkanTexture_get(&(g_renderer.vulkan.geometry.textures), i);
        vkDestroyImage(g_renderer.vulkan.core.general.interface, texture.image.image, NULL);
        vkFreeMemory(g_renderer.vulkan.core.general.interface, texture.image.memory, NULL);
        vkDestroyImageView(g_renderer.vulkan.core.general.interface, texture.image.view, NULL);
        vkDestroySampler(g_renderer.vulkan.core.general.interface, texture.sampler, NULL);
    }
    ARRLIST_VulkanTexture_clear(&(g_renderer.vulkan.geometry.textures));
}

void Render() {
    // if there's no geometry, don't render
    if (g_renderer.geometry.vertices.size == 0) return;

    // check for window changes
    if (g_renderer.dimensions.x != GetScreenWidth() || g_renderer.dimensions.y != GetScreenHeight()) VUPDT_RenderSize(GetScreenWidth(), GetScreenHeight());

    // profile for stats
    BeginProfile(&(g_renderer.stats.profile));

    // recreate vertex and index buffer if needed
    if (g_renderer.geometry.changes.update_indices) {
        vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface); // TODO: make a buffer for every swap so we don't have to wait
        g_renderer.geometry.changes.update_indices = FALSE;
        if (g_renderer.geometry.changes.max_indices != g_renderer.geometry.indices.maxsize) {
            g_renderer.geometry.changes.max_indices = g_renderer.geometry.indices.maxsize;
            vkDestroyBuffer(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.indices.buffer, NULL);
            vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.indices.memory, NULL);
			VINIT_Indices(&(g_renderer.vulkan.geometry.indices));
        } else {
			VUPDT_Indices(&(g_renderer.vulkan.geometry.indices));
        }
    }
    if (g_renderer.geometry.changes.update_vertices) {
        vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface); // TODO: make a buffer for every swap so we don't have to wait
        g_renderer.geometry.changes.update_vertices = FALSE;
        if (g_renderer.geometry.changes.max_vertices != g_renderer.geometry.vertices.maxsize) {
            g_renderer.geometry.changes.max_vertices = g_renderer.geometry.vertices.maxsize;
            vkDestroyBuffer(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.vertices.buffer, NULL);
            vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.vertices.memory, NULL);
			VINIT_Vertices(&(g_renderer.vulkan.geometry.vertices));
        } else {
			VUPDT_Vertices(&(g_renderer.vulkan.geometry.vertices));
        }
    }

    // update uniform buffers
    VUPDT_UniformBuffers(&(g_renderer.vulkan.core.context.renderdata.ubos));

    // reset command buffer and record it
    vkResetCommandBuffer(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index], 0);
    VUPDT_CommandBuffer(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index]);

    // submit command buffer
    VkSubmitInfo submitInfo = { 0 };
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index]);
    submitInfo.signalSemaphoreCount = 0;
    VkResult result = vkQueueSubmit(g_renderer.vulkan.core.scheduler.queue, 1, &submitInfo, g_renderer.vulkan.core.scheduler.syncro.fences[g_renderer.swapchain.index]);
    LOG_ASSERT(result == VK_SUCCESS, "failed to submit draw command buffer!");

    // wait for and reset rendering fence
	g_renderer.swapchain.index = (g_renderer.swapchain.index + 1) % CPUSWAP_LENGTH;
    vkWaitForFences(g_renderer.vulkan.core.general.interface, 1, &(g_renderer.vulkan.core.scheduler.syncro.fences[g_renderer.swapchain.index]), VK_TRUE, UINT64_MAX);
    vkResetFences(g_renderer.vulkan.core.general.interface, 1, &(g_renderer.vulkan.core.scheduler.syncro.fences[g_renderer.swapchain.index]));

    // update render target
    glBindTexture(GL_TEXTURE_2D, g_renderer.swapchain.targets[g_renderer.swapchain.index].texture.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_renderer.dimensions.x, g_renderer.dimensions.y, GL_RGBA, GL_UNSIGNED_BYTE, g_renderer.swapchain.reference);
    glBindTexture(GL_TEXTURE_2D, 0);

    // end profiling
    EndProfile(&(g_renderer.stats.profile));
}

void Draw(float x, float y, float w, float h) {
	size_t index = (g_renderer.swapchain.index + 1) % CPUSWAP_LENGTH;
    DrawTexturePro(
        g_renderer.swapchain.targets[index].texture,
        (Rectangle){
            (g_renderer.swapchain.targets[index].texture.width / 2.0f) - (w/2.0f),
            (g_renderer.swapchain.targets[index].texture.height / 2.0f) - (h/2.0f),
            w,
            h },
        (Rectangle){ x, y, w, h},
        (Vector2){ 0, 0 },
        0.0f,
        WHITE);
}

float RenderTime() {
    return ProfileResult(&(g_renderer.stats.profile));
}
