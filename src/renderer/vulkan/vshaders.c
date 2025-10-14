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
	GenerateShader(renderer, "shaders/overlay.comp", "build/shaders/overlay.comp.spv");
	//exit(0);
}

char* last_relevant_word(char* str, int len) {
	for (int i = len - 1; i >= 0; i--) {
		if (str[i] == ' ') {
			if (!((str[i + 1] >= 'A' && str[i + 1] <= 'Z') ||
				(str[i + 1] >= 'a' && str[i + 1] <= 'z'))) {
				continue;
			} else {
				return str + i + 1;
			}
		}
	}
	return str;
}

BOOL is_alphanumeric(char c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

VulkanBoundVariable get_bound_variable(const char* name) {
	VulkanBoundVariable vbv = { 0 };
	if (strcmp(name, "OverlayUniformBufferObject")) {
		return vbv;
	} else if (strcmp(name, "UniformBufferObject")) {
		
		return vbv;
	} else if (strcmp(name, "RayGeneratorSSBOIn")) {

		return vbv;
	} else if (strcmp(name, "outputImage")) {

		return vbv;
	} else if (strcmp(name, "TriangleSSBOIn")) {

		return vbv;
	} else if (strcmp(name, "MaterialSSBOIn")) {

		return vbv;
	} else if (strcmp(name, "BVHSSBOIn")) {

		return vbv;
	} else if (strcmp(name, "SDFSSBOIn")) {

		return vbv;
	} else if (strcmp(name, "LightSSBOIn")) {

		return vbv;
	} else if (strcmp(name, "OverlaySSBO")) {

		return vbv;
	}
	EZ_WARN("Unable to automatically identify source references of shader variable \"%s\"", name);
	return (VulkanBoundVariable){ 0 };
}

VulkanShader* GenerateShader(Renderer* context, const char* readfile, const char* sourcefile) {
	VulkanShader* shader = EZ_ALLOC(1, sizeof(VulkanShader));
	shader->filename = sourcefile;
	FILE* f = fopen(readfile, "r");
	if (f) {
		char line[512] = { 0 };
		int linecount = 0;
		while (fgets(line, sizeof(line), f)) {
			linecount++;
			int linelen = strlen(line);
			if (linelen >= 512) 
				EZ_WARN("Abnormally long line length detected on line %d in shader %s, this may have adverse effects on shader parsing", linecount, readfile);
			char* bindstr = strstr(line, "layout(binding");
			if (!bindstr) bindstr = strstr(line, "layout (binding");
			if (bindstr) {
				int ind = 0;
				while (bindstr[ind] != '\0') {
					if (bindstr[ind] >= '0' && bindstr[ind] <= '9') break;
					ind++;
				}
				if (bindstr[ind] != '\0') {
					EZ_INFO("%c", bindstr[ind]);
				}
				char* identifier = last_relevant_word(line, linelen);
				ind = 0;
				while (identifier[ind] != '\0') {
					if (!is_alphanumeric(identifier[ind])) identifier[ind] = '\0';
					ind++;
				}
				EZ_INFO("%s", identifier);
				get_bound_variable(identifier);
			}
		}
	} else {
		EZ_ERROR("Shader cannot load invalid file - unable to read file %s", readfile);
		return NULL;
	}
	fclose(f);
	return shader;
}
