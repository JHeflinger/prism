#include "input.h"
#include "core/log.h"

InputMap g_input_map = { 0 };

void InitializeInput() {
    g_input_map.keymap[IK_DEV] = KEY_GRAVE;
    g_input_map.initialized = TRUE;
}

BOOL InputKeyPressed(InputKey key) {
    LOG_ASSERT(key < NUMKEYS, "Invalid key code");
    return IsKeyPressed(g_input_map.keymap[key]);
}

BOOL InputKeyDown(InputKey key) {
    LOG_ASSERT(key < NUMKEYS, "Invalid key code");
    return IsKeyDown(g_input_map.keymap[key]);
}