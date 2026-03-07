#ifndef OVERLAY_H
#define OVERLAY_H

#include "renderer/vulkan/vstructs.h"

typedef enum {
    NO_SELECT_MODE = 0,
    TRIANGLE_SELECT_MODE = 1,
    VERTEX_SELECT_MODE = 2,
} OverlayMode;

void SetOverlayContext(Renderer* renderer);

void SetViewportRec(Rectangle rec);

Rectangle GetViewportRec();

TriangleID HoveredTriangle();

OverlaySSBO* ExposedOverlaySSBO();

void SetSelectedTriangle(TriangleID tid);

TriangleID GetSelectedTriangle();

OverlayMode GetOverlayMode();

VertexID HoveredVertex();

void SetSelectedVertex(VertexID vid);

VertexID GetSelectedVertex();

#endif
