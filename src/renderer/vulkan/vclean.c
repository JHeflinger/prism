#include "vclean.h"
#include "renderer/vulkan/vutils.h"

static Renderer* g_vclean_renderer_ref = NULL;

void VCLEAN_Shaders(ARRLIST_VulkanShaderPtr* shaders) {
    for (size_t i = 0; i < shaders->size; i++) {
        for (size_t j = 0; j < CPUSWAP_LENGTH; j++) {
            ARRLIST_VulkanBoundVariable_clear(&(shaders->data[i]->variables[j]));
        }
        EZ_FREE(shaders->data[i]);
    }
	ARRLIST_VulkanShaderPtr_clear(shaders);
}

void VCLEAN_ExtendedBuffers(ARRLIST_ShaderBuffer* ebuffers) {
    for (size_t i = 0; i < ebuffers->size; i++)
        VUTIL_DestroyBuffer(ebuffers->data[i].vbuffer);
}

void VCLEAN_Transforms(VulkanDataBuffer* transforms) {
    VUTIL_DestroyBuffer(*transforms);
}

void VCLEAN_Skins(VulkanDataBuffer* skins) {
    VUTIL_DestroyBuffer(*skins);
}

void VCLEAN_Poses(VulkanDataBuffer* poses) {
    VUTIL_DestroyBuffer(*poses);
}

void VCLEAN_Lights(VulkanDataBuffer* lights) {
    VUTIL_DestroyBuffer(*lights);
}

void VCLEAN_BVH(VulkanBVH* bvh) {
    VUTIL_DestroyBuffer(bvh->workhistory);
    VUTIL_DestroyBuffer(bvh->workoffsets);
    VUTIL_DestroyBuffer(bvh->mortons);
    VUTIL_DestroyBuffer(bvh->indices);
    VUTIL_DestroyBuffer(bvh->mortonswap);
    VUTIL_DestroyBuffer(bvh->indexswap);
    VUTIL_DestroyBuffer(bvh->boundingboxes);
    VUTIL_DestroyBuffer(bvh->nodes);
    VUTIL_DestroyBuffer(bvh->buckets);
}

void VCLEAN_Simulation(VulkanFluidSimulation* vfs) {
    VUTIL_DestroyImage(vfs->density.image);
    vkDestroySampler(g_vclean_renderer_ref->vulkan.core.general.interface, vfs->density.sampler, NULL);
}

void VCLEAN_Normals(VulkanDataBuffer* normals) {
    VUTIL_DestroyBuffer(*normals);
}

void VCLEAN_Vertices(VulkanVertices* vertices) {
    VUTIL_DestroyBuffer(vertices->original);
    VUTIL_DestroyBuffer(vertices->transformed);
}

void VCLEAN_Triangles(VulkanDataBuffer* triangles) {
    VUTIL_DestroyBuffer(*triangles);
}

void VCLEAN_Splats(VulkanSplats* splats) {
    VUTIL_DestroyBuffer(splats->original);
    VUTIL_DestroyBuffer(splats->transformed);
}

void VCLEAN_Emissives(VulkanDataBuffer* emissives) {
    VUTIL_DestroyBuffer(*emissives);
}

void VCLEAN_Aliases(VulkanDataBuffer* aliases) {
    VUTIL_DestroyBuffer(*aliases);
}

void VCLEAN_Materials(VulkanDataBuffer* materials) {
    VUTIL_DestroyBuffer(*materials);
}

void VCLEAN_Geometry(VulkanGeometry* geometry) {
    VCLEAN_BVH(&(geometry->bvh));
    VCLEAN_Normals(&(geometry->normals));
    VCLEAN_Vertices(&(geometry->vertices));
    VCLEAN_Triangles(&(geometry->triangles));
    VCLEAN_Splats(&(geometry->splats));
    VCLEAN_Emissives(&(geometry->emissives));
    VCLEAN_Aliases(&(geometry->aliases));
    VCLEAN_Materials(&(geometry->materials));
    VCLEAN_Lights(&(geometry->lights));
    VCLEAN_Transforms(&(geometry->transforms));
    VCLEAN_Simulation(&(geometry->fluid));
    VCLEAN_Poses(&(geometry->poses));
    VCLEAN_Skins(&(geometry->skins));
}

void VCLEAN_Metadata(VulkanMetadata* metadata) {
    ARRLIST_StaticString_clear(&(metadata->validation));
    ARRLIST_StaticString_clear(&(metadata->extensions.required));
    ARRLIST_StaticString_clear(&(metadata->extensions.device));
}

void VCLEAN_General(VulkanGeneral* general) {
    if (ENABLE_VK_VALIDATION_LAYERS) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(general->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_messenger != NULL)
            destroy_messenger(general->instance, general->messenger, NULL);
    }
    vkDestroyDevice(general->interface, NULL);
    vkDestroyInstance(general->instance, NULL);
}

void VCLEAN_RenderData(VulkanRenderData* renderdata) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_DestroyBuffer(renderdata->ssbos[i]);
	    VUTIL_DestroyBuffer(renderdata->overlay_ssbos[i]);
    }
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_DestroyBuffer(renderdata->ubos.objects[i]);
        VUTIL_DestroyBuffer(renderdata->ubos.overlay_objects[i]);
        VUTIL_DestroyBuffer(renderdata->ubos.geometry_objects[i]);
        VUTIL_DestroyBuffer(renderdata->ubos.simulation_objects[i]);
    }
    for (size_t i = 0; i < g_vclean_renderer_ref->vulkan.core.shaders.size; i++) {
        vkDestroyDescriptorPool(g_vclean_renderer_ref->vulkan.core.general.interface, renderdata->descriptors[i].pool, NULL);
        vkDestroyDescriptorSetLayout(g_vclean_renderer_ref->vulkan.core.general.interface, renderdata->descriptors[i].layout, NULL);
    }
    VCLEAN_OverlayBridge(renderdata->overlay_bridge);
    EZ_FREE(renderdata->descriptors);
}

void VCLEAN_DenoiseTargets(VulkanRenderContext* context) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_DestroyImage(context->gbuffer[i]);
        VUTIL_DestroyImage(context->denoise_a[i]);
        VUTIL_DestroyImage(context->denoise_b[i]);
    }
}

void VCLEAN_RenderContext(VulkanRenderContext* context) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_DestroyImage(context->targets[i]);
        VUTIL_DestroyImage(context->hdr[i]);
    }
    VCLEAN_DenoiseTargets(context);

    VCLEAN_RenderData(&(context->renderdata));

    for (size_t i = 0; i < g_vclean_renderer_ref->vulkan.core.shaders.size; i++) {
        vkDestroyPipeline(g_vclean_renderer_ref->vulkan.core.general.interface, context->pipeline.pipeline[i], NULL);
        vkDestroyPipelineLayout(g_vclean_renderer_ref->vulkan.core.general.interface, context->pipeline.layout[i], NULL);
    }
    EZ_FREE(context->pipeline.pipeline);
    EZ_FREE(context->pipeline.layout);
}

void VCLEAN_Bridge(VulkanDataBuffer* bridges) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        vkUnmapMemory(g_vclean_renderer_ref->vulkan.core.general.interface, bridges[i].memory);
        VUTIL_DestroyBuffer(bridges[i]);
    }
}

void VCLEAN_OverlayBridge(VulkanDataBuffer* bridges) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        vkUnmapMemory(g_vclean_renderer_ref->vulkan.core.general.interface, bridges[i].memory);
        VUTIL_DestroyBuffer(bridges[i]);
    }
}

void VCLEAN_Scheduler(VulkanScheduler* scheduler) {
    for (int i = 0; i < CPUSWAP_LENGTH; i++)
        vkDestroyFence(g_vclean_renderer_ref->vulkan.core.general.interface, scheduler->syncro.fences[i], NULL);
    vkDestroyCommandPool(g_vclean_renderer_ref->vulkan.core.general.interface, scheduler->commands.pool, NULL);
    vkDestroySemaphore(g_vclean_renderer_ref->vulkan.core.general.interface, scheduler->semaphore, NULL);
}

void VCLEAN_Transfer(VulkanTransfer* transfer) {
    vkFreeCommandBuffers(g_vclean_renderer_ref->vulkan.core.general.interface, transfer->pool, CPUSWAP_LENGTH, transfer->commands);
    vkDestroyCommandPool(g_vclean_renderer_ref->vulkan.core.general.interface, transfer->pool, NULL);
    vkDestroySemaphore(g_vclean_renderer_ref->vulkan.core.general.interface, transfer->semaphore, NULL);
    vkUnmapMemory(g_vclean_renderer_ref->vulkan.core.general.interface, transfer->staging.memory);
    VUTIL_DestroyBuffer(transfer->staging);
}

void VCLEAN_Core(VulkanCore* core) {
    VCLEAN_Transfer(&(core->transfer));
    VCLEAN_Geometry(&(core->geometry));
    VCLEAN_Bridge(core->bridge);
    VCLEAN_Scheduler(&(core->scheduler));
    VCLEAN_RenderContext(&(core->context));
    VCLEAN_General(&(core->general));
	VCLEAN_Shaders(&(core->shaders));
}

void VCLEAN_Vulkan(VulkanObject* vulkan) {
    vkDeviceWaitIdle(g_vclean_renderer_ref->vulkan.core.general.interface);
    VCLEAN_ExtendedBuffers(&(g_vclean_renderer_ref->ebuffers));
    VCLEAN_Metadata(&(vulkan->metadata));
    VCLEAN_Core(&(vulkan->core));
}

void VCLEAN_SetVulkanCleanContext(Renderer* renderer) {
    g_vclean_renderer_ref = renderer;
}
