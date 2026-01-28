#include "vupdate.h"
#include <easylogger.h>
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/vulkan/vclean.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"

Renderer* g_vupdt_renderer_ref = NULL;

void VUPDT_Lights(VulkanDataBuffer* lights) {
    if (sizeof(PointLight) * g_vupdt_renderer_ref->geometry.lights.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.lights.data,
        sizeof(PointLight) * g_vupdt_renderer_ref->geometry.lights.size,
        sizeof(PointLight) * g_vupdt_renderer_ref->geometry.lights.maxsize,
        lights->buffer);
}

void VUPDT_BoundingVolumeHierarchy(VulkanDataBuffer* bvh) {
    if (sizeof(NodeBVH) * g_vupdt_renderer_ref->geometry.bvh.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.bvh.data,
        sizeof(NodeBVH) * g_vupdt_renderer_ref->geometry.bvh.size,
        sizeof(NodeBVH) * g_vupdt_renderer_ref->geometry.bvh.maxsize,
        bvh->buffer);
}

void VUPDT_Triangles(VulkanDataBuffer* triangles) {
    if (sizeof(Triangle) * g_vupdt_renderer_ref->geometry.triangles.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.triangles.data,
        sizeof(Triangle) * g_vupdt_renderer_ref->geometry.triangles.size,
        sizeof(Triangle) * g_vupdt_renderer_ref->geometry.triangles.maxsize,
        triangles->buffer);
}

void VUPDT_SDFs(VulkanDataBuffer* sdfs) {
    if (sizeof(SDFPrimitive) * g_vupdt_renderer_ref->geometry.sdfs.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.sdfs.data,
        sizeof(SDFPrimitive) * g_vupdt_renderer_ref->geometry.sdfs.size,
        sizeof(SDFPrimitive) * g_vupdt_renderer_ref->geometry.sdfs.maxsize,
        sdfs->buffer);
}

void VUPDT_Materials(VulkanDataBuffer* materials) {
    if (sizeof(SurfaceMaterial) * g_vupdt_renderer_ref->geometry.materials.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.materials.data,
        sizeof(SurfaceMaterial) * g_vupdt_renderer_ref->geometry.materials.size,
        sizeof(SurfaceMaterial) * g_vupdt_renderer_ref->geometry.materials.maxsize,
        materials->buffer);
}

void VUPDT_RecordCommand(VkCommandBuffer command) {
    VkCommandBufferBeginInfo beginInfo = { 0 };
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VkResult result = vkBeginCommandBuffer(command, &beginInfo);
    EZ_ASSERT(result == VK_SUCCESS, "Failed to begin recording command buffer!");

    // execute shader stages
    for (size_t i = 0; i < g_vupdt_renderer_ref->vulkan.core.shaders.size; i++) {
        if (!g_vupdt_renderer_ref->vulkan.core.shaders.data[i]->enabled) continue;

        vkCmdBindPipeline(
            command,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            g_vupdt_renderer_ref->vulkan.core.context.pipeline.pipeline[i]);

        vkCmdBindDescriptorSets(
            command,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            g_vupdt_renderer_ref->vulkan.core.context.pipeline.layout[i],
            0,
            1,
            &(g_vupdt_renderer_ref->vulkan.core.context.renderdata.descriptors[i].sets[g_vupdt_renderer_ref->swapchain.index]),
            0,
            NULL);

        float imgw = (uint32_t)g_vupdt_renderer_ref->dimensions.x;
        float imgh = (uint32_t)g_vupdt_renderer_ref->dimensions.y;
        vkCmdDispatch(command, ceil((imgw * imgh) / ((float)INVOCATION_GROUP_SIZE)), 1, 1);
    }

    // Copy image to staging
    {
        VkBufferImageCopy region = { 0 };
        region.bufferOffset = 0;
        region.bufferRowLength = 0; // Tightly packed
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = (VkOffset3D){ 0, 0, 0 };
        region.imageExtent = (VkExtent3D){ g_vupdt_renderer_ref->dimensions.x, g_vupdt_renderer_ref->dimensions.y, 1 };
        vkCmdCopyImageToBuffer(
            command,
            g_vupdt_renderer_ref->vulkan.core.context.targets[g_vupdt_renderer_ref->swapchain.index].image,
            VK_IMAGE_LAYOUT_GENERAL, g_vupdt_renderer_ref->vulkan.core.bridge.buffer, 1, &region);
    }

    // End command
    result = vkEndCommandBuffer(command);
    if (result != VK_SUCCESS) {
        EZ_FATAL("Failed to record command!");
    }
}

void VUPDT_DescriptorSets(VulkanDescriptors* descriptors) {
    size_t num_shaders = g_vupdt_renderer_ref->vulkan.core.shaders.size;
    for (size_t i = 0; i < num_shaders; i++) {
        VulkanShader* shader = g_vupdt_renderer_ref->vulkan.core.shaders.data[i];
        size_t vars = shader->variables[0].size;
        for (size_t j = 0; j < CPUSWAP_LENGTH; j++) {
            VkDescriptorBufferInfo* bufferInfos = EZ_ALLOC(vars, sizeof(VkDescriptorBufferInfo));
            VkDescriptorImageInfo* imageInfos = EZ_ALLOC(vars, sizeof(VkDescriptorImageInfo));
            VkWriteDescriptorSet* descriptorWrites = EZ_ALLOC(vars, sizeof(VkWriteDescriptorSet));
            for (size_t k = 0; k < vars; k++) {
                VulkanBoundVariable var = shader->variables[j].data[k];
                descriptorWrites[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[k].dstSet = descriptors[i].sets[j];
                descriptorWrites[k].dstBinding = k;
                descriptorWrites[k].dstArrayElement = 0;
                descriptorWrites[k].descriptorType = (VkDescriptorType)var.type;
                descriptorWrites[k].descriptorCount = 1;
                if (var.type == STORAGE_IMAGE) {
                    imageInfos[k].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    imageInfos[k].imageView = var.data.reference ? *((VkImageView*)var.data.value) : (VkImageView)var.data.value;
                    descriptorWrites[k].pImageInfo = &(imageInfos[k]);
                } else {
                    bufferInfos[k].buffer = var.data.reference ? *((VkBuffer*)var.data.value) : (VkBuffer)var.data.value;
                    bufferInfos[k].offset = 0;
                    bufferInfos[k].range = var.size.count.reference ? var.size.size * (*((size_t*)var.size.count.value)) : var.size.size * (size_t)var.size.count.value;
                    bufferInfos[k].range = bufferInfos[k].range > 0 ? bufferInfos[k].range : 1;
                    descriptorWrites[k].pBufferInfo = &(bufferInfos[k]);
                }
            }
            vkUpdateDescriptorSets(g_vupdt_renderer_ref->vulkan.core.general.interface, vars, descriptorWrites, 0, NULL);
            EZ_FREE(bufferInfos);
            EZ_FREE(imageInfos);
            EZ_FREE(descriptorWrites);
        }
    }
}

void VUPDT_UniformBuffers(UBOArray* ubos) {
    #define RAYVEC_TO_GLMVEC(gv, rv) { gv[0] = rv.x; gv[1] = rv.y; gv[2] = rv.z; }
	// relative mouse coords
    Rectangle viewport_rec = GetViewportRec();
    Vector2 renderer_dimensions = g_vupdt_renderer_ref->dimensions;
    Vector2 offset = {
        viewport_rec.x + (viewport_rec.width / 2.0f) - (GetScreenWidth() / 2.0f),
        viewport_rec.y + (viewport_rec.height / 2.0f) - (GetScreenHeight() / 2.0f)
    };
    uint32_t mx = (GetMouseX() - offset.x) * (renderer_dimensions.x / GetScreenWidth());
    uint32_t my = (GetMouseY() - offset.y) * (renderer_dimensions.y / GetScreenHeight());

    // check for camera reset
    static SimpleCamera old_camera = { 0 };
    BOOL cam_reset = FALSE;
    if (old_camera.fov == 0.0f) old_camera = g_vupdt_renderer_ref->camera;
    if (memcmp(&old_camera, &(g_vupdt_renderer_ref->camera), sizeof(SimpleCamera)) != 0) {
        cam_reset = TRUE;
        old_camera = g_vupdt_renderer_ref->camera;
    }

    // persistant vars
    static uint32_t ao_samples = 1;
    ao_samples++;
    if (cam_reset) ao_samples = 1;

    // core uniform buffer
    {
        UniformBufferObject ubo = { 0 };
        RAYVEC_TO_GLMVEC(ubo.position, g_vupdt_renderer_ref->camera.position);
        RAYVEC_TO_GLMVEC(ubo.look, g_vupdt_renderer_ref->camera.look);
        glm_vec3_sub(ubo.look, ubo.position, ubo.look);
        RAYVEC_TO_GLMVEC(ubo.up, g_vupdt_renderer_ref->camera.up);
        glm_vec3_normalize(ubo.up);
        glm_vec3_normalize(ubo.look);
        glm_vec3_negate_to(ubo.look, ubo.w);
        glm_vec3_crossn(ubo.up, ubo.w, ubo.u);
        glm_vec3_crossn(ubo.w, ubo.u, ubo.v);
        ubo.fov = glm_rad(g_vupdt_renderer_ref->camera.fov);
        ubo.width = g_vupdt_renderer_ref->dimensions.x;
        ubo.height = g_vupdt_renderer_ref->dimensions.y;
        ubo.triangles = g_vupdt_renderer_ref->geometry.triangles.size;
        ubo.viewport[0] = g_vupdt_renderer_ref->viewport.x;
        ubo.viewport[1] = g_vupdt_renderer_ref->viewport.y;
        ubo.bvhsize = g_vupdt_renderer_ref->geometry.bvh.size;
        ubo.frametime = RenderFrameTime();
        if (g_vupdt_renderer_ref->config.autoframeless) {
            #define TARGET_FRAMETIME 0.016f
            if (RenderFrameTime() > 0) {
                g_vupdt_renderer_ref->config.frameless *= (TARGET_FRAMETIME / RenderFrameTime());
                if (g_vupdt_renderer_ref->config.frameless > 1.0f)
                    g_vupdt_renderer_ref->config.frameless = 1.0f;
            }
            #undef TARGET_FRAMETIME
        }
        ubo.frameless = g_vupdt_renderer_ref->config.frameless;
        ubo.seed = rand();
        ubo.shadows = (uint32_t)g_vupdt_renderer_ref->config.shadows;
        ubo.reflections = (uint32_t)g_vupdt_renderer_ref->config.reflections;
        ubo.lighting = (uint32_t)g_vupdt_renderer_ref->config.lighting;
        ubo.raytrace = (uint32_t)g_vupdt_renderer_ref->config.raytrace;
        ubo.sdf = (uint32_t)g_vupdt_renderer_ref->config.sdf;
        ubo.sdfsize = g_vupdt_renderer_ref->geometry.sdfs.size;
        ubo.sdfsmooth = g_vupdt_renderer_ref->config.sdfsmooth;
        ubo.maxmarches = g_vupdt_renderer_ref->config.maxmarches;
        ubo.time = g_vupdt_renderer_ref->config.time;
        ubo.antialiasing = (uint32_t)g_vupdt_renderer_ref->config.antialiasing;
        ubo.lightssize = g_vupdt_renderer_ref->geometry.lights.size;
        ubo.grid = (uint32_t)g_vupdt_renderer_ref->config.grid;
        ubo.mouse_x = mx;
        ubo.mouse_y = my;
        ubo.reset = cam_reset;
        ubo.ao_samples = ao_samples;
        memcpy(ubos->mapped[g_vupdt_renderer_ref->swapchain.index], &ubo, sizeof(UniformBufferObject));
    }

    // overlay uniform buffer
    {
        OverlayUniformBufferObject ubo = { 0 };
        ubo.mouse_x = mx;
        ubo.mouse_y = my;
		ubo.image_width = g_vupdt_renderer_ref->dimensions.x;
		ubo.image_height = g_vupdt_renderer_ref->dimensions.y;
        ubo.single_selected_tid = GetSelectedTriangle();
        memcpy(ubos->overlay_mapped[g_vupdt_renderer_ref->swapchain.index], &ubo, sizeof(OverlayUniformBufferObject));
    }
    #undef RAYVEC_TO_GLMVEC
}

void VUPDT_SetVulkanUpdateContext(Renderer* renderer) {
	g_vupdt_renderer_ref = renderer;
}

