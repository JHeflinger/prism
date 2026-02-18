#ifndef RENDERER_H
#define RENDERER_H

#include "renderer/rstructs.h"

PipelineFlags GetPipelineFlags();

void SetPipelineFlags(PipelineFlags flags);

void SetViewportSlice(size_t w, size_t h);

void OverrideResolution(size_t x, size_t y);

void InitializeRenderer();

void DestroyRenderer();

SimpleCamera GetCamera();

void MoveCamera(SimpleCamera camera);

void ReorientCamera();

void SubmitVertex(GPUVec3 vertex);

void ClearVertices();

TriangleID SubmitTriangle(Triangle triangle);

void ClearTriangles();

LightID SubmitLight(PointLight light);

void ClearLights();

MaterialID SubmitMaterial(SurfaceMaterial material);

MaterialID SubmitNamedMaterial(SurfaceMaterial material, const char* name);

char* MaterialName(MaterialID mid);

char** MaterialNameReference(MaterialID mid);

void ClearMaterials();

void Render();

void Draw(float x, float y, float w, float h);

float RenderTime();

size_t NumVertices();

size_t NumTriangles();

size_t NumMaterials();

size_t NumEmissives();

void UpdateVertices();

SurfaceMaterial* MaterialReference(size_t index);

void UpdateMaterials();

size_t NumLights();

PointLight* LightReference(size_t index);

void UpdateLights();

Vector2 RenderResolution();

RendererConfig* RenderConfig();

float RenderFrameTime();

Triangle* TriangleReference(size_t index);

void RecalculateTriangleBB(size_t index);

void UpdateTriangles();

void SaveRender(const char* filepath);

char* GPUModel();

void PollGPUCache(BOOL init);

size_t GPUHeapCount();

size_t GPUHeapUsage(size_t i);

size_t GPUHeapBudget(size_t i);

const char* GPUHeapType(size_t i);

#endif
