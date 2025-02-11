#ifndef RENDERER_H
#define RENDERER_H

#include "renderer/rstructs.h"

void SetViewportSlice(size_t w, size_t h);

void OverrideResolution(size_t x, size_t y);

void InitializeRenderer();

void DestroyRenderer();

SimpleCamera GetCamera();

void MoveCamera(SimpleCamera camera);

TriangleID SubmitTriangle(Triangle triangle);

void RemoveTriangle(TriangleID id);

void ClearTriangles();

SDFID SubmitSDF(SDFPrimitive sdf);

void RemoveSDF(SDFID id);

void ClearSDFs();

MaterialID SubmitMaterial(SurfaceMaterial material);

void ClearMaterials();

void Render();

void Draw(float x, float y, float w, float h);

float RenderTime();

size_t NumTriangles();

Vector2 RenderResolution();

RendererConfig* RenderConfig();

#endif
