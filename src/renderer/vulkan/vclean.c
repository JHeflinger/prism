#include "vclean.h"

void VCLEAN_DimensionDependant(VulkanObject* vulkan) {
    // destroy depth buffer
    vkDestroyImageView(vulkan->core.general.interface, vulkan->core.context.attachments.depth.view, NULL);
    vkDestroyImage(vulkan->core.general.interface, vulkan->core.context.attachments.depth.image, NULL);
    vkFreeMemory(vulkan->core.general.interface, vulkan->core.context.attachments.depth.memory, NULL);

    // destroy framebuffer
    vkDestroyFramebuffer(vulkan->core.general.interface, vulkan->core.context.framebuffer, NULL);

    // unmap and destroy cross buffer
    vkUnmapMemory(vulkan->core.general.interface, vulkan->core.bridge.memory);
    vkFreeMemory(vulkan->core.general.interface, vulkan->core.bridge.memory, NULL);
    vkDestroyBuffer(vulkan->core.general.interface, vulkan->core.bridge.buffer, NULL);

    // destroy image
    vkDestroyImageView(vulkan->core.general.interface, vulkan->core.context.attachments.target.view, NULL);
    vkDestroyImage(vulkan->core.general.interface, vulkan->core.context.attachments.target.image, NULL);
    vkFreeMemory(vulkan->core.general.interface, vulkan->core.context.attachments.target.memory, NULL);

    // destroy color image
    vkDestroyImageView(vulkan->core.general.interface, vulkan->core.context.attachments.color.view, NULL);
    vkDestroyImage(vulkan->core.general.interface, vulkan->core.context.attachments.color.image, NULL);
    vkFreeMemory(vulkan->core.general.interface, vulkan->core.context.attachments.color.memory, NULL);
}

void VCLEAN_Vulkan(VulkanObject* vulkan) {
    // wait for device to finish
    vkDeviceWaitIdle(vulkan->core.general.interface);

    // clean swapchain
    VCLEAN_DimensionDependant(vulkan);

    // destroy raytracing targets
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        vkDestroyImageView(vulkan->core.general.interface, vulkan->core.context.raytracer.targets[i].view, NULL);
        vkDestroyImage(vulkan->core.general.interface, vulkan->core.context.raytracer.targets[i].image, NULL);
        vkFreeMemory(vulkan->core.general.interface, vulkan->core.context.raytracer.targets[i].memory, NULL);
    }

    // destroy raytracing ssbos
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        vkDestroyBuffer(vulkan->core.general.interface, vulkan->core.context.raytracer.ssbos[i].buffer, NULL);
        vkFreeMemory(vulkan->core.general.interface, vulkan->core.context.raytracer.ssbos[i].memory, NULL);
    }

    // destroy uniform buffers
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        vkDestroyBuffer(vulkan->core.general.interface, vulkan->core.context.renderdata.ubos.objects[i].buffer, NULL);
        vkFreeMemory(vulkan->core.general.interface, vulkan->core.context.renderdata.ubos.objects[i].memory, NULL);
    }

    // destroy descriptor pools
    vkDestroyDescriptorPool(vulkan->core.general.interface, vulkan->core.context.renderdata.descriptors.pool, NULL);
    vkDestroyDescriptorPool(vulkan->core.general.interface, vulkan->core.context.raytracer.descriptors.pool, NULL);

    // destroy descriptor set layouts
    vkDestroyDescriptorSetLayout(vulkan->core.general.interface, vulkan->core.context.renderdata.descriptors.layout, NULL);
    vkDestroyDescriptorSetLayout(vulkan->core.general.interface, vulkan->core.context.raytracer.descriptors.layout, NULL);

    // destroy syncro objects
    for (int i = 0; i < CPUSWAP_LENGTH; i++)
        vkDestroyFence(vulkan->core.general.interface, vulkan->core.scheduler.syncro.fences[i], NULL);

    // destroy command pool
    vkDestroyCommandPool(vulkan->core.general.interface, vulkan->core.scheduler.commands.pool, NULL);

    // destroy pipeline
    vkDestroyPipeline(vulkan->core.general.interface, vulkan->core.context.pipeline.pipeline, NULL);
    vkDestroyPipelineLayout(vulkan->core.general.interface, vulkan->core.context.pipeline.layout, NULL);
    vkDestroyRenderPass(vulkan->core.general.interface, vulkan->core.context.renderpass, NULL);

    // destroy vulkan device
    vkDestroyDevice(vulkan->core.general.interface, NULL);

    // destroy debug messenger
    if (ENABLE_VK_VALIDATION_LAYERS) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vulkan->core.general.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_messenger != NULL)
            destroy_messenger(vulkan->core.general.instance, vulkan->core.general.messenger, NULL);
    }

    // destroy vulkan instance
    vkDestroyInstance(vulkan->core.general.instance, NULL);

    // clean required extensions
    ARRLIST_StaticString_clear(&(vulkan->metadata.validation));
    ARRLIST_StaticString_clear(&(vulkan->metadata.extensions.required));
    ARRLIST_StaticString_clear(&(vulkan->metadata.extensions.device));
}