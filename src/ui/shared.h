#ifndef SHARED_H
#define SHARED_H

#include <ui/ui.h>

size_t DropdownSelectSimVisual(void* data, size_t index, BOOL cancel);

size_t DropdownSelectMaterial(void* data, size_t index, BOOL cancel);

size_t DropdownSelectLightModel(void* data, size_t index, BOOL cancel);

size_t DropdownSelectARAPModel(void* data, size_t index, BOOL cancel);

size_t DropdownSelectAnimation(void* data, size_t index, BOOL cancel);

size_t DropdownSelectDebugMode(void* data, size_t index, BOOL cancel);

char** SimVisualLabels();

char** LightModelLabels();

char** ARAPModelLabels();

char** DebugModeLabels();

Popup* GenerateAddObjectPopup();

Popup* GenerateAddSimObjectPopup();

#endif
