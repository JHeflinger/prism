#include "vupdate.h"
#include <easylogger.h>
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/vulkan/vclean.h"
#include "renderer/renderer.h"
#include "renderer/overlay.h"
#include "renderer/rmath.h"

Renderer* g_vupdt_renderer_ref = NULL;

void VUPDT_Lights(VulkanDataBuffer* lights) {
    if (sizeof(SceneLight) * g_vupdt_renderer_ref->geometry.lights.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.lights.data,
        sizeof(SceneLight) * g_vupdt_renderer_ref->geometry.lights.size,
        sizeof(SceneLight) * g_vupdt_renderer_ref->geometry.lights.maxsize,
        lights->buffer);
}

void VUPDT_Normals(VulkanDataBuffer* normals) {
    if (sizeof(vec4) * g_vupdt_renderer_ref->geometry.normals.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.normals.data,
        sizeof(vec4) * g_vupdt_renderer_ref->geometry.normals.size,
        sizeof(vec4) * g_vupdt_renderer_ref->geometry.normals.maxsize,
        normals->buffer);
}

void VUPDT_Vertices(VulkanDataBuffer* vertices) {
    if (sizeof(vec4) * g_vupdt_renderer_ref->geometry.vertices.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.vertices.data,
        sizeof(vec4) * g_vupdt_renderer_ref->geometry.vertices.size,
        sizeof(vec4) * g_vupdt_renderer_ref->geometry.vertices.maxsize,
        vertices->buffer);
}

void VUPDT_Triangles(VulkanDataBuffer* triangles) {
    if (sizeof(Triangle) * g_vupdt_renderer_ref->geometry.triangles.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.triangles.data,
        sizeof(Triangle) * g_vupdt_renderer_ref->geometry.triangles.size,
        sizeof(Triangle) * g_vupdt_renderer_ref->geometry.triangles.maxsize,
        triangles->buffer);
}

void VUPDT_Emissives(VulkanDataBuffer* emissives) {
    if (sizeof(TriangleID) * g_vupdt_renderer_ref->geometry.emissives.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.emissives.data,
        sizeof(TriangleID) * g_vupdt_renderer_ref->geometry.emissives.size,
        sizeof(TriangleID) * g_vupdt_renderer_ref->geometry.emissives.maxsize,
        emissives->buffer);
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

    if (g_vupdt_renderer_ref->geometry.changes.update_bvh) {
        g_vupdt_renderer_ref->geometry.changes.update_bvh--;
        g_vupdt_renderer_ref->config.flags |= BVH_PIPELINE_FLAGS;
    } else {
        g_vupdt_renderer_ref->config.flags &= ~(BVH_PIPELINE_FLAGS);
    }

    // execute shader stages
    uint32_t radix_bits = 0;
    #define _record_push_constants(elements) { \
        VulkanPushConstants pc = { elements, radix_bits }; \
        vkCmdPushConstants( \
            command, \
            g_vupdt_renderer_ref->vulkan.core.context.pipeline.layout[i], \
            VK_SHADER_STAGE_COMPUTE_BIT, \
            0, sizeof(VulkanPushConstants), &pc);}
    for (size_t i = 0; i < g_vupdt_renderer_ref->vulkan.core.shaders.size; i++) {
        if (!(g_vupdt_renderer_ref->config.flags & (1u << i))) continue;
        uint32_t invocations = (uint32_t)g_vupdt_renderer_ref->dimensions.x * (uint32_t)g_vupdt_renderer_ref->dimensions.y;

        if (((1u << i) & CENTROID_SHADER_FLAG) ||
            ((1u << i) & HISTOGRAM_SHADER_FLAG) ||
            ((1u << i) & SCATTER_SHADER_FLAG) ||
            ((1u << i) & LEAVES_SHADER_FLAG) ||
            ((1u << i) & BVH_SHADER_FLAG) ||
            ((1u << i) & REBIND_SHADER_FLAG)){
            invocations = g_vupdt_renderer_ref->geometry.triangles.size;
            _record_push_constants(g_vupdt_renderer_ref->geometry.triangles.size);
        }

        if ((1u << i) & HISTORY_SHADER_FLAG) {
            uint32_t wg = ceil(g_vupdt_renderer_ref->geometry.triangles.size / ((float)INVOCATION_GROUP_SIZE));
            invocations = wg*16;
            _record_push_constants(wg);
        }

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
        vkCmdDispatch(command, ceil((invocations) / ((float)INVOCATION_GROUP_SIZE)), 1, 1);
        VUTIL_RecordGeneralBarrier(command);

        if ((1u << i) & SCATTER_SHADER_FLAG) {
            radix_bits += 4;
            if (radix_bits < 32) i -= 3;
        }
    }

    // Copy image to staging
    {
        VkImageMemoryBarrier imgBarrier = {0};
        imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imgBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        imgBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        imgBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imgBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imgBarrier.image = g_vupdt_renderer_ref->vulkan.core.context.targets[g_vupdt_renderer_ref->swapchain.index].image;
        imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imgBarrier.subresourceRange.baseMipLevel = 0;
        imgBarrier.subresourceRange.levelCount = 1;
        imgBarrier.subresourceRange.baseArrayLayer = 0;
        imgBarrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 1, &imgBarrier);
        VkBufferImageCopy region = { 0 };
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
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

    // Copy overlay ssbo back over
    {
        VkBufferMemoryBarrier bufferBarrier = {0};
        bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bufferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.buffer = g_vupdt_renderer_ref->vulkan.core.context.renderdata.overlay_ssbos[g_vupdt_renderer_ref->swapchain.index].buffer;
        bufferBarrier.offset = 0;
        bufferBarrier.size = sizeof(OverlaySSBO);
        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 1, &bufferBarrier, 0, NULL);
        VkBufferCopy copyRegion = {0};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = sizeof(OverlaySSBO);
        vkCmdCopyBuffer(
            command,
            g_vupdt_renderer_ref->vulkan.core.context.renderdata.overlay_ssbos[g_vupdt_renderer_ref->swapchain.index].buffer,
            g_vupdt_renderer_ref->vulkan.core.context.renderdata.overlay_bridge.buffer, 1, &copyRegion);
    }

    // End command
    result = vkEndCommandBuffer(command);
    if (result != VK_SUCCESS) {
        EZ_FATAL("Failed to record command!");
    }
    #undef _record_push_constants
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
                    bufferInfos[k].range = var.size.size * ceil((var.size.count.reference ? 
                        (*((size_t*)var.size.count.value)) :
                        (size_t)var.size.count.value) / (var.size.reduction > 0.0f ? var.size.reduction : 1.0f));
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
    static PipelineFlags old_flags = 0;
    static int reset_count = 0;
    if (old_camera.fov == 0.0f) old_camera = g_vupdt_renderer_ref->camera;
    if (old_flags == 0) old_flags = g_vupdt_renderer_ref->config.flags;
    if (old_flags != g_vupdt_renderer_ref->config.flags ||
        memcmp(&old_camera, &(g_vupdt_renderer_ref->camera), sizeof(SimpleCamera)) != 0) 
        reset_count = CPUSWAP_LENGTH;
    old_camera = g_vupdt_renderer_ref->camera;
    old_flags = g_vupdt_renderer_ref->config.flags;
    BOOL cam_reset = reset_count != 0;
    if (reset_count > 0) reset_count--;

    // persistant vars
    static uint32_t samples = 0;
    samples++;
    if (cam_reset) samples = 1;

    // core uniform buffer
    {
        UniformBufferObject ubo = { 0 };
        glm_vec3_copy(g_vupdt_renderer_ref->camera.position, ubo.position);
        glm_vec3_copy(g_vupdt_renderer_ref->camera.look, ubo.look);
        glm_vec3_sub(ubo.look, ubo.position, ubo.look);
        glm_vec3_copy(g_vupdt_renderer_ref->camera.up, ubo.up);
        glm_vec3_normalize(ubo.up);
        glm_vec3_normalize(ubo.look);
        CameraUVW(g_vupdt_renderer_ref->camera, ubo.u, ubo.v, ubo.w);
        ubo.fov = glm_rad(g_vupdt_renderer_ref->camera.fov);
        ubo.width = g_vupdt_renderer_ref->dimensions.x;
        ubo.height = g_vupdt_renderer_ref->dimensions.y;
        ubo.triangles = g_vupdt_renderer_ref->geometry.triangles.size;
        ubo.viewport[0] = g_vupdt_renderer_ref->viewport.x;
        ubo.viewport[1] = g_vupdt_renderer_ref->viewport.y;
        ubo.emissives = g_vupdt_renderer_ref->geometry.emissives.size;
        ubo.frametime = RenderFrameTime();
        ubo.seed = rand();
        ubo.lights = g_vupdt_renderer_ref->geometry.lights.size;
        ubo.grid = (uint32_t)g_vupdt_renderer_ref->config.grid;
        ubo.reset = cam_reset;
        ubo.samples = samples;
        ubo.direct = g_vupdt_renderer_ref->config.direct;
        ubo.lightarea = g_vupdt_renderer_ref->geometry.lightarea;
        ubo.whitepoint = g_vupdt_renderer_ref->config.whitepoint*g_vupdt_renderer_ref->config.whitepoint;
        ubo.gamma = g_vupdt_renderer_ref->config.gamma;
        ubo.swap = CPUSWAP_LENGTH;
        ubo.showdof = g_vupdt_renderer_ref->config.showdof;
        ubo.aperature = g_vupdt_renderer_ref->camera.aperature;
        ubo.focus = g_vupdt_renderer_ref->camera.focus;
        ubo.normals = g_vupdt_renderer_ref->config.normals;
        ubo.directonly = g_vupdt_renderer_ref->config.directonly;
        ubo.scenelighting = g_vupdt_renderer_ref->config.scenelighting;
        ubo.scenelightingonly = g_vupdt_renderer_ref->config.scenelightingonly;
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
        ubo.single_selected_vid = GetSelectedVertex();
        ubo.divisor = g_vupdt_renderer_ref->config.flags & PATHTRACE_SHADER_FLAG ? CPUSWAP_LENGTH : 1;
        ubo.mode = GetOverlayMode();
        memcpy(ubos->overlay_mapped[g_vupdt_renderer_ref->swapchain.index], &ubo, sizeof(OverlayUniformBufferObject));
    }

    // geometry uniform buffer
    {
        GeometryUniformBufferObject ubo = { 0 };
        glm_vec3_copy(g_vupdt_renderer_ref->geometry.bounds.min, ubo.minBB);
        glm_vec3_copy(g_vupdt_renderer_ref->geometry.bounds.max, ubo.maxBB);
        memcpy(ubos->geometry_mapped[g_vupdt_renderer_ref->swapchain.index], &ubo, sizeof(GeometryUniformBufferObject));
    }
}

void VUPDT_SetVulkanUpdateContext(Renderer* renderer) {
	g_vupdt_renderer_ref = renderer;
}

