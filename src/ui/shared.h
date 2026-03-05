#ifndef SHARED_H
#define SHARED_H

#include <stddef.h>

size_t DropdownSelectMaterial(void* data, size_t index);

size_t DropdownSelectLightModel(void* data, size_t index);

char** LightModelLabels();

#endif
