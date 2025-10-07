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
			(SchrodingSize) {
				(SchrodingRef) {
					FALSE,
					(void*)1
				},
				sizeof(UniformBufferObject)
			},
		});
		ARRLIST_VulkanBoundVariable_add(&(rShader->variables[i]), (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.renderdata.ssbos[i].buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					FALSE,
					(void*)(((size_t)renderer->dimensions.x) * ((size_t)renderer->dimensions.y))
				},
				sizeof(RayGenerator)
			}
		});
		ARRLIST_VulkanBoundVariable_add(&(rShader->variables[i]), (VulkanBoundVariable) {
			STORAGE_IMAGE,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.targets[i].view)
			},
			(SchrodingSize) { (SchrodingRef) { 0 }, 0 }
		});
		ARRLIST_VulkanBoundVariable_add(&(rShader->variables[i]), (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.triangles.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.triangles.size)
				},
				sizeof(Triangle)
			}
		});
		ARRLIST_VulkanBoundVariable_add(&(rShader->variables[i]), (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.materials.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.materials.size)
				},
				sizeof(SurfaceMaterial)
			}
		});
		ARRLIST_VulkanBoundVariable_add(&(rShader->variables[i]), (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.bvh.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.bvh.size)
				},
				sizeof(NodeBVH)
			}
		});
		ARRLIST_VulkanBoundVariable_add(&(rShader->variables[i]), (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.sdfs.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.sdfs.size)
				},
				sizeof(SDFPrimitive)
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
