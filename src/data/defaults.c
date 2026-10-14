#include "defaults.h"
#include "data/commands.h"
#include <core/entrypoint.h>
#include <ui/panels/console.h>

void DefaultPreload() {
    RegisterCommand((Command){ "help", HelpCommand, "help {usage|descriptions}", "Outputs help on console commands"});
    RegisterCommand((Command){ "config", ConfigureCommand, "config {read|write} {name} {value", "Read or write to the renderer config" });
}

void DefaultPostload() { }

void DefaultPreupdate() { }

void DefaultCleanup() { }

REGISTER_PRELOAD(DefaultPreload);
REGISTER_POSTLOAD(DefaultPostload);
REGISTER_PREUPDATE(DefaultPreupdate);
REGISTER_CLEANUP(DefaultCleanup);
