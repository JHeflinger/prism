#include "simulate.h"
#include "renderer/renderer.h"

void DrawSimulatePanel(float width, float height) {
    //FluidSimulation* sim = &(RendererGeometry()->fluid);

}

Panel GenerateSimulatePanel() {
	Panel p = { 0 };
	SetupPanel(&p, "Simulate");
	p.draw = DrawSimulatePanel;
	return p;
}
