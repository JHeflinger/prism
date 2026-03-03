#include "input.h"
#include <easylogger.h>

InputMap g_input_map = { 0 };

void InitializeInput() {
    g_input_map.keymap[IK_DEV] = KEY_D;
    g_input_map.keymap[IK_PAN_CAMERA] = KEY_SPACE;
    g_input_map.keymap[IK_RESET_CAMERA] = KEY_GRAVE;
    g_input_map.keymap[IK_FIT_CAMERA] = KEY_F;
    g_input_map.keymap[IK_TOGGLE_HINTS] = KEY_H;
    g_input_map.keymap[IK_L_OVERRIDE] = KEY_L;
    g_input_map.keymap[IK_C_OVERRIDE] = KEY_C;
    g_input_map.keymap[IK_B_OVERRIDE] = KEY_B;
    g_input_map.keymap[IK_O_OVERRIDE] = KEY_O;
    g_input_map.keymap[IK_S_OVERRIDE] = KEY_S;
    g_input_map.keymap[IK_ZOOM] = KEY_Z;

    g_input_map.btnmap[IK_MOUSELEFT] = MOUSE_BUTTON_LEFT;
    g_input_map.btnmap[IK_MOUSERIGHT] = MOUSE_BUTTON_RIGHT;

    g_input_map.keynames[IK_DEV] = "D";
    g_input_map.keynames[IK_PAN_CAMERA] = "SPCBAR";
    g_input_map.keynames[IK_RESET_CAMERA] = "~";
    g_input_map.keynames[IK_FIT_CAMERA] = "F";
    g_input_map.keynames[IK_TOGGLE_HINTS] = "H";
    g_input_map.keynames[IK_L_OVERRIDE] = "L";
    g_input_map.keynames[IK_C_OVERRIDE] = "C";
    g_input_map.keynames[IK_B_OVERRIDE] = "B";
    g_input_map.keynames[IK_O_OVERRIDE] = "O";
    g_input_map.keynames[IK_S_OVERRIDE] = "S";
    g_input_map.keynames[IK_ZOOM] = "Z";

    g_input_map.btnnames[IK_MOUSELEFT] = "LEFT CLICK";
    g_input_map.btnnames[IK_MOUSERIGHT] = "RIGHT CLICK";

    g_input_map.initialized = TRUE;
}

void BlockInput() {
    g_input_map.blocked = TRUE;
}

void UnblockInput() {
    g_input_map.blocked = FALSE;
}

const char* InputKeyRepresentation(InputKey key) {
    EZ_ASSERT(key < NUMKEYS, "Invalid key code");
    return g_input_map.keynames[key];
}

const char* InputButtonRepresentation(InputButton button) {
    EZ_ASSERT(button < NUMBTNS, "Invalid button code");
    return g_input_map.btnnames[button];
}

BOOL InputKeyPressed(InputKey key) {
    EZ_ASSERT(key < NUMKEYS, "Invalid key code");
    if (g_input_map.blocked) return FALSE;
    return IsKeyPressed(g_input_map.keymap[key]);
}

BOOL InputKeyReleased(InputKey key) {
    EZ_ASSERT(key < NUMKEYS, "Invalid key code");
    if (g_input_map.blocked) return FALSE;
    return IsKeyReleased(g_input_map.keymap[key]);
}

BOOL InputKeyDown(InputKey key) {
    EZ_ASSERT(key < NUMKEYS, "Invalid key code");
    if (g_input_map.blocked) return FALSE;
    return IsKeyDown(g_input_map.keymap[key]);
}

BOOL InputKeyUp(InputKey key) {
    EZ_ASSERT(key < NUMKEYS, "Invalid key code");
    if (g_input_map.blocked) return FALSE;
    return IsKeyUp(g_input_map.keymap[key]);
}

BOOL InputButtonPressed(InputButton btn) {
    EZ_ASSERT(btn < NUMBTNS, "Invalid button code");
    if (g_input_map.blocked) return FALSE;
    return IsMouseButtonPressed(g_input_map.btnmap[btn]);
}

BOOL InputButtonReleased(InputButton btn) {
    EZ_ASSERT(btn < NUMBTNS, "Invalid button code");
    if (g_input_map.blocked) return FALSE;
    return IsMouseButtonReleased(g_input_map.btnmap[btn]);
}

BOOL InputButtonDown(InputButton btn) {
    EZ_ASSERT(btn < NUMBTNS, "Invalid button code");
    if (g_input_map.blocked) return FALSE;
    return IsMouseButtonDown(g_input_map.btnmap[btn]);
}

BOOL InputButtonUp(InputButton btn) {
    EZ_ASSERT(btn < NUMBTNS, "Invalid button code");
    if (g_input_map.blocked) return FALSE;
    return IsMouseButtonUp(g_input_map.btnmap[btn]);
}
