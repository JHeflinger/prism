#include "vshaders.h"
#include <easymemory.h>

ARRLIST_VulkanShaderPtr* GenerateDefaultShaders() {
	ARRLIST_VulkanShaderPtr* list = EZ_ALLOC(1, sizeof(ARRLIST_VulkanShaderPtr));
	
	// render shader
	VulkanShader* rShader = EZ_ALLOC(1, sizeof(VulkanShader));	
	rShader->filename = "build/shaders/render.comp.spv";
	ARRLIST_VulkanBoundVariable_add(&(rshader->variables), (VulkanBoundVariable) {
		0
	});

	// overlay shader
	VulkanShader* oShader = EZ_ALLOC(1, sizeof(VulkanShader));
	oShader->filename = "build/shaders/overlay.comp.spv";

	ARRLIST_VulkanShaderPtr_add(list, rShader);
	ARRLIST_VulkanShaderPtr_add(list, oShader);
	return list;
}
