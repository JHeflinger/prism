#include "vinit.h"
#include <easylogger.h>
#include "renderer/vulkan/vutils.h"
#include "renderer/vulkan/vupdate.h"
#include <GLFW/glfw3.h>

Renderer* g_vinit_renderer_ref = NULL;

BOOL VINIT_OverlaySSBO(VulkanDataBuffer* ssbo) {
	VUTIL_CreateBuffer(
		sizeof(OverlaySSBO),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		ssbo);
	return TRUE;
}

BOOL VINIT_Lights(VulkanDataBuffer* lights) {
    size_t arrsize = sizeof(PointLight) * g_vinit_renderer_ref->geometry.lights.maxsize;
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

        vkMapMemory(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            ubos->objects[i].memory,
            0, sizeof(UniformBufferObject), 0, &(ubos->mapped[i]));
        vkMapMemory(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            ubos->overlay_objects[i].memory,
            0, sizeof(OverlayUniformBufferObject), 0, &(ubos->overlay_mapped[i]));
    }
    return TRUE;
}

BOOL VINIT_Descriptors(VulkanDescriptors* descriptors) {
    // render descriptors
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = { 0 };
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding ssboLayoutBinding = { 0 };
        ssboLayoutBinding.binding = 1;
        ssboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ssboLayoutBinding.descriptorCount = 1;
        ssboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding imageLayoutBinding = { 0 };
        imageLayoutBinding.binding = 2;
        imageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        imageLayoutBinding.descriptorCount = 1;
        imageLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding trianglesLayoutBinding = { 0 };
        trianglesLayoutBinding.binding = 3;
        trianglesLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        trianglesLayoutBinding.descriptorCount = 1;
        trianglesLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding materialsLayoutBinding = { 0 };
        materialsLayoutBinding.binding = 4;
        materialsLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        materialsLayoutBinding.descriptorCount = 1;
        materialsLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bvhLayoutBinding = { 0 };
        bvhLayoutBinding.binding = 5;
        bvhLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bvhLayoutBinding.descriptorCount = 1;
        bvhLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding sdfLayoutBinding = { 0 };
        sdfLayoutBinding.binding = 6;
        sdfLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sdfLayoutBinding.descriptorCount = 1;
        sdfLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding lightLayoutBinding = { 0 };
        lightLayoutBinding.binding = 7;
        lightLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        lightLayoutBinding.descriptorCount = 1;
        lightLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding overlaySSBOLayoutBinding = { 0 };
        overlaySSBOLayoutBinding.binding = 8;
        overlaySSBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        overlaySSBOLayoutBinding.descriptorCount = 1;
        overlaySSBOLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bindings[] = { 
            uboLayoutBinding,
            ssboLayoutBinding,
            imageLayoutBinding,
            trianglesLayoutBinding,
            materialsLayoutBinding,
            bvhLayoutBinding,
            sdfLayoutBinding,
            lightLayoutBinding,
			overlaySSBOLayoutBinding
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo = { 0 };
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 9;
        layoutInfo.pBindings = bindings;

        VkResult result = vkCreateDescriptorSetLayout(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            &layoutInfo, NULL, &(descriptors[0].layout));
        if (result != VK_SUCCESS) {
            LOG_FATAL("Failed to create descriptor set layout!");
            return FALSE;
        }

        VkDescriptorPoolSize poolSizes[9] = { 0 };
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = CPUSWAP_LENGTH;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[1].descriptorCount = CPUSWAP_LENGTH;
        poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[2].descriptorCount = CPUSWAP_LENGTH;
        poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[3].descriptorCount = CPUSWAP_LENGTH;
        poolSizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[4].descriptorCount = CPUSWAP_LENGTH;
        poolSizes[5].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[5].descriptorCount = CPUSWAP_LENGTH;
        poolSizes[6].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[6].descriptorCount = CPUSWAP_LENGTH;
        poolSizes[7].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[7].descriptorCount = CPUSWAP_LENGTH;
        poolSizes[8].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[8].descriptorCount = CPUSWAP_LENGTH;

        VkDescriptorPoolCreateInfo poolInfo = { 0 };
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 9;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = CPUSWAP_LENGTH;
        result = vkCreateDescriptorPool(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            &poolInfo, NULL, &(descriptors[0].pool));
        if (result != VK_SUCCESS) {
            LOG_FATAL("Failed to create descriptor pool!");
            return FALSE;
        }

        VkDescriptorSetLayout layouts[CPUSWAP_LENGTH];
        for (size_t i = 0; i < CPUSWAP_LENGTH; i++) layouts[i] = descriptors[0].layout;
        VkDescriptorSetAllocateInfo allocInfo = { 0 };
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptors[0].pool;
        allocInfo.descriptorSetCount = CPUSWAP_LENGTH;
        allocInfo.pSetLayouts = layouts;
        result = vkAllocateDescriptorSets(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            &allocInfo, descriptors[0].sets);
        if (result != VK_SUCCESS) {
            LOG_FATAL("Failed to create descriptor sets!");
            return FALSE;
        }
    }

    // overlay descripors
    {
        VkDescriptorSetLayoutBinding ssboLayoutBinding = { 0 };
        ssboLayoutBinding.binding = 0;
        ssboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ssboLayoutBinding.descriptorCount = 1;
        ssboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding imageLayoutBinding = { 0 };
        imageLayoutBinding.binding = 1;
        imageLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        imageLayoutBinding.descriptorCount = 1;
        imageLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding uboLayoutBinding = { 0 };
        uboLayoutBinding.binding = 2;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding overlaySSBOLayoutBinding = { 0 };
        overlaySSBOLayoutBinding.binding = 3;
        overlaySSBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        overlaySSBOLayoutBinding.descriptorCount = 1;
        overlaySSBOLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding bindings[] = {
            ssboLayoutBinding,
            imageLayoutBinding,
            uboLayoutBinding,
			overlaySSBOLayoutBinding
        };

        VkDescriptorSetLayoutCreateInfo layoutInfo = { 0 };
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 4;
        layoutInfo.pBindings = bindings;

        VkResult result = vkCreateDescriptorSetLayout(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            &layoutInfo, NULL, &(descriptors[1].layout));
        if (result != VK_SUCCESS) {
            LOG_FATAL("Failed to create descriptor set layout!");
            return FALSE;
        }

        VkDescriptorPoolSize poolSizes[4] = { 0 };
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[0].descriptorCount = CPUSWAP_LENGTH;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[1].descriptorCount = CPUSWAP_LENGTH;
        poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[2].descriptorCount = CPUSWAP_LENGTH;
        poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSizes[3].descriptorCount = CPUSWAP_LENGTH;

        VkDescriptorPoolCreateInfo poolInfo = { 0 };
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 4;
        poolInfo.pPoolSizes = poolSizes;
        poolInfo.maxSets = CPUSWAP_LENGTH;
        result = vkCreateDescriptorPool(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            &poolInfo, NULL, &(descriptors[1].pool));
        if (result != VK_SUCCESS) {
            LOG_FATAL("Failed to create descriptor pool!");
            return FALSE;
        }

        VkDescriptorSetLayout layouts[CPUSWAP_LENGTH];
        for (size_t i = 0; i < CPUSWAP_LENGTH; i++) layouts[i] = descriptors[1].layout;
        VkDescriptorSetAllocateInfo allocInfo = { 0 };
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptors[1].pool;
        allocInfo.descriptorSetCount = CPUSWAP_LENGTH;
        allocInfo.pSetLayouts = layouts;
        result = vkAllocateDescriptorSets(
            g_vinit_renderer_ref->vulkan.core.general.interface,
            &allocInfo, descriptors[1].sets);
        if (result != VK_SUCCESS) {
            LOG_FATAL("Failed to create descriptor sets!");
            return FALSE;
        }
    }

    VUPDT_DescriptorSets(descriptors);
    return TRUE;
}

BOOL VINIT_ShaderStorageBuffers(VulkanDataBuffer* ssbo_array) {
    uint32_t imgw = (uint32_t)g_vinit_renderer_ref->dimensions.x;
    uint32_t imgh = (uint32_t)g_vinit_renderer_ref->dimensions.y;
    RayGenerator* raygens = EZALLOC(imgw * imgh, sizeof(RayGenerator));
    for (uint32_t x = 0; x < imgw; x++) {
        for (uint32_t y = 0; y < imgh; y++) {
            raygens[y*imgw + x].x = x;
            raygens[y*imgw + x].y = y;
			raygens[y*imgw + x].time = 0.0f;
			raygens[y*imgw + x].tid = 0;
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
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &(ssbo_array[i]));
        VUTIL_CopyBuffer(stagingBuffer.buffer, ssbo_array[i].buffer, bufferSize);
    }

    vkDestroyBuffer(g_vinit_renderer_ref->vulkan.core.general.interface, stagingBuffer.buffer, NULL);
    vkFreeMemory(g_vinit_renderer_ref->vulkan.core.general.interface, stagingBuffer.memory, NULL);
    EZFREE(raygens);
    return TRUE;
}

BOOL VINIT_RenderData(VulkanRenderData* renderdata) {
	if (!VINIT_ShaderStorageBuffers(renderdata->ssbos)) return FALSE;
	if (!VINIT_UniformBuffers(&(renderdata->ubos))) return FALSE;
	if (!VINIT_OverlaySSBO(&(renderdata->overlay_ssbo))) return FALSE;
	if (!VINIT_Descriptors(renderdata->descriptors)) return FALSE;
    return TRUE;
}

BOOL VINIT_Pipeline(VulkanPipeline* pipeline) {
    SimpleFile* compshadercode = ReadFile("build/shaders/render.comp.spv");
    SimpleFile* overlayshadercode = ReadFile("build/shaders/overlay.comp.spv");
	VkShaderModule compshader = VUTIL_CreateShader(compshadercode);
    VkShaderModule overlayshader = VUTIL_CreateShader(overlayshadercode);

	VkPipelineShaderStageCreateInfo compShaderStageInfo = { 0 };
	compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	compShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	compShaderStageInfo.module = compshader;
	compShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo overlayShaderStageInfo = { 0 };
	overlayShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	overlayShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	overlayShaderStageInfo.module = overlayshader;
	overlayShaderStageInfo.pName = "main";

    VkPipelineLayoutCreateInfo pipelineLayoutInfos[2] = { 0 };
    pipelineLayoutInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfos[0].setLayoutCount = 1;
    pipelineLayoutInfos[0].pSetLayouts = &(g_vinit_renderer_ref->vulkan.core.context.renderdata.descriptors[0].layout);
    pipelineLayoutInfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfos[1].setLayoutCount = 1;
    pipelineLayoutInfos[1].pSetLayouts = &(g_vinit_renderer_ref->vulkan.core.context.renderdata.descriptors[1].layout);

    VkResult result = vkCreatePipelineLayout(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        &(pipelineLayoutInfos[0]), NULL, &(pipeline->layout[0]));
	result |= vkCreatePipelineLayout(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        &(pipelineLayoutInfos[1]), NULL, &(pipeline->layout[1]));
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create pipeline layout!");
        return FALSE;
    }

    VkComputePipelineCreateInfo pipelineInfos[2] = { 0 };
    pipelineInfos[0].sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfos[0].layout = pipeline->layout[0];
    pipelineInfos[0].stage = compShaderStageInfo;
    pipelineInfos[1].sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfos[1].layout = pipeline->layout[1];
    pipelineInfos[1].stage = overlayShaderStageInfo;

    result = vkCreateComputePipelines(
        g_vinit_renderer_ref->vulkan.core.general.interface,
        VK_NULL_HANDLE, 2, pipelineInfos, NULL, pipeline->pipeline);
    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create pipeline!");
        return FALSE;
    }

    FreeFile(compshadercode);
    FreeFile(overlayshadercode);
	vkDestroyShaderModule(g_vinit_renderer_ref->vulkan.core.general.interface, compshader, NULL);
	vkDestroyShaderModule(g_vinit_renderer_ref->vulkan.core.general.interface, overlayshader, NULL);
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

BOOL VINIT_RenderContext(VulkanRenderContext* context) {
	if (!VINIT_Targets(context->targets)) return FALSE;
	if (!VINIT_RenderData(&(context->renderdata))) return FALSE;
	if (!VINIT_Pipeline(&(context->pipeline))) return FALSE;
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

BOOL VINIT_SDFs(VulkanDataBuffer* sdfs) {
    size_t arrsize = sizeof(SDFPrimitive) * g_vinit_renderer_ref->geometry.sdfs.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        sdfs);
    VUPDT_SDFs(sdfs);
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

BOOL VINIT_BoundingVolumeHierarchy(VulkanDataBuffer* bvh) {
    size_t arrsize = sizeof(NodeBVH) * g_vinit_renderer_ref->geometry.bvh.maxsize;
    arrsize = arrsize > 0 ? arrsize : 1;
    VUTIL_CreateBuffer(
        arrsize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        bvh);
    VUPDT_BoundingVolumeHierarchy(bvh);
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
		LOG_WARN("Requested validation layers are not available");
        SUPER_DISABLE_VALIDATION_LAYERS();
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

BOOL VINIT_Geometry(VulkanGeometry* geometry) {
	if (!VINIT_Triangles(&(geometry->triangles))) return FALSE;
	if (!VINIT_Materials(&(geometry->materials))) return FALSE;
	if (!VINIT_BoundingVolumeHierarchy(&(geometry->bvh))) return FALSE;
	if (!VINIT_SDFs(&(geometry->sdfs))) return FALSE;
	if (!VINIT_Lights(&(geometry->lights))) return FALSE;
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
	if (!VINIT_Geometry(&(core->geometry))) return FALSE;
	if (!VINIT_Scheduler(&(core->scheduler))) return FALSE;
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
