#ifndef RENDERER_H
#define RENDERER_H

#include "renderer/rstructs.h"

void OverrideResolution(size_t x, size_t y);

void InitializeRenderer();

void DestroyRenderer();

SimpleCamera GetCamera();

void MoveCamera(SimpleCamera camera);

TriangleID SubmitTriangle(Triangle triangle);

void RemoveTriangle(TriangleID id);

void ClearTriangles();

void Render();

void Draw(float x, float y, float w, float h);

float RenderTime();

size_t NumTriangles();

Vector2 RenderResolution();

#endif
