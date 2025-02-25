#include "popup.h"
#include "data/input.h"
#include "data/colors.h"
#include "core/log.h"
#include <easymemory.h>

int add_object_popup_stage_0(size_t x, size_t y, size_t w, size_t h) {
    if (InputButtonPressed(IK_MOUSELEFT)) LOG_INFO("heya");
    float width = 600;
    float height = 400;
    float xpos = x + ((w - width) / 2.0f);
    float ypos = y + ((h - height) / 2.0f);
    DrawRectangle(xpos, ypos, width, height, MappedColor(PANEL_BG_COLOR));
    return -1;
}

Popup* GenerateEmptyPopup() {
    return EZALLOC(1, sizeof(Popup));
}

void CleanPopup(Popup* popup) {
    if (popup->options != 0)
        for (size_t i = 0; i < popup->options; i++)
            CleanPopup(((Popup**)popup->results)[i]);
    EZFREE(popup);
}

Popup* GenerateAddObjectPopup() {
    Popup* popup = GenerateEmptyPopup();
    popup->options = 0;
    popup->behavior = add_object_popup_stage_0;
    return popup;
}