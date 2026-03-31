#include "vclean.h"
#include "renderer/vulkan/vutils.h"

Renderer* g_vclean_renderer_ref = NULL;

void VCLEAN_Shaders(ARRLIST_VulkanShaderPtr* shaders) {
    for (size_t i = 0; i < shaders->size; i++) {
        for (size_t j = 0; j < CPUSWAP_LENGTH; j++) {
            ARRLIST_VulkanBoundVariable_clear(&(shaders->data[i]->variables[j]));
        }
        EZ_FREE(shaders->data[i]);
    }
	ARRLIST_VulkanShaderPtr_clear(shaders);
}

void VCLEAN_Transforms(VulkanDataBuffer* transforms) {
    VUTIL_DestroyBuffer(*transforms);
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

void VCLEAN_Emissives(VulkanDataBuffer* emissives) {
    VUTIL_DestroyBuffer(*emissives);
}

void VCLEAN_Materials(VulkanDataBuffer* materials) {
    VUTIL_DestroyBuffer(*materials);
}

void VCLEAN_Geometry(VulkanGeometry* geometry) {
    VCLEAN_BVH(&(geometry->bvh));
    VCLEAN_Normals(&(geometry->normals));
    VCLEAN_Vertices(&(geometry->vertices));
    VCLEAN_Triangles(&(geometry->triangles));
    VCLEAN_Emissives(&(geometry->emissives));
    VCLEAN_Materials(&(geometry->materials));
    VCLEAN_Lights(&(geometry->lights));
    VCLEAN_Lights(&(geometry->transforms));
    VCLEAN_Simulation(&(geometry->fluid));
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
    VCLEAN_OverlayBridge(&(renderdata->overlay_bridge));
    EZ_FREE(renderdata->descriptors);
}

void VCLEAN_RenderContext(VulkanRenderContext* context) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_DestroyImage(context->targets[i]);
        VUTIL_DestroyImage(context->hdr[i]);
    }

    VCLEAN_RenderData(&(context->renderdata));

    for (size_t i = 0; i < g_vclean_renderer_ref->vulkan.core.shaders.size; i++) {
        vkDestroyPipeline(g_vclean_renderer_ref->vulkan.core.general.interface, context->pipeline.pipeline[i], NULL);
        vkDestroyPipelineLayout(g_vclean_renderer_ref->vulkan.core.general.interface, context->pipeline.layout[i], NULL);
    }
    EZ_FREE(context->pipeline.pipeline);
    EZ_FREE(context->pipeline.layout);
}

void VCLEAN_Bridge(VulkanDataBuffer* bridge) {
    vkUnmapMemory(g_vclean_renderer_ref->vulkan.core.general.interface, bridge->memory);
    VUTIL_DestroyBuffer(*bridge);
}

void VCLEAN_OverlayBridge(VulkanDataBuffer* bridge) {
    vkUnmapMemory(g_vclean_renderer_ref->vulkan.core.general.interface, bridge->memory);
    VUTIL_DestroyBuffer(*bridge);
}

void VCLEAN_Scheduler(VulkanScheduler* scheduler) {
    for (int i = 0; i < CPUSWAP_LENGTH; i++)
        vkDestroyFence(g_vclean_renderer_ref->vulkan.core.general.interface, scheduler->syncro.fences[i], NULL);
    vkDestroyCommandPool(g_vclean_renderer_ref->vulkan.core.general.interface, scheduler->commands.pool, NULL);
}

void VCLEAN_Transfer(VulkanTransfer* transfer) {
    vkFreeCommandBuffers(g_vclean_renderer_ref->vulkan.core.general.interface, transfer->pool, 1, &transfer->commands);
    vkDestroyCommandPool(g_vclean_renderer_ref->vulkan.core.general.interface, transfer->pool, NULL);
    vkDestroySemaphore(g_vclean_renderer_ref->vulkan.core.general.interface, transfer->semaphore, NULL);
    vkUnmapMemory(g_vclean_renderer_ref->vulkan.core.general.interface, transfer->staging.memory);
    VUTIL_DestroyBuffer(transfer->staging);
}

void VCLEAN_Core(VulkanCore* core) {
    VCLEAN_Transfer(&(core->transfer));
    VCLEAN_Geometry(&(core->geometry));
    VCLEAN_Bridge(&(core->bridge));
    VCLEAN_Scheduler(&(core->scheduler));
    VCLEAN_RenderContext(&(core->context));
    VCLEAN_General(&(core->general));
	VCLEAN_Shaders(&(core->shaders));
}

void VCLEAN_Vulkan(VulkanObject* vulkan) {
    vkDeviceWaitIdle(g_vclean_renderer_ref->vulkan.core.general.interface);
    VCLEAN_Metadata(&(vulkan->metadata));
    VCLEAN_Core(&(vulkan->core));
}

void VCLEAN_SetVulkanCleanContext(Renderer* renderer) {
    g_vclean_renderer_ref = renderer;
}
