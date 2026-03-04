#include "vshaders.h"
#include <easymemory.h>
#include <easyobjects.h>
#include <easybasics.h>

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
	if (strcmp(name, "OverlayUniformBufferObject") == 0) {
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
				}, 0.0f,
				sizeof(OverlayUniformBufferObject)
			},
		};
	} else if (strcmp(name, "UniformBufferObject") == 0) {
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
				}, 0.0f,
				sizeof(UniformBufferObject)
			},
		};
	} else if (strcmp(name, "GeometryUniformBufferObject") == 0) {
		return (VulkanBoundVariable) {
			UNIFORM_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.renderdata.ubos.geometry_objects[i].buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					FALSE,
					(void*)1
				}, 0.0f,
				sizeof(GeometryUniformBufferObject)
			},
		};
	} else if (strcmp(name, "RayGeneratorSSBOIn") == 0) {
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
				}, 0.0f,
				sizeof(RayGenerator)
			}
		};
	} else if (strcmp(name, "hdrImage") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_IMAGE,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.hdr[i].view)
			},
			(SchrodingSize) { (SchrodingRef) { 0 }, 0, 0 }
		};
	} else if (strcmp(name, "outputImage") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_IMAGE,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.targets[i].view)
			},
			(SchrodingSize) { (SchrodingRef) { 0 }, 0, 0 }
		};
	} else if (strcmp(name, "TriangleSSBOIn") == 0) {
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
				}, 0.0f,
				sizeof(Triangle)
			}
		};
	} else if (strcmp(name, "EmissiveSSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.emissives.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.emissives.size)
				}, 0.0f,
				sizeof(TriangleID)
			}
		};
	} else if (strcmp(name, "MaterialSSBOIn") == 0) {
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
				}, 0.0f,
				sizeof(SurfaceMaterial)
			}
		};
	} else if (strcmp(name, "VertexSSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.vertices.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.vertices.size)
				}, 0.0f,
				sizeof(vec4)
			}
		};
	} else if (strcmp(name, "WorkGroupOffsetSSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.bvh.workoffsets.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.triangles.size)
				}, INVOCATION_GROUP_SIZE,
				sizeof(uint32_t) * 16
			}
		};
	} else if (strcmp(name, "WorkGroupHistorySSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.bvh.workhistory.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.triangles.size)
				}, INVOCATION_GROUP_SIZE,
				sizeof(uint32_t) * 16
			}
		};
	} else if (strcmp(name, "MortonSSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.bvh.mortons.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.triangles.size)
				}, 0.0f,
				sizeof(uint32_t)
			}
		};
	} else if (strcmp(name, "IndexSSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.bvh.indices.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.triangles.size)
				}, 0.0f,
				sizeof(uint32_t)
			}
		};
	} else if (strcmp(name, "MortonSwapSSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.bvh.mortonswap.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.triangles.size)
				}, 0.0f,
				sizeof(uint32_t)
			}
		};
	} else if (strcmp(name, "IndexSwapSSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.bvh.indexswap.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.triangles.size)
				}, 0.0f,
				sizeof(uint32_t)
			}
		};
	} else if (strcmp(name, "BoundingBoxSSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.bvh.boundingboxes.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.triangles.size)
				}, 0.0f,
				sizeof(AxisAlignedBoundingBox)
			}
		};
	} else if (strcmp(name, "BVHNodeSSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.bvh.nodes.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.triangles.size)
				}, 0.0f,
				sizeof(BVHNode) * 2
			}
		};
	} else if (strcmp(name, "BucketBaseSSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.bvh.buckets.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					FALSE,
					(void*)16
				}, 0.0f,
				sizeof(uint32_t)
			}
		};
	} else if (strcmp(name, "NormalSSBOIn") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.geometry.normals.buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					TRUE,
					&(renderer->geometry.normals.size)
				}, 0.0f,
				sizeof(vec4)
			}
		};
	} else if (strcmp(name, "LightSSBOIn") == 0) {
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
				}, 0.0f,
				sizeof(SceneLight)
			}
		};
	} else if (strcmp(name, "OverlaySSBO") == 0) {
		return (VulkanBoundVariable) {
			STORAGE_BUFFER,
			(SchrodingRef) {
				TRUE,
				&(renderer->vulkan.core.context.renderdata.overlay_ssbos[i].buffer)
			},
			(SchrodingSize) {
				(SchrodingRef) {
					FALSE,
					(void*)1
				}, 0.0f,
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
					for (size_t i = 0; i < CPUSWAP_LENGTH; i++) {
						ARRLIST_VulkanBoundVariable_add(&vbvs, get_bound_variable(context, identifier, i));
					}
					num_vars++;
				} else {
					EZ_WARN("Unable to detect a binding on line %d: %s", linecount, bindstr);
				}
			}
		}
		for (int i = 0; i < num_vars; i++) {
			size_t index = 0;
			BOOL found = FALSE;
			for (size_t k = 0; k < indices.size; k++) {
				if (indices.data[k] == i) {
					index = k;
					found = TRUE;
					break;
				}
			}
			if (!found) {
				EZ_ERROR("Shader \"%s\" bind group is missing index %d", readfile, i);	
			}
			for (size_t j = 0; j < CPUSWAP_LENGTH; j++) {
				ARRLIST_VulkanBoundVariable_add(&(shader->variables[j]), vbvs.data[index*CPUSWAP_LENGTH + j]);
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
