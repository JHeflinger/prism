#ifndef SHARED_H
#define SHARED_H

#include <stddef.h>

size_t DropdownSelectSimVisual(void* data, size_t index);

size_t DropdownSelectMaterial(void* data, size_t index);

size_t DropdownSelectLightModel(void* data, size_t index);

size_t DropdownSelectARAPModel(void* data, size_t index);

size_t DropdownSelectAnimation(void* data, size_t index);

size_t DropdownSelectDebugMode(void* data, size_t index);

char** SimVisualLabels();

char** LightModelLabels();

char** ARAPModelLabels();

char** DebugModeLabels();

#endif
