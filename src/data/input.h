#ifndef INPUT_H
#define INPUT_H

#include "data/config.h"
#include <raylib.h>

#define NUMKEYS 1

typedef enum {
    IK_DEV = 0,
} InputKey;

typedef struct {
    BOOL initialized;
    KeyboardKey keymap[NUMKEYS];
} InputMap;

void InitializeInput();

BOOL InputKeyPressed(InputKey key);

BOOL InputKeyDown(InputKey key);

#endif