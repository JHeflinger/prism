#include "vupdate.h"
#include "core/log.h"
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/vulkan/vclean.h"

Renderer* g_vupdt_renderer_ref = NULL;

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
    LOG_ASSERT(result == VK_SUCCESS, "Failed to begin recording command buffer!");

    // trace rays
    {
        vkCmdBindPipeline(
            command,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            g_vupdt_renderer_ref->vulkan.core.context.pipeline.pipeline);

        vkCmdBindDescriptorSets(
            command,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            g_vupdt_renderer_ref->vulkan.core.context.pipeline.layout,
            0,
            1,
            &(g_vupdt_renderer_ref->vulkan.core.context.renderdata.descriptors.sets[g_vupdt_renderer_ref->swapchain.index]),
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
        LOG_FATAL("Failed to record command!");
    }
}

void VUPDT_DescriptorSets(VulkanDescriptors* descriptors) {
    uint32_t imgw = (uint32_t)g_vupdt_renderer_ref->dimensions.x;
    uint32_t imgh = (uint32_t)g_vupdt_renderer_ref->dimensions.y;
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VkDescriptorBufferInfo bufferInfo = { 0 };
        bufferInfo.buffer = g_vupdt_renderer_ref->vulkan.core.context.renderdata.ubos.objects[i].buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorBufferInfo storageBufferInfo = { 0 };
        storageBufferInfo.buffer = g_vupdt_renderer_ref->vulkan.core.context.renderdata.ssbos[i].buffer;
        storageBufferInfo.offset = 0;
        storageBufferInfo.range = sizeof(RayGenerator) * imgw * imgh;

        VkDescriptorImageInfo imageInfo = { 0 };
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageInfo.imageView = g_vupdt_renderer_ref->vulkan.core.context.targets[i].view;

        VkDescriptorBufferInfo triangleBufferInfo = { 0 };
        triangleBufferInfo.buffer = g_vupdt_renderer_ref->vulkan.core.geometry.triangles.buffer;
        triangleBufferInfo.offset = 0;
        size_t arrsize = sizeof(Triangle) * g_vupdt_renderer_ref->geometry.triangles.size;
        arrsize = arrsize > 0 ? arrsize : 1;
        triangleBufferInfo.range = arrsize;

        VkDescriptorBufferInfo materialsBufferInfo = { 0 };
        materialsBufferInfo.buffer = g_vupdt_renderer_ref->vulkan.core.geometry.materials.buffer;
        materialsBufferInfo.offset = 0;
        arrsize = sizeof(SurfaceMaterial) * g_vupdt_renderer_ref->geometry.materials.size;
        arrsize = arrsize > 0 ? arrsize : 1;
        materialsBufferInfo.range = arrsize;

        VkDescriptorBufferInfo bvhBufferInfo = { 0 };
        bvhBufferInfo.buffer = g_vupdt_renderer_ref->vulkan.core.geometry.bvh.buffer;
        bvhBufferInfo.offset = 0;
        arrsize = sizeof(NodeBVH) * g_vupdt_renderer_ref->geometry.bvh.size;
        arrsize = arrsize > 0 ? arrsize : 1;
        bvhBufferInfo.range = arrsize;

        VkDescriptorBufferInfo sdfBufferInfo = { 0 };
        sdfBufferInfo.buffer = g_vupdt_renderer_ref->vulkan.core.geometry.sdfs.buffer;
        sdfBufferInfo.offset = 0;
        arrsize = sizeof(SDFPrimitive) * g_vupdt_renderer_ref->geometry.sdfs.size;
        arrsize = arrsize > 0 ? arrsize : 1;
        sdfBufferInfo.range = arrsize;

        VkWriteDescriptorSet descriptorWrites[7] = { 0 };

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

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = descriptors->sets[i];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].dstArrayElement = 0;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pBufferInfo = &materialsBufferInfo;

        descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5].dstSet = descriptors->sets[i];
        descriptorWrites[5].dstBinding = 5;
        descriptorWrites[5].dstArrayElement = 0;
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[5].descriptorCount = 1;
        descriptorWrites[5].pBufferInfo = &bvhBufferInfo;

        descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[6].dstSet = descriptors->sets[i];
        descriptorWrites[6].dstBinding = 6;
        descriptorWrites[6].dstArrayElement = 0;
        descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].pBufferInfo = &sdfBufferInfo;

        vkUpdateDescriptorSets(g_vupdt_renderer_ref->vulkan.core.general.interface, 7, descriptorWrites, 0, NULL);
    }
}

void VUPDT_UniformBuffers(UBOArray* ubos) {
    #define RAYVEC_TO_GLMVEC(gv, rv) { gv[0] = rv.x; gv[1] = rv.y; gv[2] = rv.z; }
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
	ubo.frametime = GetFrameTime();
	ubo.frameless = g_vupdt_renderer_ref->config.frameless;
	ubo.seed = rand();
	ubo.shadows = (uint32_t)g_vupdt_renderer_ref->config.shadows;
	ubo.reflections = (uint32_t)g_vupdt_renderer_ref->config.reflections;
	ubo.lighting = (uint32_t)g_vupdt_renderer_ref->config.lighting;
    ubo.raytrace = (uint32_t)g_vupdt_renderer_ref->config.raytrace;
    ubo.sdf = (uint32_t)g_vupdt_renderer_ref->config.sdf;
    ubo.sdfsize = g_vupdt_renderer_ref->geometry.sdfs.size;
    memcpy(ubos->mapped[g_vupdt_renderer_ref->swapchain.index], &ubo, sizeof(UniformBufferObject));
    #undef RAYVEC_TO_GLMVEC
}

void VUPDT_SetVulkanUpdateContext(Renderer* renderer) {
	g_vupdt_renderer_ref = renderer;
}

