#include "vshaders.h"
#include <easymemory.h>
#include <easyobjects.h>

DECLARE_ARRLIST(int);
IMPL_ARRLIST(int);

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
	GenerateShader(renderer, "shaders/render.comp", "build/shaders/render.comp.spv");
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

VulkanBoundVariable get_bound_variable(Renderer* renderer, const char* name, size_t i) {
	if (strcmp(name, "OverlayUniformBufferObject")) {
		return (VulkanBoundVariable) {
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
		};
	} else if (strcmp(name, "UniformBufferObject")) {	
		return (VulkanBoundVariable) {
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
		};
	} else if (strcmp(name, "RayGeneratorSSBOIn")) {
		return (VulkanBoundVariable) {
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
		};
	} else if (strcmp(name, "outputImage")) {
		return (VulkanBoundVariable) {
			STORAGE_IMAGE,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.targets[i].view)
			},
			(SchrodingSize) { (SchrodingRef) { 0 }, 0 }
		};
	} else if (strcmp(name, "TriangleSSBOIn")) {
		return (VulkanBoundVariable) {
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
		};
	} else if (strcmp(name, "MaterialSSBOIn")) {
		return (VulkanBoundVariable) {
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
		};
	} else if (strcmp(name, "BVHSSBOIn")) {
		return (VulkanBoundVariable) {
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
		};
	} else if (strcmp(name, "SDFSSBOIn")) {
		return (VulkanBoundVariable) {
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
		};
	} else if (strcmp(name, "LightSSBOIn")) {
		return (VulkanBoundVariable) {
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
		};
	} else if (strcmp(name, "OverlaySSBO")) {
		return (VulkanBoundVariable) {
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
		};
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
		int num_vars = 0;
		ARRLIST_int indices = { 0 };
		ARRLIST_VulkanBoundVariable vbvs = { 0 };
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
					char numbuff[64] = { 0 };
					int buffind = 0;
					while (bindstr[ind] >= '0' && bindstr[ind] <= '9') {
						numbuff[buffind] = bindstr[ind];
						buffind++;
						ind++;
					}
					ARRLIST_int_add(&indices, atoi(numbuff));
					char* identifier = last_relevant_word(line, linelen);
					ind = 0;
					while (identifier[ind] != '\0') {
						if (!is_alphanumeric(identifier[ind])) identifier[ind] = '\0';
						ind++;
					}
					for (size_t i = 0; i < CPUSWAP_LENGTH; i++)
						ARRLIST_VulkanBoundVariable_add(&vbvs, get_bound_variable(context, identifier, i));
					num_vars++;
				} else {
					EZ_WARN("Unable to detect a binding on line %d: %s", linecount, bindstr);
				}
			}
		}
		EZ_INFO("%d bound variables detected", num_vars);
		for (size_t i = 0; i < indices.size; i++) {
			
			for (size_t j = 0; j < CPUSWAP_LENGTH; j++) {
			}
		}
		ARRLIST_int_clear(&indices);
		ARRLIST_VulkanBoundVariable_clear(&vbvs);
	} else {
		EZ_ERROR("Shader cannot load invalid file - unable to read file %s", readfile);
		return NULL;
	}
	fclose(f);
	return shader;
}
