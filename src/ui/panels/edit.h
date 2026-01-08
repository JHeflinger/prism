#ifndef EDIT_H
#define EDIT_H

#include "ui/ui.h"
#include "renderer/rstructs.h"

void SetEditMaterial(size_t index);

void SetEditLight(size_t index);

void SetEditTriangle(size_t index);

Panel GenerateEditPanel();

#endif
