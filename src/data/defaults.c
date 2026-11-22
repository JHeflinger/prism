#include "defaults.h"
#include "data/commands.h"
#include <core/entrypoint.h>
#include <ui/panels/console.h>

void DefaultPreload() {
    RegisterCommand((Command){ "help", HelpCommand, "help {usage|descriptions}", "Outputs help on console commands"});
    RegisterCommand((Command){ "config", ConfigureCommand, "config {read|write} {name} {value", "Read or write to the renderer config" });
    RegisterCommand((Command){ "timeevent", TimeEventCommand, "timeevent {<seconds>} ...", "Set a command to execute in a given amount of time" });
    RegisterCommand((Command){ "notify", NotifyCommand, "notify <level> ...", "Send a notification of a given level and with the given text" });
    RegisterCommand((Command){ "clear", ClearCommand, "clear {notifications}", "Clear a given data subject" });
    RegisterCommand((Command){ "refresh", RefreshCommand, "refresh {assets}", "Refresh a given data subject" });
    RegisterCommand((Command){ "select", SelectCommand, "select {worldname} {id}", "Select an entity given its world name and entity ID" });
}

void DefaultPostload() { }

void DefaultPreupdate() { }

void DefaultCleanup() { }

REGISTER_PRELOAD(DefaultPreload);
REGISTER_POSTLOAD(DefaultPostload);
REGISTER_PREUPDATE(DefaultPreupdate);
REGISTER_CLEANUP(DefaultCleanup);
