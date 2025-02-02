#include "vupdate.h"
#include "renderer/vulkan/vutils.h"

Renderer* g_vupdt_renderer_ref = NULL;

void VUPDT_Vertices(VulkanDataBuffer* vertices) {
    if (sizeof(Vertex) * g_vupdt_renderer_ref->geometry.vertices.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.vertices.data,
        sizeof(Vertex) * g_vupdt_renderer_ref->geometry.vertices.size,
        sizeof(Vertex) * g_vupdt_renderer_ref->geometry.vertices.maxsize,
        vertices->buffer);
}

void VUPDT_Indices(VulkanDataBuffer* indices) {
    if (sizeof(Index) * g_vupdt_renderer_ref->geometry.indices.maxsize == 0) return;
    VUTIL_CopyHostToBuffer(
        g_vupdt_renderer_ref->geometry.indices.data,
        sizeof(Index) * g_vupdt_renderer_ref->geometry.indices.size,
        sizeof(Index) * g_vupdt_renderer_ref->geometry.indices.maxsize,
        indices->buffer);
}

void VUPDT_SetVulkanUpdateContext(Renderer* renderer) {
	g_vupdt_renderer_ref = renderer;
}

