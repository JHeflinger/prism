#include "vupdate.h"
#include "core/log.h"
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/vulkan/vclean.h"

#define INVOCATION_GROUP_SIZE 512

Renderer* g_vupdt_renderer_ref = NULL;

void VUPDT_RaytracerTriangles(VulkanRaytracer* raytracer) {
    if (sizeof(SimpleTriangle) * raytracer->triangles.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        raytracer->triangles.data,
        sizeof(SimpleTriangle) * raytracer->triangles.size,
        sizeof(SimpleTriangle) * raytracer->triangles.maxsize,
        raytracer->trianglebuffer.buffer);
}

void VUPDT_RecordRasterCommand(VkCommandBuffer command) {
    // Start command
    VkCommandBufferBeginInfo beginInfo = { 0 };
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // Optional
    beginInfo.pInheritanceInfo = NULL; // Optional
    VkResult result = vkBeginCommandBuffer(command, &beginInfo);
    LOG_ASSERT(result == VK_SUCCESS, "Failed to begin recording command buffer!");

    // Rendering
    {
        VkRenderPassBeginInfo renderPassInfo = { 0 };
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = g_vupdt_renderer_ref->vulkan.core.context.renderpass;
        renderPassInfo.framebuffer = g_vupdt_renderer_ref->vulkan.core.context.framebuffer;
        renderPassInfo.renderArea.offset = (VkOffset2D){ 0, 0 };
        renderPassInfo.renderArea.extent = (VkExtent2D){ g_vupdt_renderer_ref->dimensions.x, g_vupdt_renderer_ref->dimensions.y };

        VkClearValue clearValues[2] = { 0 };
        clearValues[0].color = (VkClearColorValue){{ 0.0f, 0.0f, 0.0f, 1.0f }};
        clearValues[1].depthStencil = (VkClearDepthStencilValue){ 1.0f, 0 };
        renderPassInfo.clearValueCount = 2;
        renderPassInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(command, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vupdt_renderer_ref->vulkan.core.context.pipeline.pipeline);

        VkViewport viewport = { 0 };
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = g_vupdt_renderer_ref->dimensions.x;
        viewport.height = g_vupdt_renderer_ref->dimensions.y;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(command, 0, 1, &viewport);

        VkRect2D scissor = { 0 };
        scissor.offset = (VkOffset2D){ 0, 0 };
        scissor.extent = (VkExtent2D){ g_vupdt_renderer_ref->dimensions.x, g_vupdt_renderer_ref->dimensions.y };
        vkCmdSetScissor(command, 0, 1, &scissor);

        VkBuffer vertexBuffers[] = { g_vupdt_renderer_ref->vulkan.geometry.vertices.buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(command, 0, 1, vertexBuffers, offsets);

        vkCmdBindIndexBuffer(command, g_vupdt_renderer_ref->vulkan.geometry.indices.buffer, 0, INDEX_VK_TYPE);

        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vupdt_renderer_ref->vulkan.core.context.pipeline.layout, 0, 1, &(g_vupdt_renderer_ref->vulkan.core.context.renderdata.descriptors.sets[g_vupdt_renderer_ref->swapchain.index]), 0, NULL);

        vkCmdDrawIndexed(command, (uint32_t)g_vupdt_renderer_ref->geometry.indices.size, 1, 0, 0, 0);

        vkCmdEndRenderPass(command);
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
        vkCmdCopyImageToBuffer(command, g_vupdt_renderer_ref->vulkan.core.context.attachments.target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_vupdt_renderer_ref->vulkan.core.bridge.buffer, 1, &region);
    }

    // End command
    result = vkEndCommandBuffer(command);
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to record command!");
    }
}

void VUPDT_RecordRaytraceCommand(VkCommandBuffer command) {
    VkCommandBufferBeginInfo beginInfo = { 0 };
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VkResult result = vkBeginCommandBuffer(command, &beginInfo);
    LOG_ASSERT(result == VK_SUCCESS, "Failed to begin recording command buffer!");

    // trace rays
    {
        vkCmdBindPipeline(
            command,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            g_vupdt_renderer_ref->vulkan.core.context.raytracer.pipeline.pipeline);

        vkCmdBindDescriptorSets(
            command,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            g_vupdt_renderer_ref->vulkan.core.context.raytracer.pipeline.layout,
            0,
            1,
            &(g_vupdt_renderer_ref->vulkan.core.context.raytracer.descriptors.sets[g_vupdt_renderer_ref->swapchain.index]),
            0,
            NULL);

        uint32_t imgw = (uint32_t)g_vupdt_renderer_ref->dimensions.x;
        uint32_t imgh = (uint32_t)g_vupdt_renderer_ref->dimensions.y;
        vkCmdDispatch(command, (imgw * imgh) / INVOCATION_GROUP_SIZE, 1, 1);
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
            g_vupdt_renderer_ref->vulkan.core.context.raytracer.targets[g_vupdt_renderer_ref->swapchain.index].image,
            VK_IMAGE_LAYOUT_GENERAL, g_vupdt_renderer_ref->vulkan.core.bridge.buffer, 1, &region);
    }

    // End command
    result = vkEndCommandBuffer(command);
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to record command!");
    }
}

void VUPDT_DescriptorSets(VulkanDescriptors* descriptors) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VkDescriptorBufferInfo bufferInfo = { 0 };
        bufferInfo.buffer = g_vupdt_renderer_ref->vulkan.core.context.renderdata.ubos.objects[i].buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfos[MAX_TEXTURES] = { 0 };
        for (size_t j = 0; j < g_vupdt_renderer_ref->vulkan.geometry.textures.size; j++) {
            imageInfos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[j].imageView = g_vupdt_renderer_ref->vulkan.geometry.textures.data[j].image.view;
            imageInfos[j].sampler = g_vupdt_renderer_ref->vulkan.geometry.textures.data[j].sampler;
        }
        // fill in rest with default texture
        for (size_t j = g_vupdt_renderer_ref->vulkan.geometry.textures.size; j < MAX_TEXTURES; j++) {
            imageInfos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[j].imageView = g_vupdt_renderer_ref->vulkan.geometry.textures.data[0].image.view;
            imageInfos[j].sampler = g_vupdt_renderer_ref->vulkan.geometry.textures.data[0].sampler;
        }

        VkWriteDescriptorSet descriptorWrites[2] = { 0 };

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptors->sets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptors->sets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = MAX_TEXTURES;
        descriptorWrites[1].pImageInfo = imageInfos;

        vkUpdateDescriptorSets(g_vupdt_renderer_ref->vulkan.core.general.interface, 2, descriptorWrites, 0, NULL);
    }
}

void VUPDT_ComputeDescriptorSets(VulkanDescriptors* descriptors) {
    uint32_t imgw = (uint32_t)g_vupdt_renderer_ref->dimensions.x;
    uint32_t imgh = (uint32_t)g_vupdt_renderer_ref->dimensions.y;
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VkDescriptorBufferInfo bufferInfo = { 0 };
        bufferInfo.buffer = g_vupdt_renderer_ref->vulkan.core.context.renderdata.ubos.objects[i].buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorBufferInfo storageBufferInfo = { 0 };
        storageBufferInfo.buffer = g_vupdt_renderer_ref->vulkan.core.context.raytracer.ssbos[i].buffer;
        storageBufferInfo.offset = 0;
        storageBufferInfo.range = sizeof(RayGenerator) * imgw * imgh;

        VkDescriptorImageInfo imageInfo = { 0 };
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.imageView = g_vupdt_renderer_ref->vulkan.core.context.raytracer.targets[i].view;

        VkDescriptorBufferInfo triangleBufferInfo = { 0 };
        triangleBufferInfo.buffer = g_vupdt_renderer_ref->vulkan.core.context.raytracer.trianglebuffer.buffer;
        triangleBufferInfo.offset = 0;
        size_t arrsize = sizeof(SimpleTriangle) * g_vupdt_renderer_ref->vulkan.core.context.raytracer.triangles.size;
        arrsize = arrsize > 0 ? arrsize : 1;
        triangleBufferInfo.range = arrsize;

        VkWriteDescriptorSet descriptorWrites[4] = { 0 };

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptors->sets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptors->sets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &storageBufferInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptors->sets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &imageInfo;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = descriptors->sets[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pBufferInfo = &triangleBufferInfo;

        vkUpdateDescriptorSets(g_vupdt_renderer_ref->vulkan.core.general.interface, 4, descriptorWrites, 0, NULL);
    }
}

void VUPDT_UniformBuffers(UBOArray* ubos) {
    UniformBufferObject ubo = { 0 };
    glm_mat4_identity(ubo.model);
    glm_lookat(
        (float*)(&g_vupdt_renderer_ref->camera.position),
        (float*)(&g_vupdt_renderer_ref->camera.look),
        (float*)(&g_vupdt_renderer_ref->camera.up), ubo.view);
    glm_perspective(
        glm_rad(45.0f),
        g_vupdt_renderer_ref->dimensions.x / g_vupdt_renderer_ref->dimensions.y,
        0.1f, 10.0f, ubo.projection);
    ubo.projection[1][1] *= -1;
	RV_TO_GV(ubo.position, g_vupdt_renderer_ref->camera.position);
	RV_TO_GV(ubo.look, g_vupdt_renderer_ref->camera.look);
    glm_vec3_sub(ubo.look, ubo.position, ubo.look);
	RV_TO_GV(ubo.up, g_vupdt_renderer_ref->camera.up);
    glm_vec3_normalize(ubo.up);
    glm_vec3_normalize(ubo.look);
    glm_vec3_negate_to(ubo.look, ubo.w);
    glm_vec3_crossn(ubo.up, ubo.w, ubo.u);
    glm_vec3_crossn(ubo.w, ubo.u, ubo.v);
	ubo.camconf[0] = glm_rad(g_vupdt_renderer_ref->camera.fov); // fov
	ubo.camconf[1] = g_vupdt_renderer_ref->dimensions.x; // width
	ubo.camconf[2] = g_vupdt_renderer_ref->dimensions.y; // height
    ubo.sizes[0] = g_vupdt_renderer_ref->vulkan.core.context.raytracer.triangles.size;
    memcpy(ubos->mapped[g_vupdt_renderer_ref->swapchain.index], &ubo, sizeof(UniformBufferObject));
}

void VUPDT_RenderSize(size_t width, size_t height) {
    vkDeviceWaitIdle(g_vupdt_renderer_ref->vulkan.core.general.interface);
    g_vupdt_renderer_ref->dimensions = (Vector2){ width, height };
    VCLEAN_DimensionDependant(&(g_vupdt_renderer_ref->vulkan));
	VINIT_Attachments(&(g_vupdt_renderer_ref->vulkan.core.context.attachments));
	VINIT_Framebuffers(&(g_vupdt_renderer_ref->vulkan.core.context.framebuffer));
	VINIT_Bridge(&(g_vupdt_renderer_ref->vulkan.core.bridge));
}

void VUPDT_Vertices(VulkanDataBuffer* vertices) {
    if (sizeof(Vertex) * g_vupdt_renderer_ref->geometry.vertices.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.vertices.data,
        sizeof(Vertex) * g_vupdt_renderer_ref->geometry.vertices.size,
        sizeof(Vertex) * g_vupdt_renderer_ref->geometry.vertices.maxsize,
        vertices->buffer);
}

void VUPDT_Indices(VulkanDataBuffer* indices) {
    if (sizeof(Index) * g_vupdt_renderer_ref->geometry.indices.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.indices.data,
        sizeof(Index) * g_vupdt_renderer_ref->geometry.indices.size,
        sizeof(Index) * g_vupdt_renderer_ref->geometry.indices.maxsize,
        indices->buffer);
}

void VUPDT_SetVulkanUpdateContext(Renderer* renderer) {
	g_vupdt_renderer_ref = renderer;
}

