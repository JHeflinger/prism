#ifndef SHARED_H
#define SHARED_H

#include <ui/ui.h>

size_t DropdownSelectSimVisual(void* data, size_t index, BOOL cancel);

size_t DropdownSelectMaterial(void* data, size_t index, BOOL cancel);

size_t DropdownSelectBaseMaterial(void* data, size_t index, BOOL cancel);

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

void AnimFlowBegin(float y, float width);

void AnimFlowNext(float w);

float AnimFlowEnd();

float AnimLabel(const char* label);

BOOL AnimToggleButton(const char* label, float w, BOOL active);

float AnimButtonWidth(const char* label);

BOOL AnimPanelHovered(const char* name);

Vector2 AnimPanelMouse();

char** AnimBoneItems(BOOL with_none, size_t* count);

void AnimDrawTextClipped(const char* text, float x, float y, float size, float maxw, Color color);

#endif
