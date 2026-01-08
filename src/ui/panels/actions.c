#include "actions.h"

void DrawActionsPanel(float width, float height) {
    UIDrawText("Render Mode: realtime or offline");
	UIDrawText("Render Technique: raytracing or pathtracing");
	UIDrawText("Have a popup warning if attempt pathtracing while realtime");
	UIDrawText("If offline, have a menu for configureation");
	UIDrawText(" - numpaths");
	UIDrawText(" - spectral?");
	UIDrawText("And a start render button for offline");
	UIDrawText("Move renderer configureations out of diagnostics and into here");
}

Panel GenerateActionsPanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Actions");
	p.draw = DrawActionsPanel;
	return p;
}
