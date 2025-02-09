#include "renderer.h"
#include "core/log.h"
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/vulkan/vupdate.h"
#include "renderer/vulkan/vclean.h"
#include "renderer/rutils.h"
#include <GLFW/glfw3.h>
#include <easymemory.h>
#include <string.h>
#include <time.h>

Renderer g_renderer = { 0 };
TriangleID g_triangle_id = 0;
Vector2 g_override_resolution = { 0 };

void SetViewportSlice(size_t w, size_t h) {
	float psuedo_w = w * (g_renderer.dimensions.x / (float)GetScreenWidth());
	float psuedo_h = h * (g_renderer.dimensions.y / (float)GetScreenHeight());
    g_renderer.viewport = (Vector2) { ceil(psuedo_w), ceil(psuedo_h) };
}

void OverrideResolution(size_t x, size_t y) {
	g_override_resolution = (Vector2){ x, y };
}

void InitializeRenderer() {
    // initialize camera
    g_renderer.camera.position = (Vector3){ 2.0f, 2.0f, 2.0f };
    g_renderer.camera.look = (Vector3){ 0.0f, 0.0f, 0.0f };
    g_renderer.camera.up = (Vector3){ 0.0f, 0.0f, 1.0f };

    // set up dimensions
    g_renderer.dimensions = (Vector2){ 
		g_override_resolution.x == 0 ? GetScreenWidth() : g_override_resolution.x,
		g_override_resolution.y == 0 ? GetScreenHeight() : g_override_resolution.y };

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
    ClearMaterials();
    ARRLIST_NodeBVH_clear(&(g_renderer.geometry.bvh));

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
    TriangleBB bb = {
        {
            triangle.a[0] < triangle.b[0] ?
                (triangle.a[0] < triangle.c[0] ? triangle.a[0] : triangle.c[0]) :
                (triangle.b[0] < triangle.c[0] ? triangle.b[0] : triangle.c[0]),
            triangle.a[1] < triangle.b[1] ?
                (triangle.a[1] < triangle.c[1] ? triangle.a[1] : triangle.c[1]) :
                (triangle.b[1] < triangle.c[1] ? triangle.b[1] : triangle.c[1]),
            triangle.a[2] < triangle.b[2] ?
                (triangle.a[2] < triangle.c[2] ? triangle.a[2] : triangle.c[2]) :
                (triangle.b[2] < triangle.c[2] ? triangle.b[2] : triangle.c[2])
        },
        {
            triangle.a[0] > triangle.b[0] ?
                (triangle.a[0] > triangle.c[0] ? triangle.a[0] : triangle.c[0]) :
                (triangle.b[0] > triangle.c[0] ? triangle.b[0] : triangle.c[0]),
            triangle.a[1] > triangle.b[1] ?
                (triangle.a[1] > triangle.c[1] ? triangle.a[1] : triangle.c[1]) :
                (triangle.b[1] > triangle.c[1] ? triangle.b[1] : triangle.c[1]),
            triangle.a[2] > triangle.b[2] ?
                (triangle.a[2] > triangle.c[2] ? triangle.a[2] : triangle.c[2]) :
                (triangle.b[2] > triangle.c[2] ? triangle.b[2] : triangle.c[2])
        },
        { 0, 0, 0 }
    };
    bb.centroid[0] = ((bb.max[0] - bb.min[0]) / 2.0f) + bb.min[0];
    bb.centroid[1] = ((bb.max[1] - bb.min[1]) / 2.0f) + bb.min[1];
    bb.centroid[2] = ((bb.max[2] - bb.min[2]) / 2.0f) + bb.min[2]; 
    ARRLIST_TriangleID_add(&(g_renderer.geometry.tids), g_triangle_id);
    ARRLIST_TriangleBB_add(&(g_renderer.geometry.tbbs), bb);
    ARRLIST_Triangle_add(&(g_renderer.geometry.triangles), triangle);
    g_triangle_id++;
    return g_triangle_id - 1;
}

void RemoveTriangle(TriangleID id) {
    size_t ind = 0;
    BOOL found = false;
    for (size_t i = 0; i < g_renderer.geometry.tids.size; i++) {
        if (g_renderer.geometry.tids.data[i] == id) {
            ind = i;
            found = TRUE;
            break;
        }
    }
    if (found) {
        ARRLIST_Triangle_remove(&(g_renderer.geometry.triangles), ind);
        ARRLIST_TriangleID_remove(&(g_renderer.geometry.tids), ind);
        ARRLIST_TriangleBB_remove(&(g_renderer.geometry.tbbs), ind);
        g_renderer.geometry.changes.update_triangles = TRUE;
    } else {
        LOG_FATAL("Unable to remove nonexistant triangle");
    }
}

void ClearTriangles() {
    ARRLIST_TriangleID_clear(&(g_renderer.geometry.tids));
    ARRLIST_TriangleBB_clear(&(g_renderer.geometry.tbbs));
    ARRLIST_Triangle_clear(&(g_renderer.geometry.triangles));
    g_renderer.geometry.changes.update_triangles = TRUE;
}


MaterialID SubmitMaterial(SurfaceMaterial material) {
    ARRLIST_SurfaceMaterial_add(&(g_renderer.geometry.materials), material);
    g_renderer.geometry.changes.update_materials = TRUE;
    return g_renderer.geometry.materials.size - 1;
}

void ClearMaterials() {
    ARRLIST_SurfaceMaterial_clear(&(g_renderer.geometry.materials));
    g_renderer.geometry.changes.update_materials = TRUE;
}

void Render() {
	// init rand
	srand(time(NULL));

    // profile for stats
    BeginProfile(&(g_renderer.stats.profile));

    // detect changes in described data
    BOOL descriptor_changes = 
        g_renderer.geometry.changes.update_triangles |
        g_renderer.geometry.changes.update_materials;

    // update triangles if needed
    if (g_renderer.geometry.changes.update_triangles) {
        vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface); // TODO: make a buffer for every swap so we don't have to wait
        g_renderer.geometry.changes.update_triangles = FALSE;
        if (g_renderer.geometry.changes.max_triangles != g_renderer.geometry.triangles.maxsize) {
            g_renderer.geometry.changes.max_triangles = g_renderer.geometry.triangles.maxsize;
            VCLEAN_Triangles(&(g_renderer.vulkan.core.geometry.triangles));
            VINIT_Triangles(&(g_renderer.vulkan.core.geometry.triangles));
        } else {
            VUPDT_Triangles(&(g_renderer.vulkan.core.geometry.triangles));
        }
        // update bvh
        RUTIL_BoundingVolumeHierarchy(&g_renderer.geometry.bvh, &g_renderer.geometry.tbbs);
        if (g_renderer.geometry.changes.max_bvh != g_renderer.geometry.bvh.maxsize) {
            g_renderer.geometry.changes.max_bvh = g_renderer.geometry.bvh.maxsize;
            VCLEAN_BoundingVolumeHierarchy(&(g_renderer.vulkan.core.geometry.bvh));
            VINIT_BoundingVolumeHierarchy(&(g_renderer.vulkan.core.geometry.bvh));
        } else {
            VUPDT_BoundingVolumeHierarchy(&(g_renderer.vulkan.core.geometry.bvh));
        }
    }

    // update materials if needed
    if (g_renderer.geometry.changes.update_materials) {
        vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface); // TODO: make a buffer for every swap so we don't have to wait
        g_renderer.geometry.changes.update_materials = FALSE;
        if (g_renderer.geometry.changes.max_materials != g_renderer.geometry.materials.maxsize) {
            g_renderer.geometry.changes.max_materials = g_renderer.geometry.materials.maxsize;
            VCLEAN_Materials(&(g_renderer.vulkan.core.geometry.materials));
            VINIT_Materials(&(g_renderer.vulkan.core.geometry.materials));
        } else {
            VUPDT_Materials(&(g_renderer.vulkan.core.geometry.materials));
        }
    }

    // update descriptor sets if needed
    if (descriptor_changes) VUPDT_DescriptorSets(&(g_renderer.vulkan.core.context.renderdata.descriptors));

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
	float psuedo_w = w * (g_renderer.dimensions.x / (float)GetScreenWidth());
	float psuedo_h = h * (g_renderer.dimensions.y / (float)GetScreenHeight());
    DrawTexturePro(
        g_renderer.swapchain.targets[index].texture,
        (Rectangle){
            (g_renderer.swapchain.targets[index].texture.width / 2.0f) - (psuedo_w/2.0f),
            (g_renderer.swapchain.targets[index].texture.height / 2.0f) - (psuedo_h/2.0f),
            psuedo_w,
            psuedo_h },
        (Rectangle){ x, y, w, h},
        (Vector2){ 0, 0 },
        0.0f,
        WHITE);
}

float RenderTime() {
    return ProfileResult(&(g_renderer.stats.profile));
}

size_t NumTriangles() {
    return g_renderer.geometry.triangles.size;
}

Vector2 RenderResolution() {
    return g_renderer.dimensions;
}
