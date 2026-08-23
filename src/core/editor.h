#ifndef EDITOR_H
#define EDITOR_H

#include <stdlib.h>
#include <ui/ui.h>

ARRLIST_Panel* EditorSharedPanels();

ARRLIST_UIConfig* EditorDefaultUIConfig();

void RunEditor();

#endif
