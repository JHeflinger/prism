#ifndef INPUT_H
#define INPUT_H

#include "data/config.h"
#include <raylib.h>

#define NUMKEYS 26
#define NUMBTNS 2

typedef enum {
    IK_DEV = 0,
    IK_PAN_CAMERA = 1,
    IK_RESET_CAMERA = 2,
    IK_FIT_CAMERA = 3,
    IK_TOGGLE_HINTS = 4,
    IK_L_OVERRIDE = 5,
    IK_C_OVERRIDE = 6,
    IK_B_OVERRIDE = 7,
    IK_O_OVERRIDE = 8,
    IK_S_OVERRIDE = 9,
    IK_ZOOM = 10,
    IK_ENTER = 11,
    IK_LEFT = 12,
    IK_RIGHT = 13,
    IK_UP = 14,
    IK_DOWN = 15,
    IK_BACKSPACE = 16,
    IK_DELETE = 17,
    IK_I_OVERRIDE = 18,
    IK_P_OVERRIDE = 19,
    IK_SELECT_FACE = 20,
    IK_SELECT_VERTEX = 21,
    IK_SELECT_NONE = 22,
    IK_SELECT = 23,
    IK_PAN_SELECTED = 24,
    IK_A_OVERRIDE = 25,
} InputKey;

typedef enum {
    IK_MOUSELEFT = 0,
    IK_MOUSERIGHT = 1,
} InputButton;

typedef struct {
    BOOL initialized;
    BOOL blocked;
    KeyboardKey keymap[NUMKEYS];
    MouseButton btnmap[NUMBTNS];
    const char* keynames[NUMKEYS];
    const char* btnnames[NUMBTNS];
} InputMap;

void InitializeInput();

void BlockInput();

void UnblockInput();

const char* InputKeyRepresentation(InputKey key);

const char* InputButtonRepresentation(InputButton button);

BOOL InputKeyPressed(InputKey key);

BOOL InputKeyReleased(InputKey key);

BOOL InputKeyDown(InputKey key);

BOOL InputKeyUp(InputKey key);

BOOL InputButtonPressed(InputButton btn);

BOOL InputButtonReleased(InputButton btn);

BOOL InputButtonDown(InputButton btn);

BOOL InputButtonUp(InputButton btn);

#endif
