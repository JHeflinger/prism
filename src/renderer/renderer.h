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

LightID SubmitLight(PointLight light);

void RemoveLight(LightID id);

void ClearLights();

MaterialID SubmitMaterial(SurfaceMaterial material);

void ClearMaterials();

void Render();

void Draw(float x, float y, float w, float h);

float RenderTime();

size_t NumTriangles();

size_t NumSDFs();

size_t NumMaterials();

size_t NumLights();

Vector2 RenderResolution();

RendererConfig* RenderConfig();

float RenderFrameTime();

#endif
