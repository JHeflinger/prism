#include "system.h"
#include "game/definitions.h"

GameSystem* GenerateSystem(
        SystemDrawFunction draw,
        SystemUpdateFunction update,
        SystemKeyEventFunction key,
        SystemMouseButtonEventFunction mousebutton,
        SystemMouseScrollEventFunction mousescroll,
        SystemMouseMoveFunction mousemove,
        SystemCleanFunction clean) {
    GameSystem* system = EZ_ALLOC(1, sizeof(GameSystem));
    system->draw = draw;
    system->update = update;
    system->key = key;
    system->mousebutton = mousebutton;
    system->mousescroll = mousescroll;
    system->mousemove = mousemove;
    system->clean = clean;
    return system;
}

void DestroySystem(GameSystem* system) {
    if (system->clean) system->clean(system);
    EZ_FREE(system);
}
