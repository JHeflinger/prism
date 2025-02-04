#include "renderer.h"
#include "core/log.h"
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/vulkan/vupdate.h"
#include "renderer/vulkan/vclean.h"
//#include <GLFW/glfw3.h>
#include <easymemory.h>
#include <string.h>
#include <math.h>

Renderer g_renderer = { 0 };
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
	VCLEAN_SetVulkanCleanContext(&g_renderer);
	BOOL result = VINIT_Vulkan(&(g_renderer.vulkan));
	LOG_ASSERT(result, "Failed to initialize vulkan");

    // set up cpu swap
	for (int i = 0; i < CPUSWAP_LENGTH; i++) {
		g_renderer.swapchain.targets[i] = LoadRenderTexture(g_renderer.dimensions.x, g_renderer.dimensions.y);
		LOG_ASSERT(IsRenderTextureValid(g_renderer.swapchain.targets[i]), "Unable to load target texture");
	}

    // configure stat profiler
    ConfigureProfile(&(g_renderer.stats.profile), "Renderer", 100);
}

void DestroyRenderer() {
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
    g_renderer.geometry.changes.update_triangles = TRUE;
    ARRLIST_TriangleID_add(&(g_renderer.geometry.ids), g_triangle_id);
    ARRLIST_Triangle_add(&(g_renderer.geometry.triangles), triangle);
    g_triangle_id++;
    return g_triangle_id - 1;
}

void RemoveTriangle(TriangleID id) {
    size_t ind = 0;
    BOOL found = false;
    for (size_t i = 0; i < g_renderer.geometry.ids.size; i++) {
        if (g_renderer.geometry.ids.data[i] == id) {
            ind = i;
            found = TRUE;
            break;
        }
    }
    if (found) {
        ARRLIST_Triangle_remove(&(g_renderer.geometry.triangles), ind);
        ARRLIST_TriangleID_remove(&(g_renderer.geometry.ids), ind);
        g_renderer.geometry.changes.update_triangles = TRUE;
    } else {
        LOG_FATAL("Unable to remove nonexistant triangle");
    }
}

void ClearTriangles() {
    vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);
    ARRLIST_TriangleID_clear(&(g_renderer.geometry.ids));
    ARRLIST_Triangle_clear(&(g_renderer.geometry.triangles));
    g_renderer.geometry.changes.update_triangles = TRUE;
}

void Render() {
    // profile for stats
    BeginProfile(&(g_renderer.stats.profile));

    if (g_renderer.geometry.changes.update_triangles) {
        vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface); // TODO: make a buffer for every swap so we don't have to wait
        g_renderer.geometry.changes.update_triangles = FALSE;
        if (g_renderer.geometry.changes.max_triangles != g_renderer.geometry.triangles.maxsize) {
            g_renderer.geometry.changes.max_triangles = g_renderer.geometry.triangles.maxsize;
            VCLEAN_Triangles(&(g_renderer.vulkan.geometry.triangles));
            VINIT_Triangles(&(g_renderer.vulkan.geometry.triangles));
            VUPDT_DescriptorSets(&(g_renderer.vulkan.core.context.renderdata.descriptors));
        } else {
            VUPDT_Triangles(&(g_renderer.vulkan.geometry.triangles));
            VUPDT_DescriptorSets(&(g_renderer.vulkan.core.context.renderdata.descriptors));
        }
    }

    // update uniform buffers
    VUPDT_UniformBuffers(&(g_renderer.vulkan.core.context.renderdata.ubos));

    // reset command buffer and record it
    vkResetCommandBuffer(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index], 0);
    VUPDT_RecordCommand(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index]);

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

size_t NumTriangles() {
    return g_renderer.vulkan.geometry.triangles.size;
}

Vector2 RenderResolution() {
    return g_renderer.dimensions;
}