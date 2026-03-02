#ifndef INPUT_H
#define INPUT_H

#include "data/config.h"
#include <raylib.h>

#define NUMKEYS 5
#define NUMBTNS 2

typedef enum {
    IK_DEV = 0,
    IK_PAN_CAMERA = 1,
    IK_RESET_CAMERA = 2,
    IK_FIT_CAMERA = 3,
    IK_TOGGLE_HINTS = 4,
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
