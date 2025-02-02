#include "vinit.h"
#include "core/log.h"
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vupdate.h"
#include <GLFW/glfw3.h>

Renderer* g_vinit_renderer_ref = NULL;

BOOL VINIT_Queue(VkQueue* queue) {
	VulkanFamilyGroup families = VUTIL_FindQueueFamilies(g_vinit_renderer_ref->vulkan.core.general.gpu);
    vkGetDeviceQueue(
		g_vinit_renderer_ref->vulkan.core.general.interface,
		families.graphics.value,
		0,
		queue);
    return TRUE;
}

BOOL VINIT_Commands(VulkanCommands* commands) {
	// create command pool
    VulkanFamilyGroup queueFamilyIndices = VUTIL_FindQueueFamilies(g_vinit_renderer_ref->vulkan.core.general.gpu);
    VkCommandPoolCreateInfo poolInfo = { 0 };
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphics.value;
    VkResult result = vkCreateCommandPool(
		g_vinit_renderer_ref->vulkan.core.general.interface,
		&poolInfo, NULL, &(commands->pool));
    if (result != VK_SUCCESS) {
		LOG_FATAL("Failed to create command pool!");
		return FALSE;
	}

	// create command buffers
	VkCommandBufferAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commands->pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = CPUSWAP_LENGTH;
    result = vkAllocateCommandBuffers(
		g_vinit_renderer_ref->vulkan.core.general.interface,
		&allocInfo,
		commands->commands);
    if (result != VK_SUCCESS) {
		LOG_FATAL("Failed to create command buffer");
		return FALSE;
	}

    return TRUE;
}

BOOL VINIT_Syncro(VulkanSyncro* syncro) {
	for (int i = 0; i < CPUSWAP_LENGTH; i++) {
        VkFenceCreateInfo fenceInfo = { 0 };
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (i != 0) fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkResult result = vkCreateFence(
			g_vinit_renderer_ref->vulkan.core.general.interface,
			&fenceInfo, NULL, &(syncro->fences[i]));
        if (result != VK_SUCCESS) {
			LOG_FATAL("Failed to create fence");
			return FALSE;
		}
    }
	return TRUE;
}

BOOL VINIT_UniformBuffers(UBOArray* ubos) {
    VkDeviceSize size = sizeof(UniformBufferObject);
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_CreateBuffer(
            size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &(ubos->objects[i]));

        vkMapMemory(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            ubos->objects[i].memory,
            0, size, 0, &(ubos->mapped[i]));
    }
    return TRUE;
}

BOOL VINIT_Descriptors(VulkanDescriptors* descriptors) {
    // create descriptor set layout
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

    VkResult result = vkCreateDescriptorSetLayout(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        &layoutInfo, NULL, &(descriptors->layout));
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create descriptor set layout!");
        return FALSE;
    }

    // create descriptor pool
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
    result = vkCreateDescriptorPool(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        &poolInfo, NULL, &(descriptors->pool));
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create descriptor pool!");
        return FALSE;
    }

    // create descriptor sets
    VkDescriptorSetLayout layouts[CPUSWAP_LENGTH];
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) layouts[i] = descriptors->layout;
    VkDescriptorSetAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptors->pool;
    allocInfo.descriptorSetCount = CPUSWAP_LENGTH;
    allocInfo.pSetLayouts = layouts;
    result = vkAllocateDescriptorSets(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        &allocInfo, descriptors->sets);
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create descriptor sets!");
        return FALSE;
    }

    return TRUE;
}

BOOL VINIT_ComputeDescriptors(VulkanDescriptors* descriptors) {
    // create descriptor set layout
    VkDescriptorSetLayoutBinding uboLayoutBinding = { 0 };
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    uboLayoutBinding.pImmutableSamplers = NULL;

    VkDescriptorSetLayoutBinding ssboLayoutBinding = { 0 };
    ssboLayoutBinding.binding = 1;
    ssboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssboLayoutBinding.descriptorCount = 1;
    ssboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ssboLayoutBinding.pImmutableSamplers = NULL;

    VkDescriptorSetLayoutBinding imageLayoutBinding = { 0 };
    imageLayoutBinding.binding = 2;
    imageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    imageLayoutBinding.descriptorCount = 1;
    imageLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    imageLayoutBinding.pImmutableSamplers = NULL;

    VkDescriptorSetLayoutBinding bindings[] = { uboLayoutBinding, ssboLayoutBinding, imageLayoutBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo = { 0 };
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    VkResult result = vkCreateDescriptorSetLayout(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        &layoutInfo, NULL, &(descriptors->layout));
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create descriptor set layout!");
        return FALSE;
    }

    // create descriptor pool
    VkDescriptorPoolSize poolSizes[3] = { 0 };
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = CPUSWAP_LENGTH;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = CPUSWAP_LENGTH;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[2].descriptorCount = CPUSWAP_LENGTH;

    VkDescriptorPoolCreateInfo poolInfo = { 0 };
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = CPUSWAP_LENGTH;
    result = vkCreateDescriptorPool(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        &poolInfo, NULL, &(descriptors->pool));
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create descriptor pool!");
        return FALSE;
    }

    // create descriptor sets
    VkDescriptorSetLayout layouts[CPUSWAP_LENGTH];
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) layouts[i] = descriptors->layout;
    VkDescriptorSetAllocateInfo allocInfo = { 0 };
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptors->pool;
    allocInfo.descriptorSetCount = CPUSWAP_LENGTH;
    allocInfo.pSetLayouts = layouts;
    result = vkAllocateDescriptorSets(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        &allocInfo, descriptors->sets);
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create descriptor sets!");
        return FALSE;
    }

    VUPDT_ComputeDescriptorSets(descriptors);
    return TRUE;
}

BOOL VINIT_Attachments(VulkanAttachments* attachments) {
    // create target
    VUTIL_CreateImage(
        g_vinit_renderer_ref->dimensions.x,
        g_vinit_renderer_ref->dimensions.y,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        IMAGE_FORMAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        &(attachments->target));

	// create color
    VUTIL_CreateImage(
        g_vinit_renderer_ref->dimensions.x,
        g_vinit_renderer_ref->dimensions.y,
        1,
        g_vinit_renderer_ref->vulkan.core.context.samples,
        IMAGE_FORMAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        &(attachments->color));
	
	// create depth
    VkFormat depthFormat = VUTIL_FindDepthFormat();
    VUTIL_CreateImage(
        g_vinit_renderer_ref->dimensions.x,
        g_vinit_renderer_ref->dimensions.y,
        1,
        g_vinit_renderer_ref->vulkan.core.context.samples,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        &(attachments->depth));

	return TRUE;
}

BOOL VINIT_Framebuffers(VkFramebuffer* framebuffer) {
    VkImageView attachments[] = { 
        g_vinit_renderer_ref->vulkan.core.context.attachments.color.view,
        g_vinit_renderer_ref->vulkan.core.context.attachments.depth.view,
        g_vinit_renderer_ref->vulkan.core.context.attachments.target.view };
    VkFramebufferCreateInfo framebufferInfo = { 0 };
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = g_vinit_renderer_ref->vulkan.core.context.renderpass;
    framebufferInfo.attachmentCount = 3;
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = g_vinit_renderer_ref->dimensions.x;
    framebufferInfo.height = g_vinit_renderer_ref->dimensions.y;
    framebufferInfo.layers = 1;
    VkResult result = vkCreateFramebuffer(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        &framebufferInfo, NULL, framebuffer);
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create framebuffers");
        return FALSE;
    }
    return TRUE;
}

BOOL VINIT_RenderData(VulkanRenderData* renderdata) {
	if (!VINIT_UniformBuffers(&(renderdata->ubos))) return FALSE;
	if (!VINIT_Descriptors(&(renderdata->descriptors))) return FALSE;
    return TRUE;
}

BOOL VINIT_RenderPass(VkRenderPass* renderpass) {
	VkAttachmentDescription colorAttachment = { 0 };
    colorAttachment.format = IMAGE_FORMAT;
    colorAttachment.samples = g_vinit_renderer_ref->vulkan.core.context.samples;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef = { 0 };
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment = { 0 };
    depthAttachment.format = VUTIL_FindDepthFormat();
    depthAttachment.samples = g_vinit_renderer_ref->vulkan.core.context.samples;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = { 0 };
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorAttachmentResolve = { 0 };
    colorAttachmentResolve.format = IMAGE_FORMAT;
    colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkAttachmentReference colorAttachmentResolveRef = { 0 };
    colorAttachmentResolveRef.attachment = 2;
    colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = { 0 };
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    subpass.pResolveAttachments = &colorAttachmentResolveRef;

    VkSubpassDependency dependency = { 0 };
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment, colorAttachmentResolve };
    VkRenderPassCreateInfo renderPassInfo = { 0 };
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 3;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(g_vinit_renderer_ref->vulkan.core.general.interface, &renderPassInfo, NULL, renderpass);
    if (result != VK_SUCCESS) {
		LOG_FATAL("Unable to create render pass");
		return FALSE;
	}

    return TRUE;
}

BOOL VINIT_Pipeline(VulkanPipeline* pipeline) {
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
    multisampling.rasterizationSamples = g_vinit_renderer_ref->vulkan.core.context.samples;
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
    pipelineLayoutInfo.pSetLayouts = &(g_vinit_renderer_ref->vulkan.core.context.renderdata.descriptors.layout);
    pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
    pipelineLayoutInfo.pPushConstantRanges = NULL; // Optional

    VkResult result = vkCreatePipelineLayout(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        &pipelineLayoutInfo, NULL, &(pipeline->layout));
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create pipeline layout!");
        return FALSE;
    }

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
    pipelineInfo.layout = pipeline->layout;
    pipelineInfo.renderPass = g_vinit_renderer_ref->vulkan.core.context.renderpass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
    pipelineInfo.basePipelineIndex = -1; // Optional

    result = vkCreateGraphicsPipelines(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &(pipeline->pipeline));
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create pipeline!");
        return FALSE;
    }

	FreeFile(vertshadercode);
	FreeFile(fragshadercode);
    FreeFile(compshadercode);
	vkDestroyShaderModule(g_vinit_renderer_ref->vulkan.core.general.interface, vertshader, NULL);
	vkDestroyShaderModule(g_vinit_renderer_ref->vulkan.core.general.interface, fragshader, NULL);
	vkDestroyShaderModule(g_vinit_renderer_ref->vulkan.core.general.interface, compshader, NULL);
    return TRUE;
}

BOOL VINIT_Scheduler(VulkanScheduler* scheduler) {
	// create syncro
	if (!VINIT_Syncro(&(scheduler->syncro))) return FALSE;

	// create commands
	if (!VINIT_Commands(&(scheduler->commands))) return FALSE;	

	// create queue
	return VINIT_Queue(&(scheduler->queue));
}

BOOL VINIT_Bridge(VulkanDataBuffer* bridge) {
    // create cross buffer
    VUTIL_CreateBuffer(
        g_vinit_renderer_ref->dimensions.x * g_vinit_renderer_ref->dimensions.y * 4, // Assuming VK_FORMAT_B8G8R8A8_SRGB
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        bridge);

    // map memory to buffer
    vkMapMemory(g_vinit_renderer_ref->vulkan.core.general.interface, bridge->memory, 0, VK_WHOLE_SIZE, 0, &(g_vinit_renderer_ref->swapchain.reference));
    return TRUE;
}

BOOL VINIT_Raytracer(VulkanRaytracer* raytracer) {
    // initialize ssbos
    uint32_t imgw = (uint32_t)g_vinit_renderer_ref->dimensions.x;
    uint32_t imgh = (uint32_t)g_vinit_renderer_ref->dimensions.y;
    RayGenerator* raygens = EZALLOC(imgw * imgh, sizeof(RayGenerator));
    for (uint32_t x = 0; x < imgw; x++) {
        for (uint32_t y = 0; y < imgh; y++) {
            raygens[y*imgh + x].x = x;
            raygens[y*imgh + x].y = y;
        }
    }

    VkDeviceSize bufferSize = sizeof(RayGenerator) * imgw * imgh;
    VulkanDataBuffer stagingBuffer;
    VUTIL_CreateBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer);
    void* data;
    vkMapMemory(g_vinit_renderer_ref->vulkan.core.general.interface, stagingBuffer.memory, 0, bufferSize, 0, &data);
    memcpy(data, raygens, (size_t)bufferSize);
    vkUnmapMemory(g_vinit_renderer_ref->vulkan.core.general.interface, stagingBuffer.memory);

    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_CreateBuffer(
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &(raytracer->ssbos[i]));
        VUTIL_CopyBuffer(stagingBuffer.buffer, raytracer->ssbos[i].buffer, bufferSize);
    }

    vkDestroyBuffer(g_vinit_renderer_ref->vulkan.core.general.interface, stagingBuffer.buffer, NULL);
    vkFreeMemory(g_vinit_renderer_ref->vulkan.core.general.interface, stagingBuffer.memory, NULL);
    EZFREE(raygens);

    // initialize targets
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_CreateImage(
            g_vinit_renderer_ref->dimensions.x,
            g_vinit_renderer_ref->dimensions.y,
            1,
            VK_SAMPLE_COUNT_1_BIT,
            IMAGE_FORMAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            &(raytracer->targets[i]));
    }

    // initialize descriptors
    VINIT_ComputeDescriptors(&(raytracer->descriptors));

    return TRUE;
}

BOOL VINIT_RenderContext(VulkanRenderContext* context) {
    context->samples = VUTIL_GetMaximumSampleCount();
	if (!VINIT_Attachments(&(context->attachments))) return FALSE;
	if (!VINIT_RenderPass(&(context->renderpass))) return FALSE;
	if (!VINIT_RenderData(&(context->renderdata))) return FALSE;
	if (!VINIT_Framebuffers(&(context->framebuffer))) return FALSE;
	if (!VINIT_Pipeline(&(context->pipeline))) return FALSE;
	if (!VINIT_Raytracer(&(context->raytracer))) return FALSE;
    return TRUE;
}

BOOL VINIT_General(VulkanGeneral* general) {
	// error check for validation layer support
    if (!(ENABLE_VK_VALIDATION_LAYERS && VUTIL_CheckValidationLayerSupport())) {
		LOG_FATAL("Requested validation layers are not available");
		return FALSE;
	}

    // create app info (technically optional)
    VkApplicationInfo appInfo = { 0 };
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Prism Renderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // create info
    VkInstanceCreateInfo createInfo = { 0 };
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = (uint32_t)(g_vinit_renderer_ref->vulkan.metadata.extensions.required.size);
    createInfo.ppEnabledExtensionNames = g_vinit_renderer_ref->vulkan.metadata.extensions.required.data;
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = { 0 };
    if (ENABLE_VK_VALIDATION_LAYERS) {
        createInfo.enabledLayerCount = g_vinit_renderer_ref->vulkan.metadata.validation.size;
        createInfo.ppEnabledLayerNames = g_vinit_renderer_ref->vulkan.metadata.validation.data;

        // set up additional debug callback for the creation of the instance
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = VUTIL_VulkanDebugCallback;
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    // create instance
    VkResult result = vkCreateInstance(&createInfo, NULL, &(general->instance));
    if (result != VK_SUCCESS) {
		LOG_FATAL("Failed to create vulkan instance");
		return FALSE;
	}

	// create validation messenger
    if (ENABLE_VK_VALIDATION_LAYERS) {
		VkDebugUtilsMessengerCreateInfoEXT createInfo = { 0 };
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = VUTIL_VulkanDebugCallback;
		createInfo.pUserData = NULL;
		PFN_vkCreateDebugUtilsMessengerEXT messenger_extension = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(general->instance, "vkCreateDebugUtilsMessengerEXT");
		if (messenger_extension == NULL) {
			LOG_FATAL("Failed to set up debug messenger");
			return FALSE;
		}
		messenger_extension(general->instance, &createInfo, NULL, &(general->messenger));
	}

	// pick gpu
	uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(general->instance, &deviceCount, NULL);
    if (deviceCount == 0) {
		LOG_FATAL("No devices with vulkan support were found");
		return FALSE;
	}
    VkPhysicalDevice* devices = EZALLOC(deviceCount, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(general->instance, &deviceCount, devices);
    uint32_t score = 0;
    uint32_t ind = 0;
    for (uint32_t i = 0; i < deviceCount; i++) {
        // check if device is suitable
        VulkanFamilyGroup families = VUTIL_FindQueueFamilies(devices[i]);
        if (!families.graphics.exists) continue;
        if (!VUTIL_CheckGPUExtensionSupport(devices[i])) continue;

        uint32_t curr_score = 0;
        VkPhysicalDeviceProperties deviceProperties;
        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceProperties(devices[i], &deviceProperties);
        vkGetPhysicalDeviceFeatures(devices[i], &deviceFeatures);
        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) curr_score += 1000;
        curr_score += deviceProperties.limits.maxImageDimension2D;
        if (!deviceFeatures.geometryShader) curr_score = 0;
        if (!deviceFeatures.samplerAnisotropy) curr_score = 0;
        if (curr_score > score) {
            score = curr_score;
            ind = i;
        }
    }
    if (score == 0) {
		LOG_FATAL("A suitable GPU could not be found");
		return FALSE;
	}
    general->gpu = devices[ind];
    EZFREE(devices);

	// create device interface
	VulkanFamilyGroup families = VUTIL_FindQueueFamilies(general->gpu);
    VkDeviceQueueCreateInfo queueCreateInfo = { 0 };
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = families.graphics.value;
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    VkPhysicalDeviceFeatures deviceFeatures = { 0 };
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.sampleRateShading = VK_TRUE;
    VkDeviceCreateInfo deviceCreateInfo = { 0 };
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = g_vinit_renderer_ref->vulkan.metadata.extensions.device.size;
    deviceCreateInfo.ppEnabledExtensionNames  = g_vinit_renderer_ref->vulkan.metadata.extensions.device.data;
    if (ENABLE_VK_VALIDATION_LAYERS) {
        deviceCreateInfo.enabledLayerCount = g_vinit_renderer_ref->vulkan.metadata.validation.size;
        deviceCreateInfo.ppEnabledLayerNames = g_vinit_renderer_ref->vulkan.metadata.validation.data;
    } else {
        deviceCreateInfo.enabledLayerCount = 0;
    }
    result = vkCreateDevice(general->gpu, &deviceCreateInfo, NULL, &(general->interface));
	if (result != VK_SUCCESS) {
		LOG_FATAL("Failed to create logical device");
		return FALSE;
	}

    return TRUE;
}

BOOL VINIT_Vertices(VulkanDataBuffer* vertices) {
    VkDeviceSize size = sizeof(Vertex) * g_vinit_renderer_ref->geometry.vertices.maxsize;
    if (size == 0) return TRUE;
    VUTIL_CreateBuffer(
        size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vertices);
	VUPDT_Vertices(vertices);
    return TRUE;
}

BOOL VINIT_Indices(VulkanDataBuffer* indices) {
    VkDeviceSize size = sizeof(Index) * g_vinit_renderer_ref->geometry.indices.maxsize;
    if (size == 0) return TRUE;
    VUTIL_CreateBuffer(
        size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        indices);
	VUPDT_Indices(indices);
    return TRUE;
}

BOOL VINIT_Geometry(VulkanGeometry* geometry) {
	if (!VINIT_Vertices(&(geometry->vertices))) return FALSE;
	if (!VINIT_Indices(&(geometry->indices))) return FALSE;
    return TRUE;
}

BOOL VINIT_Metadata(VulkanMetadata* metadata) {
	// set up validation layers
    ARRLIST_StaticString_add(&(metadata->validation), "VK_LAYER_KHRONOS_validation");

    // set up required extensions
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    for (size_t i = 0; i < glfwExtensionCount; i++)
        ARRLIST_StaticString_add(&(metadata->extensions.required), glfwExtensions[i]);
    if (ENABLE_VK_VALIDATION_LAYERS) {
        ARRLIST_StaticString_add(&(metadata->extensions.required), VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // set up device extensions
    ARRLIST_StaticString_add(&(metadata->extensions.device), VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    return TRUE;
}

BOOL VINIT_Core(VulkanCore* core) {
	if (!VINIT_General(&(core->general))) return FALSE;
	if (!VINIT_Scheduler(&(core->scheduler))) return FALSE;
	if (!VINIT_Bridge(&(core->bridge))) return FALSE;
	if (!VINIT_RenderContext(&(core->context))) return FALSE;
    return TRUE;
}

BOOL VINIT_Vulkan(VulkanObject* vulkan) {
	if (!VINIT_Metadata(&(vulkan->metadata))) return FALSE;
	if (!VINIT_Geometry(&(vulkan->geometry))) return FALSE;
	if (!VINIT_Core(&(vulkan->core))) return FALSE;
    return TRUE;
}

void VINIT_SetVulkanInitContext(Renderer* renderer) {
	g_vinit_renderer_ref = renderer;
}
