#ifndef EXTENDER_H
#define EXTENDER_H

#include "ui/ui.h"

#ifdef EXTEND_PRISM_PANELS
#define DECLARE_PANEL_EXTENSION void ExtendPanelCreation(UI* ui);
DECLARE_PANEL_EXTENSION
#endif

#ifdef EXTEND_PRISM_VIEWPORT
#define DECLARE_VIEWPORT_EXTENSION void ExtendViewportUpdate(RenderTexture2D canvas, float width, float height);
DECLARE_VIEWPORT_EXTENSION
#endif

#endif
