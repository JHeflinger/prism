#include "vclean.h"
#include "renderer/vulkan/vutils.h"

Renderer* g_vlcean_renderer_ref = NULL;

void VCLEAN_Triangles(VulkanDataBuffer* triangles) {
    VUTIL_DestroyBuffer(*triangles);
}

void VCLEAN_Geometry(VulkanGeometry* geometry) {
    VCLEAN_Triangles(&(geometry->triangles));
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
    }
    vkDestroyDescriptorPool(g_vlcean_renderer_ref->vulkan.core.general.interface, renderdata->descriptors.pool, NULL);
    vkDestroyDescriptorSetLayout(g_vlcean_renderer_ref->vulkan.core.general.interface, renderdata->descriptors.layout, NULL);
}

void VCLEAN_RenderContext(VulkanRenderContext* context) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        vkDestroyImageView(g_vlcean_renderer_ref->vulkan.core.general.interface, context->targets[i].view, NULL);
        vkDestroyImage(g_vlcean_renderer_ref->vulkan.core.general.interface, context->targets[i].image, NULL);
        vkFreeMemory(g_vlcean_renderer_ref->vulkan.core.general.interface, context->targets[i].memory, NULL);
    }

    VCLEAN_RenderData(&(context->renderdata));

    vkDestroyPipeline(g_vlcean_renderer_ref->vulkan.core.general.interface, context->pipeline.pipeline, NULL);
    vkDestroyPipelineLayout(g_vlcean_renderer_ref->vulkan.core.general.interface, context->pipeline.layout, NULL);
}

void VCLEAN_Bridge(VulkanDataBuffer* bridge) {
    vkUnmapMemory(g_vlcean_renderer_ref->vulkan.core.general.interface, bridge->memory);
    VUTIL_DestroyBuffer(*bridge);
}

void VCLEAN_Scheduler(VulkanScheduler* scheduler) {
    for (int i = 0; i < CPUSWAP_LENGTH; i++)
        vkDestroyFence(g_vlcean_renderer_ref->vulkan.core.general.interface, scheduler->syncro.fences[i], NULL);
    vkDestroyCommandPool(g_vlcean_renderer_ref->vulkan.core.general.interface, scheduler->commands.pool, NULL);
}

void VCLEAN_Core(VulkanCore* core) {
    VCLEAN_Geometry(&(core->geometry));
    VCLEAN_Bridge(&(core->bridge));
    VCLEAN_Scheduler(&(core->scheduler));
    VCLEAN_RenderContext(&(core->context));
    VCLEAN_General(&(core->general));
}

void VCLEAN_Vulkan(VulkanObject* vulkan) {
    vkDeviceWaitIdle(g_vlcean_renderer_ref->vulkan.core.general.interface);
    VCLEAN_Metadata(&(vulkan->metadata));
    VCLEAN_Core(&(vulkan->core));
}

void VCLEAN_SetVulkanCleanContext(Renderer* renderer) {
    g_vlcean_renderer_ref = renderer;
}