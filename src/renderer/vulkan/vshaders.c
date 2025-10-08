#include "vshaders.h"
#include <easymemory.h>

void GenerateDefaultShaders(ARRLIST_VulkanShaderPtr* list, Renderer* renderer) {	
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
		ARRLIST_VulkanBoundVariable_add(&(rShader->variables[i]), (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.lights.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.lights.size)
				},
				sizeof(PointLight)
			}
		});
		ARRLIST_VulkanBoundVariable_add(&(rShader->variables[i]), (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.renderdata.overlay_ssbo.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					FALSE,
					(void*)1
				},
				sizeof(OverlaySSBO)
			}
		});
	}

	// overlay shader
	VulkanShader* oShader = EZ_ALLOC(1, sizeof(VulkanShader));
	oShader->filename = "build/shaders/overlay.comp.spv";
	for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
		ARRLIST_VulkanBoundVariable_add(&(oShader->variables[i]), (VulkanBoundVariable) {
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
		ARRLIST_VulkanBoundVariable_add(&(oShader->variables[i]), (VulkanBoundVariable) {
			STORAGE_IMAGE,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.targets[i].view)
			},
			(SchrodingSize) { (SchrodingRef) { 0 }, 0 }
		});
		ARRLIST_VulkanBoundVariable_add(&(oShader->variables[i]), (VulkanBoundVariable) {
			UNIFORM_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.renderdata.ubos.overlay_objects[i].buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					FALSE,
					(void*)1
				},
				sizeof(OverlayUniformBufferObject)
			},
		});
		ARRLIST_VulkanBoundVariable_add(&(oShader->variables[i]), (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.renderdata.overlay_ssbo.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					FALSE,
					(void*)1
				},
				sizeof(OverlaySSBO)
			},
		});
	}

	ARRLIST_VulkanShaderPtr_add(list, rShader);
	ARRLIST_VulkanShaderPtr_add(list, oShader);
}
