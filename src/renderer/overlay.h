#ifndef OVERLAY_H
#define OVERLAY_H

#include "renderer/vulkan/vstructs.h"

void SetOverlayContext(Renderer* renderer);

void SetViewportRec(Rectangle rec);

Rectangle GetViewportRec();

size_t HoveredTriangleIndex();

OverlaySSBO* ExposedOverlaySSBO();

void SetSelectedTriangle(TriangleID tid);

TriangleID GetSelectedTriangle();

#endif
