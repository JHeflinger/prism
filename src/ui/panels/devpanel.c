#include "devpanel.h"
#include <string.h>

void ConfigureDevPanel(Panel* panel) {
    strcpy(panel->name, "Developer Settings");
}