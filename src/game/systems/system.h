#ifndef SYSTEM_H
#define SYSTEM_H

#include "game/declarations.h"

GameSystem* GenerateSystem(
        SystemDrawFunction draw,
        SystemUpdateFunction update,
        SystemKeyEventFunction key,
        SystemMouseButtonEventFunction mousebutton,
        SystemMouseScrollEventFunction mousescroll,
        SystemMouseMoveFunction mousemove,
        SystemCleanFunction clean);

void DestroySystem(GameSystem* system);

#endif
