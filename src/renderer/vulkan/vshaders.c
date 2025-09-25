#include "vshaders.h"
#include <easymemory.h>

ARRLIST_VulkanShaderPtr* GenerateDefaultShaders() {
	ARRLIST_VulkanShaderPtr* list = EZ_ALLOC(1, sizeof(ARRLIST_VulkanShaderPtr));
	
	// render shader
	VulkanShader* rShader = EZ_ALLOC(1, sizeof(VulkanShader));	
	rShader->filename = "build/shaders/render.comp.spv";

	// overlay shader
	VulkanShader* oShader = EZ_ALLOC(1, sizeof(VulkanShader));

	return list;
}
