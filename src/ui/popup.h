#ifndef POPUP_H
#define POPUP_H

#include <stdlib.h>

typedef int (*PopupFunction)(size_t, size_t, size_t, size_t);

typedef struct {
    PopupFunction behavior;
    size_t options;
    void* results;
} Popup;

Popup* GenerateEmptyPopup();

void CleanPopup(Popup* popup);

Popup* GenerateAddObjectPopup();

Popup* GenerateAddSimObjectPopup();

#endif
