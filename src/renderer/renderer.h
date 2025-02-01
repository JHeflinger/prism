#ifndef RENDERER_H
#define RENDERER_H

#include "renderer/rstructs.h"

void InitializeRenderer();

void DestroyRenderer();

SimpleCamera GetCamera();

void MoveCamera(SimpleCamera camera);

TriangleID SubmitTriangle(Triangle triangle);

void RemoveTriangle(TriangleID id);

void ClearTriangles();

TextureID SubmitTexture(const char* filepath);

void RemoveTexture(TextureID id);

void ClearTextures();

void Render();

void Draw(float x, float y, float w, float h);

float RenderTime();

#endif