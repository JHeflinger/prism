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

void VCLEAN_Lights(VulkanDataBuffer* lights) {
    VUTIL_DestroyBuffer(*lights);
}

void VCLEAN_BoundingVolumeHierarchy(VulkanDataBuffer* bvh) {
    VUTIL_DestroyBuffer(*bvh);
}

void VCLEAN_Triangles(VulkanDataBuffer* triangles) {
    VUTIL_DestroyBuffer(*triangles);
}

void VCLEAN_SDFs(VulkanDataBuffer* sdfs) {
    VUTIL_DestroyBuffer(*sdfs);
}

void VCLEAN_Materials(VulkanDataBuffer* materials) {
    VUTIL_DestroyBuffer(*materials);
}

void VCLEAN_Geometry(VulkanGeometry* geometry) {
    VCLEAN_Triangles(&(geometry->triangles));
    VCLEAN_Materials(&(geometry->materials));
    VCLEAN_BoundingVolumeHierarchy(&(geometry->bvh));
    VCLEAN_SDFs(&(geometry->sdfs));
    VCLEAN_Lights(&(geometry->lights));
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
    }
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_DestroyBuffer(renderdata->ubos.objects[i]);
        VUTIL_DestroyBuffer(renderdata->ubos.overlay_objects[i]);
    }
    for (size_t i = 0; i < g_vclean_renderer_ref->vulkan.core.shaders.size; i++) {
        vkDestroyDescriptorPool(g_vclean_renderer_ref->vulkan.core.general.interface, renderdata->descriptors[i].pool, NULL);
        vkDestroyDescriptorSetLayout(g_vclean_renderer_ref->vulkan.core.general.interface, renderdata->descriptors[i].layout, NULL);
    }
	VUTIL_DestroyBuffer(renderdata->overlay_ssbo);
    EZ_FREE(renderdata->descriptors);
}

void VCLEAN_RenderContext(VulkanRenderContext* context) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        vkDestroyImageView(g_vclean_renderer_ref->vulkan.core.general.interface, context->targets[i].view, NULL);
        vkDestroyImage(g_vclean_renderer_ref->vulkan.core.general.interface, context->targets[i].image, NULL);
        vkFreeMemory(g_vclean_renderer_ref->vulkan.core.general.interface, context->targets[i].memory, NULL);
        vkDestroyImageView(g_vclean_renderer_ref->vulkan.core.general.interface, context->hdr[i].view, NULL);
        vkDestroyImage(g_vclean_renderer_ref->vulkan.core.general.interface, context->hdr[i].image, NULL);
        vkFreeMemory(g_vclean_renderer_ref->vulkan.core.general.interface, context->hdr[i].memory, NULL);
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

void VCLEAN_Scheduler(VulkanScheduler* scheduler) {
    for (int i = 0; i < CPUSWAP_LENGTH; i++)
        vkDestroyFence(g_vclean_renderer_ref->vulkan.core.general.interface, scheduler->syncro.fences[i], NULL);
    vkDestroyCommandPool(g_vclean_renderer_ref->vulkan.core.general.interface, scheduler->commands.pool, NULL);
}

void VCLEAN_Core(VulkanCore* core) {
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
