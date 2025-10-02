#include "vshaders.h"
#include <easymemory.h>

ARRLIST_VulkanShaderPtr* GenerateDefaultShaders(Renderer* renderer) {
	ARRLIST_VulkanShaderPtr* list = EZ_ALLOC(1, sizeof(ARRLIST_VulkanShaderPtr));
	
	// render shader
	VulkanShader* rShader = EZ_ALLOC(1, sizeof(VulkanShader));	
	rShader->filename = "build/shaders/render.comp.spv";
	for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
		ARRLIST_VulkanBoundVariable_add(&(rShader->variables[i]), (VulkanBoundVariable) {
			UNIFORM_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.renderdata.ubos.objects[i].buffer)
			},
			(SchrodingRef) {
				FALSE,
				(void*)(sizeof(UniformBufferObject))
			},
		});
		ARRLIST_VulkanBoundVariable_add(&(rShader->variables[i]), (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.renderdata.ssbos[i].buffer)
			},
			(SchrodingRef) {
				FALSE,
				(void*)(sizeof(RayGenerator) * ((uint32_t)renderer->dimensions.x) * ((uint32_t)renderer->dimensions.y))
			}
		});
	}

	// overlay shader
	VulkanShader* oShader = EZ_ALLOC(1, sizeof(VulkanShader));
	oShader->filename = "build/shaders/overlay.comp.spv";

	ARRLIST_VulkanShaderPtr_add(list, rShader);
	ARRLIST_VulkanShaderPtr_add(list, oShader);
	return list;
}
