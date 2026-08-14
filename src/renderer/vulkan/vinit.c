#include "vinit.h"
#include <easylogger.h>
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vupdate.h"
#include "renderer/vulkan/vshaders.h"
#include <GLFW/glfw3.h>

static Renderer* g_vinit_renderer_ref = NULL;

BOOL VINIT_Shaders(ARRLIST_VulkanShaderPtr* shaders) {
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/vertex.comp",
        "shaders/vertex.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/centroids.comp",
        "shaders/centroids.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/histogram.comp",
        "shaders/histogram.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/history.comp",
        "shaders/history.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/scatter.comp",
        "shaders/scatter.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/leaves.comp",
        "shaders/leaves.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/bvh.comp",
        "shaders/bvh.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/rebind.comp",
        "shaders/rebind.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/default.comp",
        "shaders/default.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/path.comp",
        "shaders/path.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/fluid.comp",
        "shaders/fluid.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/tonemap.comp",
        "shaders/tonemap.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/analyze.comp",
        "shaders/analyze.comp.spv"));
	ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
        g_vinit_renderer_ref,
        "expanded/overlay.comp",
        "shaders/overlay.comp.spv"));
    for (size_t i = 0; i < g_vinit_renderer_ref->externals.size; i++) {
        ARRLIST_VulkanShaderPtr_add(shaders, GenerateShader(
            g_vinit_renderer_ref,
            g_vinit_renderer_ref->externals.data[i].location,
            g_vinit_renderer_ref->externals.data[i].binary
        ));
    }
	return TRUE;
}

BOOL VINIT_ExtendedBuffers(ARRLIST_ShaderBuffer* ebuffers) {
    for (size_t i = 0; i < ebuffers->size; i++) {
        size_t arrsize = ebuffers->data[i].size;
        ebuffers->data[i].vbuffersize = arrsize;
        VUTIL_CreateBuffer(
            arrsize > 0 ? arrsize : 1,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &(ebuffers->data[i].vbuffer));
        if (arrsize > 0) {
            VUTIL_CopyHostToBuffer(
                ebuffers->data[i].data,
                arrsize, arrsize,
                ebuffers->data[i].vbuffer.buffer);
        }
    }
    return TRUE;
}

BOOL VINIT_OverlaySSBOs(VulkanDataBuffer* ssbo_array) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
	    VUTIL_CreateBuffer(
		    sizeof(OverlaySSBO),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		    &(ssbo_array[i]));
    }
	return TRUE;
}

BOOL VINIT_Transforms(VulkanDataBuffer* transforms) {
    size_t arrsize = sizeof(MeshDescriptor) * g_vinit_renderer_ref->geometry.meshes.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        transforms);
    VUPDT_Transforms(transforms);
    return TRUE;
}

BOOL VINIT_Skins(VulkanDataBuffer* skins) {
    size_t arrsize = sizeof(VertexSkin) * g_vinit_renderer_ref->geometry.skins.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        skins);
    VUPDT_Skins(skins);
    return TRUE;
}

BOOL VINIT_Poses(VulkanDataBuffer* poses) {
    size_t arrsize = sizeof(mat4) * g_vinit_renderer_ref->geometry.poses.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        poses);
    VUPDT_Poses(poses);
    return TRUE;
}

BOOL VINIT_Lights(VulkanDataBuffer* lights) {
    size_t arrsize = sizeof(SceneLight) * g_vinit_renderer_ref->geometry.lights.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        lights);
    VUPDT_Lights(lights);
    return TRUE;
}

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
		EZ_FATAL("Failed to create command pool!");
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
		EZ_FATAL("Failed to create command buffer");
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
			EZ_FATAL("Failed to create fence");
			return FALSE;
		}
    }
	return TRUE;
}

BOOL VINIT_UniformBuffers(UBOArray* ubos) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_CreateBuffer(
            sizeof(UniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &(ubos->objects[i]));
        VUTIL_CreateBuffer(
            sizeof(OverlayUniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &(ubos->overlay_objects[i]));
        VUTIL_CreateBuffer(
            sizeof(GeometryUniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &(ubos->geometry_objects[i]));
        VUTIL_CreateBuffer(
            sizeof(SimulationUniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &(ubos->simulation_objects[i]));
        vkMapMemory(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            ubos->objects[i].memory,
            0, sizeof(UniformBufferObject), 0, &(ubos->mapped[i]));
        vkMapMemory(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            ubos->overlay_objects[i].memory,
            0, sizeof(OverlayUniformBufferObject), 0, &(ubos->overlay_mapped[i]));
        vkMapMemory(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            ubos->geometry_objects[i].memory,
            0, sizeof(GeometryUniformBufferObject), 0, &(ubos->geometry_mapped[i]));
        vkMapMemory(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            ubos->simulation_objects[i].memory,
            0, sizeof(SimulationUniformBufferObject), 0, &(ubos->simulation_mapped[i]));
    }
    return TRUE;
}

BOOL VINIT_Descriptors(VulkanDescriptors* descriptors) {
    size_t num_shaders = g_vinit_renderer_ref->vulkan.core.shaders.size;
    for (size_t i = 0; i < num_shaders; i++) {
        VulkanShader* shader = g_vinit_renderer_ref->vulkan.core.shaders.data[i];
        size_t vars = shader->variables[0].size;
        VkDescriptorPoolSize* poolSizes = EZ_ALLOC(vars, sizeof(VkDescriptorPoolSize));
        VkDescriptorSetLayoutBinding* bindings = EZ_ALLOC(vars, sizeof(VkDescriptorSetLayoutBinding));
        for (size_t j = 0; j < vars; j++) {
            bindings[j].binding = j;
            bindings[j].descriptorType = (VkDescriptorType)(shader->variables[0].data[j].type);
            bindings[j].descriptorCount = 1;
            bindings[j].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            poolSizes[j].type = (VkDescriptorType)(shader->variables[0].data[j].type);
            poolSizes[j].descriptorCount = CPUSWAP_LENGTH;
        }
        VkDescriptorSetLayoutCreateInfo layoutInfo = { 0 };
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = vars;
        layoutInfo.pBindings = bindings;
        VkResult result = vkCreateDescriptorSetLayout(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            &layoutInfo, NULL, &(descriptors[i].layout));
        if (result != VK_SUCCESS) {
            EZ_FATAL("Failed to create descriptor set layout!");
            return FALSE;
        }
        VkDescriptorPoolCreateInfo poolInfo = { 0 };
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = vars;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = CPUSWAP_LENGTH;
        result = vkCreateDescriptorPool(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            &poolInfo, NULL, &(descriptors[i].pool));
        if (result != VK_SUCCESS) {
            EZ_FATAL("Failed to create descriptor pool!");
            return FALSE;
        }
        VkDescriptorSetLayout layouts[CPUSWAP_LENGTH];
        for (size_t k = 0; k < CPUSWAP_LENGTH; k++) layouts[k] = descriptors[i].layout;
        VkDescriptorSetAllocateInfo allocInfo = { 0 };
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptors[i].pool;
        allocInfo.descriptorSetCount = CPUSWAP_LENGTH;
        allocInfo.pSetLayouts = layouts;
        result = vkAllocateDescriptorSets(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            &allocInfo, descriptors[i].sets);
        if (result != VK_SUCCESS) {
            EZ_FATAL("Failed to create descriptor sets!");
            return FALSE;
        }
        EZ_FREE(poolSizes);
        EZ_FREE(bindings);
    }
    VUPDT_DescriptorSetsAll(descriptors);
    return TRUE;
}

BOOL VINIT_ShaderStorageBuffers(VulkanDataBuffer* ssbo_array) {
    uint32_t imgw = (uint32_t)g_vinit_renderer_ref->dimensions.x;
    uint32_t imgh = (uint32_t)g_vinit_renderer_ref->dimensions.y;
    RayGenerator* raygens = EZ_ALLOC(imgw * imgh, sizeof(RayGenerator));
    for (size_t i = 0; i < imgw * imgh; i++) raygens[i].tid = (uint32_t)-1;

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
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &(ssbo_array[i]));
        VUTIL_CopyBuffer(stagingBuffer.buffer, ssbo_array[i].buffer, bufferSize);
    }

    vkDestroyBuffer(g_vinit_renderer_ref->vulkan.core.general.interface, stagingBuffer.buffer, NULL);
    vkFreeMemory(g_vinit_renderer_ref->vulkan.core.general.interface, stagingBuffer.memory, NULL);
    EZ_FREE(raygens);
    return TRUE;
}

BOOL VINIT_RenderData(VulkanRenderData* renderdata) {
    renderdata->descriptors = EZ_ALLOC(g_vinit_renderer_ref->vulkan.core.shaders.size, sizeof(VulkanDescriptors));
	if (!VINIT_ShaderStorageBuffers(renderdata->ssbos)) return FALSE;
	if (!VINIT_UniformBuffers(&(renderdata->ubos))) return FALSE;
	if (!VINIT_OverlaySSBOs(renderdata->overlay_ssbos)) return FALSE;
	if (!VINIT_OverlayBridge(&(renderdata->overlay_bridge))) return FALSE;
	if (!VINIT_Descriptors(renderdata->descriptors)) return FALSE;
    return TRUE;
}

BOOL VINIT_Pipeline(VulkanPipeline* pipeline) {
    pipeline->pipeline = EZ_ALLOC(g_vinit_renderer_ref->vulkan.core.shaders.size, sizeof(VkPipeline));
    pipeline->layout = EZ_ALLOC(g_vinit_renderer_ref->vulkan.core.shaders.size, sizeof(VkPipelineLayout));
    size_t num_shaders = g_vinit_renderer_ref->vulkan.core.shaders.size;
    VkShaderModule* shadermodules = EZ_ALLOC(num_shaders, sizeof(VkShaderModule));
    VkComputePipelineCreateInfo* pipelineInfos = EZ_ALLOC(num_shaders, sizeof(VkComputePipelineCreateInfo));
    for (size_t i = 0; i < num_shaders; i++) {
        VulkanShader* shader = g_vinit_renderer_ref->vulkan.core.shaders.data[i];
        SimpleFile* shadercode = ReadSimpleFile(shader->filename);
        shadermodules[i] = VUTIL_CreateShader(shadercode);
        FreeFile(shadercode);

        VkPipelineShaderStageCreateInfo createInfo = { 0 };
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        createInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        createInfo.module = shadermodules[i];
        createInfo.pName = "main";

        VkPushConstantRange pushRange = { 0 };
        pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(VulkanPushConstants);

        VkPipelineLayoutCreateInfo layoutInfo = { 0 };
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &(g_vinit_renderer_ref->vulkan.core.context.renderdata.descriptors[i].layout);
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
        
        VkResult result = vkCreatePipelineLayout(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            &(layoutInfo), NULL, &(pipeline->layout[i]));
        if (result != VK_SUCCESS) {
            EZ_FATAL("Failed to create pipeline layout!");
            return FALSE;
        }

        pipelineInfos[i].sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfos[i].layout = pipeline->layout[i];
        pipelineInfos[i].stage = createInfo;
    }

    VkResult result = vkCreateComputePipelines(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        VK_NULL_HANDLE, num_shaders, pipelineInfos, NULL, pipeline->pipeline);
    if (result != VK_SUCCESS) {
        EZ_FATAL("Failed to create pipeline!");
        return FALSE;
    }

    for (size_t i = 0; i < num_shaders; i++)
	    vkDestroyShaderModule(g_vinit_renderer_ref->vulkan.core.general.interface, shadermodules[i], NULL);
    EZ_FREE(shadermodules);
    EZ_FREE(pipelineInfos);

    return TRUE;
}

BOOL VINIT_Scheduler(VulkanScheduler* scheduler) {
	// create syncro
	if (!VINIT_Syncro(&(scheduler->syncro))) return FALSE;

	// create commands
	if (!VINIT_Commands(&(scheduler->commands))) return FALSE;

	// create queue
	if (!VINIT_Queue(&(scheduler->queue))) return FALSE;

    // create semaphore
    VkSemaphoreTypeCreateInfo typeInfo = { 0 };
    typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = 0;
    VkSemaphoreCreateInfo sci = { 0 };
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sci.pNext = &typeInfo;
    vkCreateSemaphore(g_vinit_renderer_ref->vulkan.core.general.interface, &sci, NULL, &scheduler->semaphore);
    scheduler->signal = 0;

    return TRUE;
}

BOOL VINIT_Transfer(VulkanTransfer* transfer) {
    VulkanFamilyGroup families = VUTIL_FindQueueFamilies(g_vinit_renderer_ref->vulkan.core.general.gpu);
    vkGetDeviceQueue(g_vinit_renderer_ref->vulkan.core.general.interface, families.transfer.value, 0, &transfer->queue);
    VkCommandPoolCreateInfo pi = { 0 };
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = families.transfer.value;
    vkCreateCommandPool(g_vinit_renderer_ref->vulkan.core.general.interface, &pi, NULL, &transfer->pool);
    VkCommandBufferAllocateInfo ai = { 0 };
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = transfer->pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = CPUSWAP_LENGTH;
    vkAllocateCommandBuffers(g_vinit_renderer_ref->vulkan.core.general.interface, &ai, transfer->commands);
    VkSemaphoreTypeCreateInfo sti = { 0 };
    sti.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    sti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    sti.initialValue = 0;
    VkSemaphoreCreateInfo sci = { 0 };
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sci.pNext = &sti;
    vkCreateSemaphore(g_vinit_renderer_ref->vulkan.core.general.interface, &sci, NULL, &transfer->semaphore);
    transfer->signal = 0;
    transfer->pending = 0;
    transfer->index = 0;
    transfer->offset = 0;
    VUTIL_CreateBuffer(
        INITIAL_TRANSFER_STAGE_SIZE,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &transfer->staging);
    vkMapMemory(g_vinit_renderer_ref->vulkan.core.general.interface,
        transfer->staging.memory, 0, INITIAL_TRANSFER_STAGE_SIZE, 0,
        &transfer->mapped);
    transfer->size = INITIAL_TRANSFER_STAGE_SIZE;
    return TRUE;
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

BOOL VINIT_OverlayBridge(VulkanDataBuffer* bridge) {
    // create cross buffer
    VUTIL_CreateBuffer(
        sizeof(OverlaySSBO),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        bridge);

    // map memory to buffer
    vkMapMemory(g_vinit_renderer_ref->vulkan.core.general.interface, bridge->memory, 0, VK_WHOLE_SIZE, 0, &(g_vinit_renderer_ref->vulkan.core.context.renderdata.overlay_mapped));
    return TRUE;
}

BOOL VINIT_RenderContext(VulkanRenderContext* context) {
    if (!VINIT_TargetsHDR(context->hdr)) return FALSE;
	if (!VINIT_Targets(context->targets)) return FALSE;
	if (!VINIT_RenderData(&(context->renderdata))) return FALSE;
	if (!VINIT_Pipeline(&(context->pipeline))) return FALSE;
    return TRUE;
}

BOOL VINIT_BVH(VulkanBVH* bvh) {
    // workgroup history
    size_t arrsize = 16 * sizeof(uint32_t) * ceil(g_vinit_renderer_ref->geometry.triangles.maxsize / (float)INVOCATION_GROUP_SIZE);
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(bvh->workhistory));

    // workgroup offsets
    arrsize = 16 * sizeof(uint32_t) * ceil(g_vinit_renderer_ref->geometry.triangles.maxsize / (float)INVOCATION_GROUP_SIZE);
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(bvh->workoffsets));

    // mortons
    arrsize = sizeof(uint32_t) * g_vinit_renderer_ref->geometry.triangles.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(bvh->mortons));

    // indices
    arrsize = sizeof(uint32_t) * g_vinit_renderer_ref->geometry.triangles.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(bvh->indices));

    // morton swap
    arrsize = sizeof(uint32_t) * g_vinit_renderer_ref->geometry.triangles.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(bvh->mortonswap));

    // index swap
    arrsize = sizeof(uint32_t) * g_vinit_renderer_ref->geometry.triangles.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(bvh->indexswap));

    // bounding boxes
    arrsize = sizeof(AxisAlignedBoundingBox) * g_vinit_renderer_ref->geometry.triangles.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(bvh->boundingboxes));

    // nodes
    arrsize = sizeof(BVHNode) * g_vinit_renderer_ref->geometry.triangles.maxsize * 2;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(bvh->nodes));

    // buckets
    arrsize = sizeof(uint32_t) * 16;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(bvh->buckets));

    return TRUE;
}

BOOL VINIT_Simulation(VulkanFluidSimulation* vfs) {
    VUTIL_CreateImage3D(
        g_vinit_renderer_ref->geometry.fluid.width + 2,
        g_vinit_renderer_ref->geometry.fluid.height + 2,
        g_vinit_renderer_ref->geometry.fluid.length + 2,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        &(vfs->density.image));
    vfs->density.sampler = VUTIL_CreateSampler3D();
    VUPDT_Simulation(vfs);
    return TRUE;
}

BOOL VINIT_Normals(VulkanDataBuffer* normals) {
    size_t arrsize = sizeof(vec4) * g_vinit_renderer_ref->geometry.normals.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        normals);
    VUPDT_Normals(normals);
    return TRUE;
}

BOOL VINIT_Vertices(VulkanVertices* vertices) {
    size_t arrsize = sizeof(vec4) * g_vinit_renderer_ref->geometry.vertices.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(vertices->original));
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &(vertices->transformed));
    VUPDT_Vertices(vertices);
    return TRUE;
}

BOOL VINIT_Triangles(VulkanDataBuffer* triangles) {
    size_t arrsize = sizeof(Triangle) * g_vinit_renderer_ref->geometry.triangles.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        triangles);
    VUPDT_Triangles(triangles);
    return TRUE;
}

BOOL VINIT_Emissives(VulkanDataBuffer* emissives) {
    size_t arrsize = sizeof(TriangleID) * g_vinit_renderer_ref->geometry.emissives.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        emissives);
    VUPDT_Emissives(emissives);
    return TRUE;
}

BOOL VINIT_Materials(VulkanDataBuffer* materials) {
    size_t arrsize = sizeof(SurfaceMaterial) * g_vinit_renderer_ref->geometry.materials.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        materials);
    VUPDT_Materials(materials);
    return TRUE;
}

BOOL VINIT_TargetsHDR(VulkanImage* hdr_arr) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_CreateImage(
            g_vinit_renderer_ref->dimensions.x,
            g_vinit_renderer_ref->dimensions.y,
            1,
            VK_SAMPLE_COUNT_1_BIT,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            &(hdr_arr[i]));
        VUTIL_TransitionImageLayout(
            hdr_arr[i].image,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            1);
    }
    return TRUE;
}

BOOL VINIT_Targets(VulkanImage* targets_arr) {
    for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
        VUTIL_CreateImage(
            g_vinit_renderer_ref->dimensions.x,
            g_vinit_renderer_ref->dimensions.y,
            1,
            VK_SAMPLE_COUNT_1_BIT,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            &(targets_arr[i]));
        VUTIL_TransitionImageLayout(
            targets_arr[i].image,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            1);
    }
    return TRUE;
}

BOOL VINIT_General(VulkanGeneral* general) {
	// error check for validation layer support
    if (ENABLE_VK_VALIDATION_LAYERS && !VUTIL_CheckValidationLayerSupport()) {
		EZ_WARN("Requested validation layers are not available");
        SUPER_DISABLE_VALIDATION_LAYERS();
	}

    // create app info (technically optional)
    VkApplicationInfo appInfo = { 0 };
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Prism Renderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Prism Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

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
		EZ_FATAL("Failed to create vulkan instance");
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
			EZ_FATAL("Failed to set up debug messenger");
			return FALSE;
		}
		messenger_extension(general->instance, &createInfo, NULL, &(general->messenger));
	}

	// pick gpu
	uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(general->instance, &deviceCount, NULL);
    if (deviceCount == 0) {
		EZ_FATAL("No devices with vulkan support were found");
		return FALSE;
	}
    VkPhysicalDevice* devices = EZ_ALLOC(deviceCount, sizeof(VkPhysicalDevice));
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
        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) curr_score += 10000;
        curr_score += deviceProperties.limits.maxImageDimension2D;
        if (!deviceFeatures.geometryShader) curr_score = 0;
        if (!deviceFeatures.samplerAnisotropy) curr_score = 0;
        if (curr_score > score) {
            score = curr_score;
            ind = i;
        }
    }
    if (score == 0) {
		EZ_FATAL("A suitable GPU could not be found");
		return FALSE;
	}
    VkPhysicalDeviceProperties dp;
    vkGetPhysicalDeviceProperties(devices[ind], &dp);
	strcpy(general->gpuname, dp.deviceName);
    general->gpu = devices[ind];
    EZ_FREE(devices);

	// create device interface
	VulkanFamilyGroup families = VUTIL_FindQueueFamilies(general->gpu);
    VkDeviceQueueCreateInfo queueInfos[2];
    float priority = 1.0f;
    uint32_t queueCount = 1;
    queueInfos[0] = (VkDeviceQueueCreateInfo){ 0 };
    queueInfos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfos[0].queueFamilyIndex = families.graphics.value;
    queueInfos[0].queueCount = 1;
    queueInfos[0].pQueuePriorities = &priority;
    if (families.transfer.value != families.graphics.value) {
        queueInfos[1] = (VkDeviceQueueCreateInfo){ 0 };
        queueInfos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfos[1].queueFamilyIndex = families.transfer.value;
        queueInfos[1].queueCount = 1;
        queueInfos[1].pQueuePriorities = &priority;
        queueCount = 2;
    }
    VkPhysicalDeviceFeatures deviceFeatures = { 0 };
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.sampleRateShading = VK_TRUE;
    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures = { 0 };
    timelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    timelineFeatures.timelineSemaphore = VK_TRUE;
    VkDeviceCreateInfo deviceCreateInfo = { 0 };
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pNext = &timelineFeatures;
    deviceCreateInfo.pQueueCreateInfos = queueInfos;
    deviceCreateInfo.queueCreateInfoCount = queueCount;
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = g_vinit_renderer_ref->vulkan.metadata.extensions.device.size;
    deviceCreateInfo.ppEnabledExtensionNames = g_vinit_renderer_ref->vulkan.metadata.extensions.device.data;
    result = vkCreateDevice(general->gpu, &deviceCreateInfo, NULL, &(general->interface));
	if (result != VK_SUCCESS) {
		EZ_FATAL("Failed to create logical device");
		return FALSE;
	}

    return TRUE;
}

BOOL VINIT_Geometry(VulkanGeometry* geometry) {
    if (!VINIT_BVH(&(geometry->bvh))) return FALSE;
    if (!VINIT_Normals(&(geometry->normals))) return FALSE;
    if (!VINIT_Vertices(&(geometry->vertices))) return FALSE;
	if (!VINIT_Triangles(&(geometry->triangles))) return FALSE;
	if (!VINIT_Emissives(&(geometry->emissives))) return FALSE;
	if (!VINIT_Materials(&(geometry->materials))) return FALSE;
	if (!VINIT_Lights(&(geometry->lights))) return FALSE;
	if (!VINIT_Simulation(&(geometry->fluid))) return FALSE;
	if (!VINIT_Transforms(&(geometry->transforms))) return FALSE;
	if (!VINIT_Poses(&(geometry->poses))) return FALSE;
	if (!VINIT_Skins(&(geometry->skins))) return FALSE;
    return TRUE;
}

BOOL VINIT_Metadata(VulkanMetadata* metadata) {
	// set up validation layers
    ARRLIST_StaticString_add(&(metadata->validation), "VK_LAYER_KHRONOS_validation");

    // set up required extensions
    if (ENABLE_VK_VALIDATION_LAYERS) {
        ARRLIST_StaticString_add(&(metadata->extensions.required), VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return TRUE;
}

BOOL VINIT_Core(VulkanCore* core) {
	if (!VINIT_Shaders(&(core->shaders))) return FALSE;
	if (!VINIT_General(&(core->general))) return FALSE;
    if (!VINIT_Transfer(&(core->transfer))) return FALSE;
	if (!VINIT_Scheduler(&(core->scheduler))) return FALSE;
    if (!VINIT_ExtendedBuffers(&g_vinit_renderer_ref->ebuffers)) return FALSE;
	if (!VINIT_Geometry(&(core->geometry))) return FALSE;
	if (!VINIT_Bridge(&(core->bridge))) return FALSE;
	if (!VINIT_RenderContext(&(core->context))) return FALSE;
    return TRUE;
}

BOOL VINIT_Vulkan(VulkanObject* vulkan) {
	if (!VINIT_Metadata(&(vulkan->metadata))) return FALSE;
	if (!VINIT_Core(&(vulkan->core))) return FALSE;
    return TRUE;
}

void VINIT_SetVulkanInitContext(Renderer* renderer) {
	g_vinit_renderer_ref = renderer;
}
