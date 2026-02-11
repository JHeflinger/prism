#include "renderer.h"
#include <easylogger.h>
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/vulkan/vupdate.h"
#include "renderer/vulkan/vclean.h"
#include "renderer/rutils.h"
#include "renderer/overlay.h"
#include <GLFW/glfw3.h>
#include <easymemory.h>
#include <string.h>
#include <time.h>

Renderer g_renderer = { 0 };
TriangleID g_triangle_id = 0;
SDFID g_sdf_id = 0;
LightID g_light_id = 0;
Vector2 g_override_resolution = { 0 };
float g_rft = 0.0f;

PipelineFlags GetPipelineFlags() {
    return g_renderer.config.flags;
}

void SetPipelineFlags(PipelineFlags flags) {
    g_renderer.config.flags = flags;
}

void SetViewportSlice(size_t w, size_t h) {
	float psuedo_w = w * (g_renderer.dimensions.x / (float)GetScreenWidth());
	float psuedo_h = h * (g_renderer.dimensions.y / (float)GetScreenHeight());
    g_renderer.viewport = (Vector2) { ceil(psuedo_w), ceil(psuedo_h) };
}

void OverrideResolution(size_t x, size_t y) {
	g_override_resolution = (Vector2){ x, y };
}

void InitializeRenderer() {
	// init rand
	srand(time(NULL));

    // initialize config
    g_renderer.config.frameless = 1.0;
    g_renderer.config.smoothen = 0.0f;
    g_renderer.config.marches = 100;
    g_renderer.config.roulette = -1.0f;
    g_renderer.config.whitepoint = 20.0f;
    g_renderer.config.gamma = 2.2f;
    g_renderer.config.direct = FALSE;
    g_renderer.config.autoframeless = FALSE;
    g_renderer.config.grid = TRUE;
    g_renderer.config.async = TRUE;
    g_renderer.config.flags = PREVIEW_PIPELINE_FLAGS;

    // initialize camera
    g_renderer.camera = (SimpleCamera){
        { 2.11f, 0.0f, 2.133f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        90.0f
    };

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
	EZ_ASSERT(result, "Failed to initialize vulkan");

    // set up cpu swap
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
	    g_renderer.swapchain.target[i] = LoadRenderTexture(g_renderer.dimensions.x, g_renderer.dimensions.y);
	    EZ_ASSERT(IsRenderTextureValid(g_renderer.swapchain.target[i]), "Unable to load target texture");
    }

    // configure stat profiler
    ConfigureProfile(&(g_renderer.stats.profile), "Renderer", 10);

    // configure GPU stat cache
    g_renderer.stats.cache.update_interval = 1.0;
    PollGPUCache(TRUE);

    // default material
    SubmitNamedMaterial((SurfaceMaterial){
        {0.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        0, 0, 2
    }, "Default");

    // set overlay context
    SetOverlayContext(&g_renderer);
}

void DestroyRenderer() {
    // clean geometry
    ClearTriangles();
    ClearMaterials();
    ClearSDFs();
    ClearLights();
    ARRLIST_NodeBVH_clear(&(g_renderer.geometry.bvh));

    // destroy vulkan resources
    VCLEAN_Vulkan(&(g_renderer.vulkan));

    // unload cpu swap textures
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++)
	    UnloadRenderTexture(g_renderer.swapchain.target[i]);
}

SimpleCamera GetCamera() {
    return g_renderer.camera;
}

void MoveCamera(SimpleCamera camera) {
    g_renderer.camera = camera;
}

void ReorientCamera() {
    for (int i = 0; i < 3; i++) {
        float sign = g_renderer.camera.up[i] > 0 ? 1.0f : (g_renderer.camera.up[i] < 0 ? -1.0f : 0.0f);
        g_renderer.camera.look[i] += sign*1e-6f;
    }
    vec3 desired = { 0, 0, 1 };
    SETVEC(g_renderer.camera.up, desired);
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
    vec3 emission; SETVEC(emission, g_renderer.geometry.materials.data[triangle.material].emission);
    if (emission[0] != 0 || emission[1] != 0 || emission[2] != 0)
        ARRLIST_TriangleID_add(&(g_renderer.geometry.emissives), g_renderer.geometry.triangles.size);
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
        for (size_t i = 0; i < g_renderer.geometry.emissives.size; i++) {
            if (g_renderer.geometry.emissives.data[i] == ind) {
                ARRLIST_TriangleID_remove(&(g_renderer.geometry.emissives), i);
                break;
            }
        }
        ARRLIST_Triangle_remove(&(g_renderer.geometry.triangles), ind);
        ARRLIST_TriangleID_remove(&(g_renderer.geometry.tids), ind);
        ARRLIST_TriangleBB_remove(&(g_renderer.geometry.tbbs), ind);
        g_renderer.geometry.changes.update_triangles = TRUE;
    } else {
        EZ_FATAL("Unable to remove nonexistant triangle");
    }
}

void ClearTriangles() {
    ARRLIST_TriangleID_clear(&(g_renderer.geometry.tids));
    ARRLIST_TriangleBB_clear(&(g_renderer.geometry.tbbs));
    ARRLIST_Triangle_clear(&(g_renderer.geometry.triangles));
    ARRLIST_TriangleID_clear(&(g_renderer.geometry.emissives));
    g_renderer.geometry.changes.update_triangles = TRUE;
}

SDFID SubmitSDF(SDFPrimitive sdf) {
    g_renderer.geometry.changes.update_sdfs = TRUE;
    ARRLIST_SDFID_add(&(g_renderer.geometry.sdfids), g_sdf_id);
    ARRLIST_SDFPrimitive_add(&(g_renderer.geometry.sdfs), sdf);
    g_sdf_id++;
    return g_sdf_id - 1;
}

void RemoveSDF(SDFID id) {
    size_t ind = 0;
    BOOL found = false;
    for (size_t i = 0; i < g_renderer.geometry.sdfids.size; i++) {
        if (g_renderer.geometry.sdfids.data[i] == id) {
            ind = i;
            found = TRUE;
            break;
        }
    }
    if (found) {
        ARRLIST_SDFPrimitive_remove(&(g_renderer.geometry.sdfs), ind);
        ARRLIST_SDFID_remove(&(g_renderer.geometry.sdfids), ind);
        g_renderer.geometry.changes.update_sdfs = TRUE;
    } else {
        EZ_FATAL("Unable to remove nonexistant sdf");
    }
}

void ClearSDFs() {
    ARRLIST_SDFID_clear(&(g_renderer.geometry.sdfids));
    ARRLIST_SDFPrimitive_clear(&(g_renderer.geometry.sdfs));
    g_renderer.geometry.changes.update_sdfs = TRUE;
}

LightID SubmitLight(PointLight light) {
    g_renderer.geometry.changes.update_lights = TRUE;
    ARRLIST_LightID_add(&(g_renderer.geometry.lids), g_light_id);
    ARRLIST_PointLight_add(&(g_renderer.geometry.lights), light);
    g_light_id++;
    return g_light_id - 1;
}

void RemoveLight(LightID id) {
    size_t ind = 0;
    BOOL found = false;
    for (size_t i = 0; i < g_renderer.geometry.lights.size; i++) {
        if (g_renderer.geometry.lids.data[i] == id) {
            ind = i;
            found = TRUE;
            break;
        }
    }
    if (found) {
        ARRLIST_PointLight_remove(&(g_renderer.geometry.lights), ind);
        ARRLIST_LightID_remove(&(g_renderer.geometry.lids), ind);
        g_renderer.geometry.changes.update_lights = TRUE;
    } else {
        EZ_FATAL("Unable to remove nonexistant light");
    }
}

void ClearLights() {
    ARRLIST_LightID_clear(&(g_renderer.geometry.lids));
    ARRLIST_PointLight_clear(&(g_renderer.geometry.lights));
    g_renderer.geometry.changes.update_lights = TRUE;
}

MaterialID SubmitMaterial(SurfaceMaterial material) {
    char buf[MAX_MATERIAL_NAME_SIZE] = { 0 };
    sprintf(buf, "Material #%d", (int)g_renderer.geometry.materials.size);
    return SubmitNamedMaterial(material, buf);
}

MaterialID SubmitNamedMaterial(SurfaceMaterial material, const char* name) {
    ARRLIST_SurfaceMaterial_add(&(g_renderer.geometry.materials), material);
    char* b = EZ_ALLOC(MAX_MATERIAL_NAME_SIZE + 1, sizeof(char));
    strncpy(b, name, MAX_MATERIAL_NAME_SIZE);
    ARRLIST_DynamicString_add(&(g_renderer.geometry.materialnames), b);
    g_renderer.geometry.changes.update_materials = TRUE;
    return g_renderer.geometry.materials.size - 1;
}

char* MaterialName(MaterialID mid) {
    EZ_ASSERT(mid < g_renderer.geometry.materials.size, "Invalid material ID detected");
    return g_renderer.geometry.materialnames.data[mid];
}

char** MaterialNameReference(MaterialID mid) {
    EZ_ASSERT(mid < g_renderer.geometry.materials.size, "Invalid material ID detected");
    return &(g_renderer.geometry.materialnames.data[mid]);
}

void ClearMaterials() {
    ARRLIST_SurfaceMaterial_clear(&(g_renderer.geometry.materials));
    for (size_t i = 0; i < g_renderer.geometry.materialnames.size; i++)
        EZ_FREE(g_renderer.geometry.materialnames.data[i]);
    ARRLIST_DynamicString_clear(&(g_renderer.geometry.materialnames));
    g_renderer.geometry.changes.update_materials = TRUE;
}

void Render() {
    static BOOL async_update = TRUE;

    // update render frame time;
    g_rft += GetFrameTime();

    // detect changes in described data
    if (async_update) {
        // profile for stats
        BeginProfile(&(g_renderer.stats.profile));

        BOOL descriptor_changes = 
            g_renderer.geometry.changes.update_triangles |
            g_renderer.geometry.changes.update_materials |
            g_renderer.geometry.changes.update_sdfs |
            g_renderer.geometry.changes.update_lights;

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
            if (g_renderer.geometry.changes.max_emissives != g_renderer.geometry.emissives.maxsize) {
                g_renderer.geometry.changes.max_emissives = g_renderer.geometry.emissives.maxsize;
                VCLEAN_Emissives(&(g_renderer.vulkan.core.geometry.emissives));
                VINIT_Emissives(&(g_renderer.vulkan.core.geometry.emissives));
            } else {
                VUPDT_Emissives(&(g_renderer.vulkan.core.geometry.emissives));
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

        // update sdfs if needed
        if (g_renderer.geometry.changes.update_sdfs) {
            vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface); // TODO: make a buffer for every swap so we don't have to wait
            g_renderer.geometry.changes.update_sdfs = FALSE;
            if (g_renderer.geometry.changes.max_sdfs != g_renderer.geometry.sdfs.maxsize) {
                g_renderer.geometry.changes.max_sdfs = g_renderer.geometry.sdfs.maxsize;
                VCLEAN_SDFs(&(g_renderer.vulkan.core.geometry.sdfs));
                VINIT_SDFs(&(g_renderer.vulkan.core.geometry.sdfs));
            } else {
                VUPDT_SDFs(&(g_renderer.vulkan.core.geometry.sdfs));
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

        // update lights if needed
        if (g_renderer.geometry.changes.update_lights) {
            vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface); // TODO: make a buffer for every swap so we don't have to wait
            g_renderer.geometry.changes.update_lights = FALSE;
            if (g_renderer.geometry.changes.max_lights != g_renderer.geometry.lights.maxsize) {
                g_renderer.geometry.changes.max_lights = g_renderer.geometry.lights.maxsize;
                VCLEAN_Lights(&(g_renderer.vulkan.core.geometry.lights));
                VINIT_Lights(&(g_renderer.vulkan.core.geometry.lights));
            } else {
                VUPDT_Lights(&(g_renderer.vulkan.core.geometry.lights));
            }
        }

        // update descriptor sets if needed
        if (descriptor_changes) VUPDT_DescriptorSets(g_renderer.vulkan.core.context.renderdata.descriptors);

        // update uniform buffers
        VUPDT_UniformBuffers(&(g_renderer.vulkan.core.context.renderdata.ubos));

        // reset renderer frame time
        g_rft = 0.0f;

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
        EZ_ASSERT(result == VK_SUCCESS, "failed to submit draw command buffer!");
    }

    // wait for and reset rendering fence
	size_t new_ind = (g_renderer.swapchain.index + 1) % CPUSWAP_LENGTH;
    if (!g_renderer.config.async)
        vkWaitForFences(g_renderer.vulkan.core.general.interface, 1, &(g_renderer.vulkan.core.scheduler.syncro.fences[new_ind]), VK_TRUE, UINT64_MAX);
    if (vkGetFenceStatus(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.scheduler.syncro.fences[new_ind]) == VK_SUCCESS) {
        // copy overlay results to host
        VUTIL_CopyBufferToHost(
            (void*)(ExposedOverlaySSBO()),
            sizeof(OverlaySSBO),
            sizeof(OverlaySSBO),
            g_renderer.vulkan.core.context.renderdata.overlay_ssbo.buffer);

        // reset fences and update swapchain index
        vkResetFences(g_renderer.vulkan.core.general.interface, 1, &(g_renderer.vulkan.core.scheduler.syncro.fences[new_ind]));
        g_renderer.swapchain.index = new_ind;
        async_update = TRUE;

        // update render target
        glBindTexture(GL_TEXTURE_2D, g_renderer.swapchain.target[new_ind].texture.id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_renderer.dimensions.x, g_renderer.dimensions.y, GL_RGBA, GL_UNSIGNED_BYTE, g_renderer.swapchain.reference);
        glBindTexture(GL_TEXTURE_2D, 0);
        
        // end profiling
        EndProfile(&(g_renderer.stats.profile));
    } else {
        async_update = FALSE;
    }
}

void DrawHelper(float x, float y, float w, float h, float maxw, float maxh) {
    ClearBackground(BLACK);
    BeginBlendMode(BLEND_ADDITIVE);
	float psuedo_w = w * (g_renderer.dimensions.x / maxw);
	float psuedo_h = h * (g_renderer.dimensions.y / maxh);
    if (g_renderer.config.flags & PATHTRACE_SHADER_FLAG) {
        for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
            DrawTexturePro(
                g_renderer.swapchain.target[i].texture,
                (Rectangle){
                    (g_renderer.swapchain.target[i].texture.width / 2.0f) - (psuedo_w/2.0f),
                    (g_renderer.swapchain.target[i].texture.height / 2.0f) - (psuedo_h/2.0f),
                    psuedo_w,
                    psuedo_h },
                (Rectangle){ x, y, w, h},
                (Vector2){ 0, 0 },
                0.0f,
                WHITE);
        }
    } else {
        DrawTexturePro(
            g_renderer.swapchain.target[g_renderer.swapchain.index].texture,
            (Rectangle){
                (g_renderer.swapchain.target[g_renderer.swapchain.index].texture.width / 2.0f) - (psuedo_w/2.0f),
                (g_renderer.swapchain.target[g_renderer.swapchain.index].texture.height / 2.0f) - (psuedo_h/2.0f),
                psuedo_w,
                psuedo_h },
            (Rectangle){ x, y, w, h},
            (Vector2){ 0, 0 },
            0.0f,
            WHITE);
    }
    EndBlendMode();
}

void Draw(float x, float y, float w, float h) {
    DrawHelper(x, y, w, h, (float)GetScreenWidth(), (float)GetScreenHeight());
}

float RenderTime() {
    return ProfileResult(&(g_renderer.stats.profile));
}

size_t NumTriangles() {
    return g_renderer.geometry.triangles.size;
}

size_t NumSDFs() {
    return g_renderer.geometry.sdfs.size;
}

size_t NumMaterials() {
    return g_renderer.geometry.materials.size;
}

size_t NumEmissives() {
    return g_renderer.geometry.emissives.size;
}

SurfaceMaterial* MaterialReference(size_t index) {
    EZ_ASSERT(index < g_renderer.geometry.materials.size, "Invalid material index requested");
    return &(g_renderer.geometry.materials.data[index]);
}

void UpdateMaterials() {
    g_renderer.geometry.changes.update_materials = TRUE;
}

size_t NumLights() {
    return g_renderer.geometry.lights.size;
}

PointLight* LightReference(size_t index) {
    EZ_ASSERT(index < g_renderer.geometry.lights.size, "Invalid light index requested");
    return &(g_renderer.geometry.lights.data[index]);
}

void UpdateLights() {
    g_renderer.geometry.changes.update_lights = TRUE;
}

Vector2 RenderResolution() {
    return g_renderer.dimensions;
}

RendererConfig* RenderConfig() {
    return &(g_renderer.config);
}

float RenderFrameTime() {
    return g_rft;
}

Triangle* TriangleReference(size_t index) {
    EZ_ASSERT(index < g_renderer.geometry.triangles.size, "Invalid triangle index requested");
    return &(g_renderer.geometry.triangles.data[index]);
}

void RecalculateTriangleBB(size_t index) {
    EZ_ASSERT(index < g_renderer.geometry.triangles.size, "Invalid triangle index requested");
    Triangle triangle = g_renderer.geometry.triangles.data[index];
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
    g_renderer.geometry.tbbs.data[index] = bb;
}

void UpdateTriangles() {
    g_renderer.geometry.changes.update_triangles = TRUE;
}

void SaveRender(const char* filepath) {
	RenderTexture rt = LoadRenderTexture(g_renderer.dimensions.x, g_renderer.dimensions.y);
    BeginTextureMode(rt);
    DrawHelper(0, 0, g_renderer.dimensions.x, -g_renderer.dimensions.y, g_renderer.dimensions.x, g_renderer.dimensions.y);
    EndTextureMode();
    Image image = LoadImageFromTexture(rt.texture);
    ExportImage(image, filepath);
    UnloadImage(image);
    UnloadRenderTexture(rt);
}

char* GPUModel() {
    return g_renderer.vulkan.core.general.gpuname;
}

void PollGPUCache(BOOL init) {
    if (init || (GetTime() - g_renderer.stats.cache.update_timer) > g_renderer.stats.cache.update_interval) {
		if (init) {
			ARRLIST_StaticString_add(&(g_renderer.vulkan.metadata.extensions.required), "VK_KHR_get_physical_device_properties2");
			if (!VUTIL_CheckGPUExtensionSupport(g_renderer.vulkan.core.general.gpu)) {
				g_renderer.stats.cache.available = FALSE;
			} else {
				g_renderer.stats.cache.available = TRUE;
			}
			ARRLIST_StaticString_remove(&(g_renderer.vulkan.metadata.extensions.required), g_renderer.vulkan.metadata.extensions.required.size - 1);
		}
		if (g_renderer.stats.cache.available) {
			g_renderer.stats.cache.update_timer = GetTime();
			g_renderer.stats.cache.heap_budget = (VkPhysicalDeviceMemoryBudgetPropertiesEXT) { 0 };
	        g_renderer.stats.cache.heap_budget.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
			g_renderer.stats.cache.heap_budget.pNext = NULL;
			g_renderer.stats.cache.heap_props = (VkPhysicalDeviceMemoryProperties2) { 0 };
			g_renderer.stats.cache.heap_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
			g_renderer.stats.cache.heap_props.pNext = &(g_renderer.stats.cache.heap_budget);
			vkGetPhysicalDeviceMemoryProperties2(g_renderer.vulkan.core.general.gpu, &(g_renderer.stats.cache.heap_props));
		}
    }
}

size_t GPUHeapCount() {
	if (!g_renderer.stats.cache.available) return 0;
    return g_renderer.stats.cache.heap_props.memoryProperties.memoryHeapCount;
}

size_t GPUHeapUsage(size_t i) {
	if (!g_renderer.stats.cache.available) return 0;
    return g_renderer.stats.cache.heap_budget.heapUsage[i];
}

size_t GPUHeapBudget(size_t i) {
	if (!g_renderer.stats.cache.available) return 0;
    return g_renderer.stats.cache.heap_budget.heapBudget[i];
}

const char* GPUHeapType(size_t i) {
	if (!g_renderer.stats.cache.available) return "Unavailable";
    if (g_renderer.stats.cache.heap_props.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        return "LOCAL";
    if (g_renderer.stats.cache.heap_props.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_MULTI_INSTANCE_BIT)
        return "MULTI";
    return "SHARE";
}
