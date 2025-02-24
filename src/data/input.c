#include "input.h"
#include "core/log.h"

InputMap g_input_map = { 0 };

void InitializeInput() {
    g_input_map.keymap[IK_DEV] = KEY_D;
    g_input_map.keymap[IK_PAN_CAMERA] = KEY_SPACE;
    g_input_map.keymap[IK_RESET_CAMERA] = KEY_GRAVE;

    g_input_map.btnmap[IK_MOUSELEFT] = MOUSE_BUTTON_LEFT;
    g_input_map.btnmap[IK_MOUSERIGHT] = MOUSE_BUTTON_RIGHT;

    g_input_map.initialized = TRUE;
}

void BlockInput() {
    g_input_map.blocked = TRUE;
}

void UnblockInput() {
    g_input_map.blocked = FALSE;
}

BOOL InputKeyPressed(InputKey key) {
    LOG_ASSERT(key < NUMKEYS, "Invalid key code");
    if (g_input_map.blocked) return FALSE;
    return IsKeyPressed(g_input_map.keymap[key]);
}

BOOL InputKeyDown(InputKey key) {
    LOG_ASSERT(key < NUMKEYS, "Invalid key code");
    if (g_input_map.blocked) return FALSE;
    return IsKeyDown(g_input_map.keymap[key]);
}

BOOL InputButtonPressed(InputButton btn) {
    LOG_ASSERT(btn < NUMBTNS, "Invalid button code");
    if (g_input_map.blocked) return FALSE;
    return IsMouseButtonPressed(g_input_map.btnmap[btn]);
}

BOOL InputButtonDown(InputButton btn) {
    LOG_ASSERT(btn < NUMBTNS, "Invalid button code");
    if (g_input_map.blocked) return FALSE;
    return IsMouseButtonDown(g_input_map.btnmap[btn]);
}