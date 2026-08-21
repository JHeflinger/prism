#ifndef EDIT_H
#define EDIT_H

#include <ui/ui.h>

void SetEditMaterial(size_t index);

void SetEditLight(size_t index);

void SetEditTriangle(size_t index);

void SetEditVertex(size_t index);

void SetEditForce(size_t index);

void SetEditSource(size_t index);

void SetEditMesh(size_t index);

void DeselectEditTarget();

Panel GenerateEditPanel();

#endif
