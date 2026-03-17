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

void FitCamera();

void ReorientCamera();

void GetVertex(size_t index, vec3 out);

float* VertexReference(VertexID vertex);

void SubmitVertex(vec3 vertex);

void ClearVertices();

void LockVertex(VertexID vertex);

void UnlockVertex(VertexID vertex);

BOOL VertexLocked(VertexID vertex);

void SubmitNormal(vec3 normal);

void ClearNormals();

TriangleID SubmitTriangle(Triangle triangle);

void ClearTriangles();

LightID SubmitLight(SceneLight light);

LightID SubmitNamedLight(SceneLight light, const char* name);

char* LightName(LightID lid);

char** LightNameReference(LightID lid);

void ClearLights();

MaterialID SubmitMaterial(SurfaceMaterial material);

MaterialID SubmitNamedMaterial(SurfaceMaterial material, const char* name);

char* MaterialName(MaterialID mid);

char** MaterialNameReference(MaterialID mid);

void ClearMaterials();

void Render();

void Draw(float x, float y, float w, float h);

float RenderTime();

size_t NumNormals();

size_t NumVertices();

size_t NumTriangles();

size_t NumMaterials();

size_t NumEmissives();

void UpdateNormals();

void UpdateVertices();

SurfaceMaterial* MaterialReference(size_t index);

void UpdateMaterials();

size_t NumLights();

SceneLight* LightReference(size_t index);

void UpdateLights();

Vector2 RenderResolution();

RendererConfig* RenderConfig();

Geometry* RendererGeometry();

float RenderFrameTime();

Triangle* TriangleReference(size_t index);

void UpdateTriangles();

BOOL Subdivide();

BOOL Simplify(size_t faces);

void Displace(float displacement);

BOOL Smoothen(float smoothening);

BOOL Remesh(float nudge);

void SavePose();

void RigidDeform();

void SaveRender(const char* filepath);

char* GPUModel();

void PollGPUCache(BOOL init);

size_t GPUHeapCount();

size_t GPUHeapUsage(size_t i);

size_t GPUHeapBudget(size_t i);

const char* GPUHeapType(size_t i);

#endif
