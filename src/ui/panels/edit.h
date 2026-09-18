#ifndef EDIT_H
#define EDIT_H

#include "renderer/rstructs.h"
#include <ui/ui.h>

void SetEditCamera(size_t index);

void SetEditMaterial(size_t index);

void SetEditLight(size_t index);

void SetEditTriangle(size_t index);

void SetEditVertex(size_t index);

void SetEditForce(size_t index);

void SetEditSource(size_t index);

void SetEditMesh(size_t index);

void DeselectEditTarget();

Panel GenerateEditPanel();

void DrawEditMaterial(SurfaceMaterial* matref, char* name, float width, float height);

void DrawEditLight(SceneLight* lref, char* name, float width, float height);

void DrawEditMesh(MeshDescriptor* md, char* name, float width, float height);

void DrawEditCamera(SceneCamera* cref, char* name, float width, float height);

#endif
