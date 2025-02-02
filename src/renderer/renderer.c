#include "renderer.h"
#include "core/log.h"
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vinit.h"
#include "renderer/vulkan/vupdate.h"
#include "renderer/vulkan/vclean.h"
#include <GLFW/glfw3.h>
#include <easymemory.h>
#include <string.h>
#include <stb_image.h>
#include <math.h>

Renderer g_renderer = { 0 };
TextureID g_texture_id = 0;
TriangleID g_triangle_id = 0;

void CreatePipeline() {
	SimpleFile* vertshadercode = ReadFile("build/shaders/shader.vert.spv");
	SimpleFile* fragshadercode = ReadFile("build/shaders/shader.frag.spv");
    SimpleFile* compshadercode = ReadFile("build/shaders/shader.comp.spv");
	VkShaderModule vertshader = VUTIL_CreateShader(vertshadercode);
	VkShaderModule fragshader = VUTIL_CreateShader(fragshadercode);
	VkShaderModule compshader = VUTIL_CreateShader(compshadercode);

	VkPipelineShaderStageCreateInfo vertShaderStageInfo = { 0 };
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertshader;
	vertShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo = { 0 };
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragshader;
	fragShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo compShaderStageInfo = { 0 };
	compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	compShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	compShaderStageInfo.module = compshader;
	compShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderstages[] = { vertShaderStageInfo, fragShaderStageInfo, compShaderStageInfo };

    VkVertexInputBindingDescription bindingDescription = VUTIL_VertexBindingDescription();
    QUAD_VkVertexInputAttributeDescription attributeDescriptions = VUTIL_VertexAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { 0 };
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 4;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.value;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { 0 };
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkDynamicState dynamicStates[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState = { 0 };
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineViewportStateCreateInfo viewportState = { 0 };
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = { 0 };
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f; // Optional
    rasterizer.depthBiasClamp = 0.0f; // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

    VkPipelineMultisampleStateCreateInfo multisampling = { 0 };
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.rasterizationSamples = g_renderer.vulkan.core.context.samples;
    multisampling.minSampleShading = 0.2f; // closer to one is smoother anti-aliasing
    multisampling.pSampleMask = NULL; // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE; // Optional

    VkPipelineColorBlendAttachmentState colorBlendAttachment = { 0 };
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending = { 0 };
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = { 0 };
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &(g_renderer.vulkan.core.context.renderdata.descriptors.layout);
    pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
    pipelineLayoutInfo.pPushConstantRanges = NULL; // Optional

    VkResult result = vkCreatePipelineLayout(g_renderer.vulkan.core.general.interface, &pipelineLayoutInfo, NULL, &(g_renderer.vulkan.core.context.pipeline.layout));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create pipeline layout");

    VkPipelineDepthStencilStateCreateInfo depthStencil = { 0 };
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f; // Optional
    depthStencil.maxDepthBounds = 1.0f; // Optional
    depthStencil.stencilTestEnable = VK_FALSE;
    //depthStencil.front = { 0 }; // Optional
    //depthStencil.back = { 0 }; // Optional

    VkGraphicsPipelineCreateInfo pipelineInfo = { 0 };
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderstages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = g_renderer.vulkan.core.context.pipeline.layout;
    pipelineInfo.renderPass = g_renderer.vulkan.core.context.renderpass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1; // Optional

    result = vkCreateGraphicsPipelines(g_renderer.vulkan.core.general.interface, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &(g_renderer.vulkan.core.context.pipeline.pipeline));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create pipeline");

	FreeFile(vertshadercode);
	FreeFile(fragshadercode);
    FreeFile(compshadercode);
	vkDestroyShaderModule(g_renderer.vulkan.core.general.interface, vertshader, NULL);
	vkDestroyShaderModule(g_renderer.vulkan.core.general.interface, fragshader, NULL);
	vkDestroyShaderModule(g_renderer.vulkan.core.general.interface, compshader, NULL);
}

void CreateFramebuffers() {
    VkImageView attachments[] = { g_renderer.vulkan.core.context.attachments.color.view, g_renderer.vulkan.core.context.attachments.depth.view, g_renderer.vulkan.core.context.attachments.target.view };
    VkFramebufferCreateInfo framebufferInfo = { 0 };
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = g_renderer.vulkan.core.context.renderpass;
    framebufferInfo.attachmentCount = 3;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = g_renderer.dimensions.x;
    framebufferInfo.height = g_renderer.dimensions.y;
    framebufferInfo.layers = 1;
    VkResult result = vkCreateFramebuffer(g_renderer.vulkan.core.general.interface, &framebufferInfo, NULL, &(g_renderer.vulkan.core.context.framebuffer));
    LOG_ASSERT(result == VK_SUCCESS, "Unable to create framebuffer");
}

void RecordCommand(VkCommandBuffer command) {
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
        renderPassInfo.renderPass = g_renderer.vulkan.core.context.renderpass;
        renderPassInfo.framebuffer = g_renderer.vulkan.core.context.framebuffer;
        renderPassInfo.renderArea.offset = (VkOffset2D){ 0, 0 };
        renderPassInfo.renderArea.extent = (VkExtent2D){ g_renderer.dimensions.x, g_renderer.dimensions.y };

        VkClearValue clearValues[2] = { 0 };
        clearValues[0].color = (VkClearColorValue){{ 0.0f, 0.0f, 0.0f, 1.0f }};
        clearValues[1].depthStencil = (VkClearDepthStencilValue){ 1.0f, 0 };
        renderPassInfo.clearValueCount = 2;
        renderPassInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(command, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, g_renderer.vulkan.core.context.pipeline.pipeline);

        VkViewport viewport = { 0 };
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = g_renderer.dimensions.x;
        viewport.height = g_renderer.dimensions.y;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(command, 0, 1, &viewport);

        VkRect2D scissor = { 0 };
        scissor.offset = (VkOffset2D){ 0, 0 };
        scissor.extent = (VkExtent2D){ g_renderer.dimensions.x, g_renderer.dimensions.y };
        vkCmdSetScissor(command, 0, 1, &scissor);

        VkBuffer vertexBuffers[] = { g_renderer.vulkan.geometry.vertices.buffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(command, 0, 1, vertexBuffers, offsets);

        vkCmdBindIndexBuffer(command, g_renderer.vulkan.geometry.indices.buffer, 0, INDEX_VK_TYPE);

        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, g_renderer.vulkan.core.context.pipeline.layout, 0, 1, &(g_renderer.vulkan.core.context.renderdata.descriptors.sets[g_renderer.swapchain.index]), 0, NULL);

        vkCmdDrawIndexed(command, (uint32_t)g_renderer.geometry.indices.size, 1, 0, 0, 0);

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
        region.imageExtent = (VkExtent3D){ g_renderer.dimensions.x, g_renderer.dimensions.y, 1 };
        vkCmdCopyImageToBuffer(command, g_renderer.vulkan.core.context.attachments.target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_renderer.vulkan.core.bridge.buffer, 1, &region);
    }

    // End command
    result = vkEndCommandBuffer(command);
    LOG_ASSERT(result == VK_SUCCESS, "Failed to record command buffer!");
}

void CleanSwapchain() {
    // destroy depth buffer
    vkDestroyImageView(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.attachments.depth.view, NULL);
    vkDestroyImage(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.attachments.depth.image, NULL);
    vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.attachments.depth.memory, NULL);

    // destroy framebuffer
    vkDestroyFramebuffer(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.framebuffer, NULL);

    // unmap and destroy cross buffer
    vkUnmapMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.bridge.memory);
    vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.bridge.memory, NULL);
    vkDestroyBuffer(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.bridge.buffer, NULL);

    // destroy image
    vkDestroyImageView(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.attachments.target.view, NULL);
    vkDestroyImage(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.attachments.target.image, NULL);
    vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.attachments.target.memory, NULL);

    // destroy color image
    vkDestroyImageView(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.attachments.color.view, NULL);
    vkDestroyImage(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.attachments.color.image, NULL);
    vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.attachments.color.memory, NULL);
}

void RecreateSwapchain() {
    vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);
    g_renderer.dimensions = (Vector2){ GetScreenWidth(), GetScreenHeight() };
    CleanSwapchain();
	VINIT_Attachments(&(g_renderer.vulkan.core.context.attachments));
    CreateFramebuffers();
}

void CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding = { 0 };
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = NULL;

    VkDescriptorSetLayoutBinding samplerLayoutBinding = { 0 };
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = MAX_TEXTURES;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = NULL;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding bindings[] = { uboLayoutBinding, samplerLayoutBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo = { 0 };
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    VkResult result = vkCreateDescriptorSetLayout(g_renderer.vulkan.core.general.interface, &layoutInfo, NULL, &(g_renderer.vulkan.core.context.renderdata.descriptors.layout));
    LOG_ASSERT(result == VK_SUCCESS, "Failed to create descriptor set layout!");
}

void CreateUniformBuffers() {
    VkDeviceSize size = sizeof(UniformBufferObject);
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_CreateBuffer(
            size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &(g_renderer.vulkan.core.context.renderdata.ubos.objects[i]));

        vkMapMemory(
            g_renderer.vulkan.core.general.interface,
            g_renderer.vulkan.core.context.renderdata.ubos.objects[i].memory,
            0, size, 0, &g_renderer.vulkan.core.context.renderdata.ubos.mapped[i]);
    }
}

void UpdateUniformBuffers() {
    static float time = 0.0f;
    time += GetFrameTime();
    UniformBufferObject ubo = { 0 };
    glm_mat4_identity(ubo.model);
    glm_lookat((float*)(&g_renderer.camera.position), (float*)(&g_renderer.camera.look), (float*)(&g_renderer.camera.up), ubo.view);
    glm_perspective(glm_rad(45.0f), g_renderer.dimensions.x / g_renderer.dimensions.y, 0.1f, 10.0f, ubo.projection);
    ubo.projection[1][1] *= -1;
    memcpy(g_renderer.vulkan.core.context.renderdata.ubos.mapped[g_renderer.swapchain.index], &ubo, sizeof(UniformBufferObject));
}

void CreateDescriptorPool() {
    VkDescriptorPoolSize poolSizes[2] = { 0 };
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = CPUSWAP_LENGTH;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = CPUSWAP_LENGTH * MAX_TEXTURES;

    VkDescriptorPoolCreateInfo poolInfo = { 0 };
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = CPUSWAP_LENGTH;
    VkResult result = vkCreateDescriptorPool(g_renderer.vulkan.core.general.interface, &poolInfo, NULL, &(g_renderer.vulkan.core.context.renderdata.descriptors.pool));
    LOG_ASSERT(result == VK_SUCCESS, "failed to create descriptor pool!");
}

void UpdateDescriptorSets() {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VkDescriptorBufferInfo bufferInfo = { 0 };
        bufferInfo.buffer = g_renderer.vulkan.core.context.renderdata.ubos.objects[i].buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfos[MAX_TEXTURES] = { 0 };
        for (size_t j = 0; j < g_renderer.vulkan.geometry.textures.size; j++) {
            imageInfos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[j].imageView = g_renderer.vulkan.geometry.textures.data[j].image.view;
            imageInfos[j].sampler = g_renderer.vulkan.geometry.textures.data[j].sampler;
        }
        // fill in rest with default texture
        for (size_t j = g_renderer.vulkan.geometry.textures.size; j < MAX_TEXTURES; j++) {
            imageInfos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfos[j].imageView = g_renderer.vulkan.geometry.textures.data[0].image.view;
            imageInfos[j].sampler = g_renderer.vulkan.geometry.textures.data[0].sampler;
        }

        VkWriteDescriptorSet descriptorWrites[2] = { 0 };

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = g_renderer.vulkan.core.context.renderdata.descriptors.sets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = g_renderer.vulkan.core.context.renderdata.descriptors.sets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = MAX_TEXTURES;
        descriptorWrites[1].pImageInfo = imageInfos;
        vkUpdateDescriptorSets(g_renderer.vulkan.core.general.interface, 2, descriptorWrites, 0, NULL);
    }
}

void CreateDescriptorSets() {
    VkDescriptorSetLayout layouts[CPUSWAP_LENGTH];
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) layouts[i] = g_renderer.vulkan.core.context.renderdata.descriptors.layout;
    VkDescriptorSetAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = g_renderer.vulkan.core.context.renderdata.descriptors.pool;
    allocInfo.descriptorSetCount = CPUSWAP_LENGTH;
    allocInfo.pSetLayouts = layouts;
    VkResult result = vkAllocateDescriptorSets(g_renderer.vulkan.core.general.interface, &allocInfo, g_renderer.vulkan.core.context.renderdata.descriptors.sets);
    LOG_ASSERT(result == VK_SUCCESS, "Failed to allocate descriptor sets!");
}

void DestroyVulkan() {
    // wait for device to finish
    vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface);

    // clean swapchain
    CleanSwapchain();

    // destroy textures
    ClearTextures();

    // destroy uniform buffers
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        vkDestroyBuffer(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.renderdata.ubos.objects[i].buffer, NULL);
        vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.renderdata.ubos.objects[i].memory, NULL);
    }

    // destroy descriptor pool
    vkDestroyDescriptorPool(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.renderdata.descriptors.pool, NULL);

    // destroy descriptor set layout
    vkDestroyDescriptorSetLayout(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.renderdata.descriptors.layout, NULL);

    // destroy syncro objects
    for (int i = 0; i < CPUSWAP_LENGTH; i++)
        vkDestroyFence(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.scheduler.syncro.fences[i], NULL);

    // destroy command pool
    vkDestroyCommandPool(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.scheduler.commands.pool, NULL);

    // destroy pipeline
    vkDestroyPipeline(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.pipeline.pipeline, NULL);
    vkDestroyPipelineLayout(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.pipeline.layout, NULL);
    vkDestroyRenderPass(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.core.context.renderpass, NULL);

    // clean geometry
    ClearTriangles();

    // destroy vulkan device
    vkDestroyDevice(g_renderer.vulkan.core.general.interface, NULL);

    // destroy debug messenger
    if (ENABLE_VK_VALIDATION_LAYERS) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g_renderer.vulkan.core.general.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_messenger != NULL)
            destroy_messenger(g_renderer.vulkan.core.general.instance, g_renderer.vulkan.core.general.messenger, NULL);
    }

    // destroy vulkan instance
    vkDestroyInstance(g_renderer.vulkan.core.general.instance, NULL);

    // clean required extensions
    ARRLIST_StaticString_clear(&(g_renderer.vulkan.metadata.validation));
    ARRLIST_StaticString_clear(&(g_renderer.vulkan.metadata.extensions.required));
    ARRLIST_StaticString_clear(&(g_renderer.vulkan.metadata.extensions.device));
}

void InitializeVulkan() {
    CreateUniformBuffers();
	CreateDescriptorSetLayout();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreatePipeline();
    CreateFramebuffers();
}

void InitializeRenderer() {
    // initialize camera
    g_renderer.camera.position = (Vector3){ 2.0f, 2.0f, 2.0f };
    g_renderer.camera.look = (Vector3){ 0.0f, 0.0f, 0.0f };
    g_renderer.camera.up = (Vector3){ 0.0f, 0.0f, 1.0f };

    // set up dimensions
    g_renderer.dimensions = (Vector2){ GetScreenWidth(), GetScreenHeight() };

    // initialize vulkan resources
	VUTIL_SetVulkanUtilsContext(&g_renderer);
	VINIT_SetVulkanInitContext(&g_renderer);
	VUPDT_SetVulkanUpdateContext(&g_renderer);
	BOOL result = VINIT_Vulkan(&(g_renderer.vulkan));
	LOG_ASSERT(result, "Failed to initialize vulkan");
	InitializeVulkan();

    // default texture
    SubmitTexture("assets/images/default.png");

    // set up cpu swap
	for (int i = 0; i < CPUSWAP_LENGTH; i++) {
		g_renderer.swapchain.targets[i] = LoadRenderTexture(g_renderer.dimensions.x, g_renderer.dimensions.y);
		LOG_ASSERT(IsRenderTextureValid(g_renderer.swapchain.targets[i]), "Unable to load target texture");
	}

    // configure stat profiler
    ConfigureProfile(&(g_renderer.stats.profile), "Renderer", 100);
}

void DestroyRenderer() {
    // destroy vulkan resources
    DestroyVulkan();

    // unload cpu swap textures
	for (int i = 0; i < CPUSWAP_LENGTH; i++)
		UnloadRenderTexture(g_renderer.swapchain.targets[i]);
}

SimpleCamera GetCamera() {
    return g_renderer.camera;
}

void MoveCamera(SimpleCamera camera) {
    g_renderer.camera = camera;
}

TriangleID SubmitTriangle(Triangle triangle) {
    ARRLIST_Index_add(&(g_renderer.geometry.indices), g_renderer.geometry.vertices.size);
    ARRLIST_Vertex_add(&(g_renderer.geometry.vertices), triangle.vertices[0]);
    ARRLIST_Index_add(&(g_renderer.geometry.indices), g_renderer.geometry.vertices.size);
    ARRLIST_Vertex_add(&(g_renderer.geometry.vertices), triangle.vertices[1]);
    ARRLIST_Index_add(&(g_renderer.geometry.indices), g_renderer.geometry.vertices.size);
    ARRLIST_Vertex_add(&(g_renderer.geometry.vertices), triangle.vertices[2]);
    g_renderer.geometry.changes.update_indices = TRUE;
    g_renderer.geometry.changes.update_vertices = TRUE;
    ARRLIST_TriangleID_add(&(g_renderer.vulkan.geometry.triangles), g_triangle_id);
    g_triangle_id++;
    return g_triangle_id - 1;
}

void RemoveTriangle(TriangleID id) {
    size_t ind = 0;
    BOOL found = false;
    for (size_t i = 0; i < g_renderer.vulkan.geometry.triangles.size; i++) {
        if (g_renderer.vulkan.geometry.triangles.data[i] == id) {
            ind = i;
            found = TRUE;
            break;
        }
    }
    if (found) {
        for (int i = 3; i > 0; i--) {
            int realind = i - 1;
            ARRLIST_Index_remove(&(g_renderer.geometry.indices), ind + realind);
            ARRLIST_Vertex_remove(&(g_renderer.geometry.vertices), ind + realind);
        }
        ARRLIST_TriangleID_remove(&(g_renderer.vulkan.geometry.triangles), ind);
        g_renderer.geometry.changes.update_indices = TRUE;
        g_renderer.geometry.changes.update_vertices = TRUE;
    } else {
        LOG_FATAL("Unable to remove nonexistant triangle");
    }
}

void ClearTriangles() {
    // clean vertex data
    ARRLIST_Vertex_clear(&(g_renderer.geometry.vertices));

    // clean index data
    ARRLIST_Index_clear(&(g_renderer.geometry.indices));

    // clean triangle ref data
    ARRLIST_TriangleID_clear(&(g_renderer.vulkan.geometry.triangles));

    // destroy vertex buffer
    vkDestroyBuffer(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.vertices.buffer, NULL);

    // free vertex memory
    vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.vertices.memory, NULL);

    // destroy index buffer
    vkDestroyBuffer(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.indices.buffer, NULL);

    // free index memory
    vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.indices.memory, NULL);
}

TextureID SubmitTexture(const char* filepath) {
    LOG_ASSERT(g_renderer.vulkan.geometry.textures.size < MAX_TEXTURES, "No more room for more textures");
    VulkanTexture texture = { 0 };

    // Create texture image
    {
        int texWidth, texHeight, texChannels;
        stbi_uc* pixels = stbi_load(filepath, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        LOG_ASSERT(pixels, "Failed to load texture image");
        texture.filepath = filepath;
        texture.width = texWidth;
        texture.height = texHeight;
        texture.mip_levels = (uint32_t)floor(log2(texWidth > texHeight ? texWidth : texHeight)) + 1;
        VkDeviceSize imageSize = texWidth * texHeight * 4;
        VulkanDataBuffer stagingBuffer;
        VUTIL_CreateBuffer(
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingBuffer);
        void* data;
        vkMapMemory(g_renderer.vulkan.core.general.interface, stagingBuffer.memory, 0, imageSize, 0, &data);
        memcpy(data, pixels, (size_t)imageSize);
        vkUnmapMemory(g_renderer.vulkan.core.general.interface, stagingBuffer.memory);
        stbi_image_free(pixels);
        VUTIL_CreateImage(
            texWidth,
            texHeight,
            texture.mip_levels,
            VK_SAMPLE_COUNT_1_BIT,
            IMAGE_FORMAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT, 
            &(texture.image));
        VUTIL_TransitionImageLayout(texture.image.image, IMAGE_FORMAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mip_levels);
        VUTIL_CopyBufferToImage(stagingBuffer.buffer, texture.image.image, (uint32_t)texWidth, (uint32_t)texHeight);
        VUTIL_GenerateMipmaps(texture.image.image, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, texture.mip_levels);
        VUTIL_DestroyBuffer(stagingBuffer);
    }

    // Create texture sampler
    {
        VkPhysicalDeviceProperties properties = { 0 };
        vkGetPhysicalDeviceProperties(g_renderer.vulkan.core.general.gpu, &properties);
        VkSamplerCreateInfo samplerInfo = { 0 };
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = (float)texture.mip_levels;
        VkResult result = vkCreateSampler(g_renderer.vulkan.core.general.interface, &samplerInfo, NULL, &(texture.sampler));
        LOG_ASSERT(result == VK_SUCCESS, "Failed to create texture sampler");
    }

    texture.id = g_texture_id;
    g_texture_id++;
    ARRLIST_VulkanTexture_add(&(g_renderer.vulkan.geometry.textures), texture);

    if (g_renderer.vulkan.geometry.textures.size > 1) UpdateDescriptorSets();

    return texture.id;
}

void RemoveTexture(TextureID id) {
    for (size_t i = 0; i < g_renderer.vulkan.geometry.textures.size; i++) {
        VulkanTexture texture = ARRLIST_VulkanTexture_get(&(g_renderer.vulkan.geometry.textures), i);
        if (texture.id == id) {
            vkDestroyImage(g_renderer.vulkan.core.general.interface, texture.image.image, NULL);
            vkFreeMemory(g_renderer.vulkan.core.general.interface, texture.image.memory, NULL);
            vkDestroyImageView(g_renderer.vulkan.core.general.interface, texture.image.view, NULL);
            vkDestroySampler(g_renderer.vulkan.core.general.interface, texture.sampler, NULL);
            ARRLIST_VulkanTexture_remove(&(g_renderer.vulkan.geometry.textures), i);
            return;
        }
    }
    LOG_FATAL("Texture could not be removed because it does not exist");
}

void ClearTextures() {
    for (size_t i = 0; i < g_renderer.vulkan.geometry.textures.size; i++) {
        VulkanTexture texture = ARRLIST_VulkanTexture_get(&(g_renderer.vulkan.geometry.textures), i);
        vkDestroyImage(g_renderer.vulkan.core.general.interface, texture.image.image, NULL);
        vkFreeMemory(g_renderer.vulkan.core.general.interface, texture.image.memory, NULL);
        vkDestroyImageView(g_renderer.vulkan.core.general.interface, texture.image.view, NULL);
        vkDestroySampler(g_renderer.vulkan.core.general.interface, texture.sampler, NULL);
    }
    ARRLIST_VulkanTexture_clear(&(g_renderer.vulkan.geometry.textures));
}

void Render() {
    // if there's no geometry, don't render
    if (g_renderer.geometry.vertices.size == 0) return;

    // profile for stats
    BeginProfile(&(g_renderer.stats.profile));

    // recreate vertex and index buffer if needed
    if (g_renderer.geometry.changes.update_indices) {
        vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface); // TODO: make a buffer for every swap so we don't have to wait
        g_renderer.geometry.changes.update_indices = FALSE;
        if (g_renderer.geometry.changes.max_indices != g_renderer.geometry.indices.maxsize) {
            g_renderer.geometry.changes.max_indices = g_renderer.geometry.indices.maxsize;
            vkDestroyBuffer(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.indices.buffer, NULL);
            vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.indices.memory, NULL);
			VINIT_Indices(&(g_renderer.vulkan.geometry.indices));
        } else {
			VUPDT_Indices(&(g_renderer.vulkan.geometry.indices));
        }
    }
    if (g_renderer.geometry.changes.update_vertices) {
        vkDeviceWaitIdle(g_renderer.vulkan.core.general.interface); // TODO: make a buffer for every swap so we don't have to wait
        g_renderer.geometry.changes.update_vertices = FALSE;
        if (g_renderer.geometry.changes.max_vertices != g_renderer.geometry.vertices.maxsize) {
            g_renderer.geometry.changes.max_vertices = g_renderer.geometry.vertices.maxsize;
            vkDestroyBuffer(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.vertices.buffer, NULL);
            vkFreeMemory(g_renderer.vulkan.core.general.interface, g_renderer.vulkan.geometry.vertices.memory, NULL);
			VINIT_Vertices(&(g_renderer.vulkan.geometry.vertices));
        } else {
			VUPDT_Vertices(&(g_renderer.vulkan.geometry.vertices));
        }
    }

    // check for window changes
    if (g_renderer.dimensions.x != GetScreenWidth() || g_renderer.dimensions.y != GetScreenHeight()) RecreateSwapchain();

    // update uniform buffers
    UpdateUniformBuffers();

    // reset command buffer and record it
    vkResetCommandBuffer(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index], 0);
    RecordCommand(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index]);

    // submit command buffer
    VkSubmitInfo submitInfo = { 0 };
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &(g_renderer.vulkan.core.scheduler.commands.commands[g_renderer.swapchain.index]);
    submitInfo.signalSemaphoreCount = 0;
    VkResult result = vkQueueSubmit(g_renderer.vulkan.core.scheduler.queue, 1, &submitInfo, g_renderer.vulkan.core.scheduler.syncro.fences[g_renderer.swapchain.index]);
    LOG_ASSERT(result == VK_SUCCESS, "failed to submit draw command buffer!");

    // wait for and reset rendering fence
	g_renderer.swapchain.index = (g_renderer.swapchain.index + 1) % CPUSWAP_LENGTH;
    vkWaitForFences(g_renderer.vulkan.core.general.interface, 1, &(g_renderer.vulkan.core.scheduler.syncro.fences[g_renderer.swapchain.index]), VK_TRUE, UINT64_MAX);
    vkResetFences(g_renderer.vulkan.core.general.interface, 1, &(g_renderer.vulkan.core.scheduler.syncro.fences[g_renderer.swapchain.index]));

    // update render target
    glBindTexture(GL_TEXTURE_2D, g_renderer.swapchain.targets[g_renderer.swapchain.index].texture.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_renderer.dimensions.x, g_renderer.dimensions.y, GL_RGBA, GL_UNSIGNED_BYTE, g_renderer.swapchain.reference);
    glBindTexture(GL_TEXTURE_2D, 0);

    // end profiling
    EndProfile(&(g_renderer.stats.profile));
}

void Draw(float x, float y, float w, float h) {
	size_t index = (g_renderer.swapchain.index + 1) % CPUSWAP_LENGTH;
    DrawTexturePro(
        g_renderer.swapchain.targets[index].texture,
        (Rectangle){
            (g_renderer.swapchain.targets[index].texture.width / 2.0f) - (w/2.0f),
            (g_renderer.swapchain.targets[index].texture.height / 2.0f) - (h/2.0f),
            w,
            h },
        (Rectangle){ x, y, w, h},
        (Vector2){ 0, 0 },
        0.0f,
        WHITE);
}

float RenderTime() {
    return ProfileResult(&(g_renderer.stats.profile));
}
