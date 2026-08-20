#include "defaults.h"
#include <core/entrypoint.h>

void DefaultPreload() { }

void DefaultPostload() { }

void DefaultPreupdate() { }

void DefaultCleanup() { }

REGISTER_PRELOAD(DefaultPreload);
REGISTER_POSTLOAD(DefaultPostload);
REGISTER_PREUPDATE(DefaultPreupdate);
REGISTER_CLEANUP(DefaultCleanup);
